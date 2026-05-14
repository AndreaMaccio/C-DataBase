#include "../include/storage.h"
#include "../include/protocol.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static FILE *wal_fp;

int storage_init(const char *wal_path) {
  wal_fp = fopen(wal_path, "a");
  if (!wal_fp) {
    perror("fopen wal");
    return -1;
  }
  return 0;
}

// Blocking checkpoint: dumps entire hashmap to disk, then clears the WAL.
int checkpoint_database(hashmap_t *map, const char *snapshot_file) {
  FILE *fp = fopen(snapshot_file, "w");
  if (!fp) {
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

  return 0;
}

// Atomic SET: writes to binary WAL first, then updates the hashmap.
void client_execute_set(hashmap_t *map, const char *key, const char *value) {
  protocol_header_t header;
  header.opcode = OP_SET;
  header.key_len = strlen(key);
  header.val_len = strlen(value);

  fwrite(&header, sizeof(header), 1, wal_fp);
  fwrite(key, 1, header.key_len, wal_fp);
  fwrite(value, 1, header.val_len, wal_fp);
  fflush(wal_fp);

  hashmap_set(map, key, value);
}

// Same write-ahead pattern as SET but for deletions
void client_execute_del(hashmap_t *map, const char *key) {
  protocol_header_t header;
  header.opcode = OP_DEL;
  header.key_len = strlen(key);
  header.val_len = 0;

  fwrite(&header, sizeof(header), 1, wal_fp);
  fwrite(key, 1, header.key_len, wal_fp);
  fflush(wal_fp);

  hashmap_del(map, key);
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
// Track the child process from a previous bgsave
static pid_t bgsave_pid = -1;
static char pending_snapshot[256];
static char pending_old_wal[256];

// Called during shutdown to wait for any in-flight bgsave child
// and perform the rename/cleanup that would normally happen at the next timer tick.
void bgsave_finalize(void) {
  if (bgsave_pid <= 0)
    return;

  int status;
  waitpid(bgsave_pid, &status, 0); // blocking wait — fine during shutdown
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    rename("data/dump_temp.txt", pending_snapshot);
    remove(pending_old_wal);
    printf("[bgsave] Pending snapshot finalized\n");
  }
  bgsave_pid = -1;
}

// Non-blocking snapshot via fork(). Child inherits the hashmap
// through OS Copy-on-Write and writes to a temp file.
// Parent returns immediately to the event loop.
int bgsave_database(hashmap_t *map, const char *snapshot_file,
                    const char *wal_path) {

  // Check if the previous bgsave child has finished
  if (bgsave_pid > 0) {
    int status;
    pid_t result = waitpid(bgsave_pid, &status, WNOHANG);
    if (result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      // Previous child finished successfully, do the rename
      rename("data/dump_temp.txt", pending_snapshot);
      remove(pending_old_wal);
      printf("[bgsave] Snapshot saved in background!\n");
      bgsave_pid = -1;
    } else if (result > 0) {
      // Previous child failed
      fprintf(stderr, "[bgsave] Child process failed\n");
      bgsave_pid = -1;
    } else {
      // result == 0: previous child still running, skip this bgsave
      printf("[bgsave] Previous save still running, skipping\n");
      return 0;
    }
  }

  // Save paths for the rename (will happen at the NEXT timer tick)
  snprintf(pending_snapshot, sizeof(pending_snapshot), "%s", snapshot_file);
  snprintf(pending_old_wal, sizeof(pending_old_wal), "%s.old", wal_path);

  // WAL rotation: close -> rename to .old -> open fresh
  fclose(wal_fp);
  rename(wal_path, pending_old_wal);
  wal_fp = fopen(wal_path, "a");

  pid_t pid = fork();

  if (pid == 0) {
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

  // Parent: save the pid and return immediately to the event loop
  bgsave_pid = pid;
  return 0;
}