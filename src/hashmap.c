#include "../include/hashmap.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned long hash(const char *str) {
  unsigned long h = 5381;

  int c;

  while ((c = *str++)) {
    h = ((h << 5) + h) + c;
  }

  return h;
}

hashmap_t *hashmap_create(size_t size) {
  hashmap_t *map = malloc(sizeof(hashmap_t));
  map->size = size;
  map->buckets = calloc(size, sizeof(entry_t *));
  pthread_mutex_init(&map->mutex, NULL);
  return map;
}

void hashmap_set_nolock(hashmap_t *map, const char *key, const char *value) {
  unsigned long h = hash(key);
  size_t index = h % map->size;
  entry_t *current = map->buckets[index];

  while (current) {
    if (strcmp(current->key, key) == 0) {
      free(current->value);
      current->value = strdup(value);
      return;
    }
  }

  entry_t *entry = malloc(sizeof(entry_t));
  entry->key = strdup(key);
  entry->value = strdup(value);
  entry->next = map->buckets[index];
  map->buckets[index] = entry;
}

void hashmap_set(hashmap_t *map, const char *key, const char *value) {
  pthread_mutex_lock(&map->mutex);
  hashmap_set_nolock(map, key, value);
  pthread_mutex_unlock(&map->mutex);
}

char *hashmap_get(hashmap_t *map, const char *key) {
  pthread_mutex_lock(&map->mutex);
  unsigned long h = hash(key);
  size_t index = h % map->size;
  entry_t *current = map->buckets[index];

  while (current) {
    if (strcmp(current->key, key) == 0) {
      char *result = strdup(current->value);
      pthread_mutex_unlock(&map->mutex);
      return (result);
    }
    current = current->next;
  }
  pthread_mutex_unlock(&map->mutex);
  return NULL;
}

void hashmap_free(hashmap_t *map) {
  for (size_t i = 0; i < map->size; i++) {
    entry_t *current = map->buckets[i];

    while (current) {
      entry_t *tmp = current;
      current = current->next;

      free(tmp->value);
      free(tmp->key);
      free(tmp);
    }
  }
  free(map->buckets);
  pthread_mutex_destroy(&map->mutex);
  free(map);
}

void hashmap_del_nolock(hashmap_t *map, const char *key) {
  unsigned long h = hash(key);
  size_t index = h % map->size;
  entry_t *current = map->buckets[index];
  entry_t *prev = NULL;

  while (current) {
    if (strcmp(current->key, key) == 0) {
      if (prev)
        prev->next = current->next;
      else
        map->buckets[index] = current->next;
      free(current->key);
      free(current->value);
      free(current);

      return;
    }
    prev = current;
    current = current->next;
  }
}

void hashmap_del(hashmap_t *map, const char *key) {
  pthread_mutex_lock(&map->mutex);
  hashmap_del_nolock(map, key);
  pthread_mutex_unlock(&map->mutex);
}
