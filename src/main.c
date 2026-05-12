#include "../include/hashmap.h"
#include "../include/server.h"
#include "../include/signalhandling.h"
#include "../include/storage.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PORT 8080
#define AUTOSAVE_INTERVAL 30

hashmap_t *db;

void *autosave_thread(void *arg) {
  while (keep_running) {
    sleep(AUTOSAVE_INTERVAL);
    if (!keep_running)
      break;
    printf("[autosave] saving database...\n");

    if (bgsave_database(db, "data/dump.txt", "data/wal.log") == 0) {
      printf("[autosave] bgsave initiated and completed successfully\n");
    } else {
      printf("[autosave] bgsave failed\n");
    }
  }
  return NULL;
}

int main() {
  setup_signal_handlers();
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

  server_start(PORT, db);

  printf("\n [Server] Spegimento in corso, attendere...\n");
  pthread_join(autosave_tid, NULL);
  printf("\n [Server] Esecuzione snapshot finale...\n");
  checkpoint_database(db, "data/dump.txt");
  hashmap_free(db);
  printf("\n [Server] Scrittura sicura completata.");

  return 0;
}