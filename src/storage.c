#include "../include/storage.h"
#include "../include/protocol.h"
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static FILE *wal_fp;
static pthread_mutex_t wal_mutex = PTHREAD_MUTEX_INITIALIZER;

int storage_init(const char *wal_path) {
  wal_fp = fopen(wal_path, "a");
  if (!wal_fp) {
    perror("fopen wal");
    return -1;
  }
  return 0;
}

// Blocking checkpoint: dumps entire hashmap to disk, then clears the WAL.
// Lock order: wal_mutex first, then map->mutex (always, to prevent deadlocks).
int checkpoint_database(hashmap_t *map, const char *snapshot_file) {
  pthread_mutex_lock(&wal_mutex);
  pthread_mutex_lock(&map->mutex);

  FILE *fp = fopen(snapshot_file, "w");
  if (!fp) {
    pthread_mutex_unlock(&map->mutex);
    pthread_mutex_unlock(&wal_mutex);
    return -1;
  }

  protocol_header_t header;
  header.opcode = OP_SET;

  for (size_t i = 0; i < map->size; i++) {
    entry_t *current = map->buckets[i];
    while (current) {
      header.key_len = strlen(current->key);
      header.val_len = strlen(current->value);
      fwrite(&header, sizeof(header), 1, fp);
      fwrite(current->key, sizeof(char), header.key_len, fp);
      fwrite(current->value, sizeof(char), header.val_len, fp);
      current = current->next;
    }
  }

  fflush(fp);
  fsync(fileno(fp));
  fclose(fp);

  // Snapshot is safe on disk, WAL can be truncated
  fflush(wal_fp);
  fsync(fileno(wal_fp));
  ftruncate(fileno(wal_fp), 0);
  fseek(wal_fp, 0, SEEK_SET);

  pthread_mutex_unlock(&map->mutex);
  pthread_mutex_unlock(&wal_mutex);

  return 0;
}

// Atomic SET: writes to binary WAL first, then updates the hashmap.
// Both locks held to keep WAL and memory in sync.
void client_execute_set(hashmap_t *map, const char *key, const char *value) {
  pthread_mutex_lock(&wal_mutex);
  pthread_mutex_lock(&map->mutex);

  protocol_header_t header;
  header.opcode = OP_SET;
  header.key_len = strlen(key);
  header.val_len = strlen(value);

  // Raw bytes: header (9B) + key + value. No text separators.
  fwrite(&header, sizeof(header), 1, wal_fp);
  fwrite(key, 1, header.key_len, wal_fp);
  fwrite(value, 1, header.val_len, wal_fp);
  fflush(wal_fp);

  hashmap_set_nolock(map, key, value);

  pthread_mutex_unlock(&map->mutex);
  pthread_mutex_unlock(&wal_mutex);
}

// Same write-ahead pattern as SET but for deletions
void client_execute_del(hashmap_t *map, const char *key) {
  pthread_mutex_lock(&wal_mutex);
  pthread_mutex_lock(&map->mutex);

  protocol_header_t header;
  header.opcode = OP_DEL;
  header.key_len = strlen(key);
  header.val_len = 0;

  fwrite(&header, sizeof(header), 1, wal_fp);
  fwrite(key, 1, header.key_len, wal_fp);
  fflush(wal_fp);

  hashmap_del_nolock(map, key);

  pthread_mutex_unlock(&map->mutex);
  pthread_mutex_unlock(&wal_mutex);
}

// Replays a single binary WAL file, reading header-by-header
void storage_replay_wal_file(hashmap_t *map, const char *filepath) {
  int fp = open(filepath, O_RDONLY);
  if (fp < 0)
    return;

  struct stat st;
  fstat(fp, &st);

  if (st.st_size == 0) {
    close(fp);
    return;
  }

  char *data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fp, 0);
  close(fp);

  char *cursor = data;
  char *end = data + st.st_size;

  while (cursor + sizeof(protocol_header_t) <= end) {
    protocol_header_t *h = (protocol_header_t *)cursor;
    cursor += sizeof(protocol_header_t);

    char *key = NULL;
    char *value = NULL;

    if (h->key_len > 0) {
      key = malloc(h->key_len + 1);
      memcpy(key, cursor, h->key_len);
      cursor += h->key_len;
      key[h->key_len] = '\0';
    }

    if (h->val_len > 0) {
      value = malloc(h->val_len + 1);
      memcpy(value, cursor, h->val_len);
      cursor += h->val_len;
      value[h->val_len] = '\0';
    }

    if (h->opcode == OP_SET) {
      hashmap_set(map, key, value);
    } else if (h->opcode == OP_DEL) {
      hashmap_del(map, key);
    }

    if (key)
      free(key);
    if (value)
      free(value);
  }
  munmap(data, st.st_size);
}

// Replays both WAL files and DUMP in order for full crash recovery:
// 1) wal.log.old (pre-bgsave ops)  2) wal.log (post-bgsave ops)
int storage_replay_wal(hashmap_t *map) {
  storage_replay_wal_file(map, "data/dump.txt");
  storage_replay_wal_file(map, "data/wal.log.old");
  storage_replay_wal_file(map, "data/wal.log");
  return 0;
}

// Non-blocking snapshot via fork(). Child inherits the hashmap
// through OS Copy-on-Write and writes to a temp file.
// Parent keeps serving clients without pausing.
int bgsave_database(hashmap_t *map, const char *snapshot_file,
                    const char *wal_path) {
  pid_t pid;
  int status;
  char old_wal_path[256];
  snprintf(old_wal_path, sizeof(old_wal_path), "%s.old", wal_path);

  pthread_mutex_lock(&wal_mutex);
  pthread_mutex_lock(&map->mutex);

  // WAL rotation: close -> rename to .old -> open fresh
  fclose(wal_fp);
  rename(wal_path, old_wal_path);
  wal_fp = fopen(wal_path, "a");

  pid = fork();

  if (!pid) {
    // Child: write snapshot to temp file
    FILE *fp = fopen("data/dump_temp.txt", "w");
    if (!fp)
      exit(1);

    protocol_header_t header;
    header.opcode = OP_SET;

    for (size_t i = 0; i < map->size; i++) {
      entry_t *current = map->buckets[i];
      while (current) {
        header.key_len = strlen(current->key);
        header.val_len = strlen(current->value);
        fwrite(&header, sizeof(header), 1, fp);
        fwrite(current->key, sizeof(char), header.key_len, fp);
        fwrite(current->value, sizeof(char), header.val_len, fp);
        current = current->next;
      }
    }

    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
    exit(0);
  }

  // Parent: unlock so the server can keep working
  pthread_mutex_unlock(&wal_mutex);
  pthread_mutex_unlock(&map->mutex);

  waitpid(pid, &status, 0);
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    // Atomic rename: no partial file risk
    rename("data/dump_temp.txt", snapshot_file);
    remove(old_wal_path);
    printf("[bgsave] Snapshot saved in background!\n");
    return 0;
  } else {
    perror("[bgsave] Child process failed\n");
    return -1;
  }
}