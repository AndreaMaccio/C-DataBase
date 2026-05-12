#include "../include/hashmap.h"
#include <_strings.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// djb2 hash function — simple, fast, and gives decent distribution
static unsigned long hash(const char *str) {
  unsigned long h = 5381;

  int c;

  while ((c = *str++)) {
    h = ((h << 5) + h) + c;
  }

  return h;
}

// Allocates a new hashmap with `size` buckets (all initially empty)
hashmap_t *hashmap_create(size_t size) {
  hashmap_t *map = malloc(sizeof(hashmap_t));
  map->size = size;
  map->count = 0;
  map->buckets = calloc(size, sizeof(entry_t *));
  pthread_mutex_init(&map->mutex, NULL);
  return map;
}

// _nolock variant: caller must hold the mutex.
// If the key exists, update the value. Otherwise insert at the head of the chain.
// Triggers expansion if load factor exceeds MAX_LOAD.
void hashmap_set_nolock(hashmap_t *map, const char *key, const char *value) {
  if (map->count > map->size * MAX_LOAD) {
    hashmap_expand_nolock(map);
  }

  unsigned long h = hash(key);
  size_t index = h % map->size;
  entry_t *current = map->buckets[index];

  // Check if key already exists in this bucket's chain
  while (current) {
    if (strcmp(current->key, key) == 0) {
      free(current->value);
      current->value = strdup(value);
      return;
    }
  }

  // Key not found, prepend a new entry
  entry_t *entry = malloc(sizeof(entry_t));
  map->count++;
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

// Returns a strdup'd copy of the value (caller must free it), or NULL if not found.
// We return a copy so the caller can safely use it after releasing the lock.
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

// Frees everything: all entries in every bucket, the bucket array, and the map itself
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

// Removes an entry from the chain. Handles both head and mid-chain removal.
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
      map->count--;

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

// Doubles the number of buckets and rehashes all existing entries.
// This keeps lookup time close to O(1) by preventing chains from getting too long.
void hashmap_expand_nolock(hashmap_t *map) {
  size_t new_size = map->size * 2;
  entry_t **new_buckets = calloc(new_size, sizeof(entry_t *));

  if (!new_buckets) {
    perror("Out of memory");
    return;
  }

  // Rehash every entry into the new bucket array
  for (size_t i = 0; i < map->size; i++) {
    entry_t *current = map->buckets[i];

    while (current != NULL) {
      entry_t *next_node = current->next;

      unsigned long h = hash(current->key);
      size_t new_index = h % new_size;

      current->next = new_buckets[new_index];
      new_buckets[new_index] = current;

      current = next_node;
    }
  }

  free(map->buckets);

  map->buckets = new_buckets;
  map->size = new_size;

  printf("[hashmap] Expanded to %zu buckets!\n", new_size);
}

void hashmap_expand(hashmap_t *map) {
  pthread_mutex_lock(&map->mutex);
  hashmap_expand_nolock(map);
  pthread_mutex_unlock(&map->mutex);
}