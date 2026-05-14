#ifndef HASHMAP_H
#define HASHMAP_H
#define MAX_LOAD 0.75

#include <stddef.h>

typedef struct entry {
  char *key;
  char *value;
  struct entry *next;
} entry_t;

typedef struct {
  entry_t **buckets;
  size_t size, count;
} hashmap_t;

hashmap_t *hashmap_create(size_t size);

void hashmap_set(hashmap_t *map, const char *key, const char *value);

char *hashmap_get(hashmap_t *map, const char *key);

void hashmap_free(hashmap_t *map);

void hashmap_del(hashmap_t *map, const char *key);

void hashmap_expand(hashmap_t *map);

#endif