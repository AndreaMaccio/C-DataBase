#include "../include/storage.h"
#include "../include/protocol.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

  for (size_t i = 0; i < map->size; i++) {
    entry_t *current = map->buckets[i];
    while (current) {
      fprintf(fp, "%s=%s\n", current->key, current->value);
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

// Loads a text snapshot (key=value per line) into memory at startup
int storage_load_snapshot(hashmap_t *map, const char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp) {
    return -1;
  }

  char line[1024];
  while (fgets(line, sizeof(line), fp)) {
    line[strcspn(line, "\n")] = '\0';
    char *saveptr;
    char *key = strtok_r(line, "=", &saveptr);
    char *value = strtok_r(NULL, "=", &saveptr);

    if (key && value) {
      hashmap_set(map, key, value);
    }
  }
  fclose(fp);
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
  FILE *fp = fopen(filepath, "r");
  if (!fp) {
    return;
  }

  protocol_header_t header;
  while (fread(&header, sizeof(header), 1, fp) == 1) {
    char *key = NULL;
    char *value = NULL;

    if (header.key_len > 0) {
      key = malloc(header.key_len + 1);
      fread(key, 1, header.key_len, fp);
      key[header.key_len] = '\0';
    }

    if (header.val_len > 0) {
      value = malloc(header.val_len + 1);
      fread(value, 1, header.val_len, fp);
      value[header.val_len] = '\0';
    }

    if (header.opcode == OP_SET) {
      hashmap_set(map, key, value);
    } else if (header.opcode == OP_DEL) {
      hashmap_del(map, key);
    }

    if (key) free(key);
    if (value) free(value);
  }
  fclose(fp);
}

// Replays both WAL files in order for full crash recovery:
// 1) wal.log.old (pre-bgsave ops)  2) wal.log (post-bgsave ops)
int storage_replay_wal(hashmap_t *map) {
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

    for (size_t i = 0; i < map->size; i++) {
      entry_t *current = map->buckets[i];
      while (current) {
        fprintf(fp, "%s=%s\n", current->key, current->value);
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