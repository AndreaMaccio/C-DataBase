#ifndef STORAGE_H
#define STORAGE_H

#include "hashmap.h"

int storage_init(const char *wal_path);

void client_execute_set(hashmap_t *map, const char *key, const char *value);

void client_execute_del(hashmap_t *map, const char *key);

int checkpoint_database(hashmap_t *map, const char *snapshot_file);

int storage_replay_wal(hashmap_t *map);

int bgsave_database(hashmap_t *map, const char *snapshot_file,
                    const char *wal_path);

void bgsave_finalize(void);

#endif
