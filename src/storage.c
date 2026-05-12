#include "../include/storage.h"
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

void storage_replay_wal_file(hashmap_t *map, const char *filepath) {
  FILE *fp = fopen(filepath, "r");
  if (!fp) {
    return;
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
}

int storage_replay_wal(hashmap_t *map) {

  storage_replay_wal_file(map, "data/wal.log.old");

  storage_replay_wal_file(map, "data/wal.log");

  return 0;
}

int bgsave_database(hashmap_t *map, const char *snapshot_file,
                    const char *wal_path) {
  pid_t pid;
  int status;
  char old_wal_path[256];
  snprintf(old_wal_path, sizeof(old_wal_path), "%s.old", wal_path);

  pthread_mutex_lock(&wal_mutex);
  pthread_mutex_lock(&map->mutex);

  fclose(wal_fp);
  rename(wal_path, old_wal_path);
  wal_fp = fopen(wal_path, "a");

  pid = fork();

  if (!pid) {
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

  pthread_mutex_unlock(&wal_mutex);
  pthread_mutex_unlock(&map->mutex);
  waitpid(pid, &status, 0);
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    rename("data/dump_temp.txt", snapshot_file);
    remove(old_wal_path);
    printf("[bgsave] Snapshot salvato in background!\n");
    return 0;
  } else {
    perror("[bgsave] Errore nel salvataggio del figlio.\n");
    return -1;
  }
}