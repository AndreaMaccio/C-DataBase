#include "../include/hashmap.h"
#include "../include/server.h"
#include "../include/storage.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PORT 8080
#define AUTOSAVE_INTERVAL 30

hashmap_t *db;

void *autosave_thread(void *arg) {
  while (1) {
    sleep(AUTOSAVE_INTERVAL);
    printf("[autosave] saving database...\n");

    if (checkpoint_database(db, "data/dump.txt") == 0) {
      printf("[autosave] save complete\n");
    } else {
      printf("[autosave] save failed\n");
    }
  }
  return NULL;
}

int main() {
  printf("Avvio Database...\n");

  db = hashmap_create(128);

  if (storage_init("data/wal.log") != 0) {
    fprintf(stderr, "Errore inizializzazione storage\n");
    exit(EXIT_FAILURE);
  }

  printf("Caricamento snapshot precedente...\n");
  storage_load_snapshot(db, "data/dump.txt");

  printf("Replay del WAL in corso...\n");
  storage_replay_wal(db);

  pthread_t autosave_tid;
  if (pthread_create(&autosave_tid, NULL, autosave_thread, NULL) != 0) {
    perror("pthread_create autosave");
    exit(EXIT_FAILURE);
  }
  pthread_detach(autosave_tid);

  server_start(PORT, db);

  return 0;
}