#ifndef HASHMAP_H
#define HASHMAP_H

#include <pthread.h>
#include <stddef.h>

typedef struct entry {
  char *key;
  char *value;
  struct entry *next;
} entry_t;

typedef struct {
  entry_t **buckets;
  size_t size;
  pthread_mutex_t mutex;
} hashmap_t;

hashmap_t *hashmap_create(size_t size);

void hashmap_set(hashmap_t *map, const char *key, const char *value);

void hashmap_set_nolock(hashmap_t *map, const char *key, const char *value);

char *hashmap_get(hashmap_t *map, const char *key);

void hashmap_free(hashmap_t *map);

void hashmap_del(hashmap_t *map, const char *key);

void hashmap_del_nolock(hashmap_t *map, const char *key);

#endif