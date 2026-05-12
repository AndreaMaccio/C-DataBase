#include "../include/storage.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

  fflush(wal_fp);
  fsync(fileno(wal_fp));
  ftruncate(fileno(wal_fp), 0);
  fseek(wal_fp, 0, SEEK_SET);

  pthread_mutex_unlock(&map->mutex);
  pthread_mutex_unlock(&wal_mutex);

  return 0;
}

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

void client_execute_set(hashmap_t *map, const char *key, const char *value) {
  pthread_mutex_lock(&wal_mutex);
  pthread_mutex_lock(&map->mutex);

  fprintf(wal_fp, "SET %s %s\n", key, value);
  fflush(wal_fp);

  hashmap_set_nolock(map, key, value);

  pthread_mutex_unlock(&map->mutex);
  pthread_mutex_unlock(&wal_mutex);
}

void client_execute_del(hashmap_t *map, const char *key) {
  pthread_mutex_lock(&wal_mutex);
  pthread_mutex_lock(&map->mutex);

  fprintf(wal_fp, "DEL %s\n", key);
  fflush(wal_fp);

  hashmap_del_nolock(map, key);

  pthread_mutex_unlock(&map->mutex);
  pthread_mutex_unlock(&wal_mutex);
}

int storage_replay_wal(hashmap_t *map) {
  FILE *fp = fopen("data/wal.log", "r");
  if (!fp) {
    return -1;
  }

  char line[1024];
  while (fgets(line, sizeof(line), fp)) {
    line[strcspn(line, "\n")] = '\0';
    char *saveptr;
    char *cmd = strtok_r(line, " ", &saveptr);
    if (!cmd) {
      continue;
    }

    if (strcmp(cmd, "SET") == 0) {
      char *key = strtok_r(NULL, " ", &saveptr);
      char *value = strtok_r(NULL, "", &saveptr);
      if (key && value) {
        hashmap_set(map, key, value);
      }
    } else if (strcmp(cmd, "DEL") == 0) {
      char *key = strtok_r(NULL, " ", &saveptr);
      if (key) {
        hashmap_del(map, key);
      }
    }
  }
  fclose(fp);
  return 0;
}
