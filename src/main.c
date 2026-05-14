#include "../include/hashmap.h"
#include "../include/server.h"
#include "../include/signalhandling.h"
#include "../include/storage.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PORT 8080
#define AUTOSAVE_INTERVAL 30 // seconds between automatic background saves

hashmap_t *db;

// Background thread that periodically triggers a non-blocking snapshot.
// Uses bgsave (fork-based) so the server keeps serving clients
// while the child process writes to disk.
void *autosave_thread(void *arg) {
  (void)arg;
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
  printf("Starting database...\n");

  db = hashmap_create(128);

  // Open the WAL file for appending new operations
  if (storage_init("data/wal.log") != 0) {
    fprintf(stderr, "Storage initialization failed\n");
    exit(EXIT_FAILURE);
  }

  // Recovery sequence: first load the last full snapshot (included in the wal
  // replay), then replay any operations that happened after it
  printf("Replaying WAL...\n");
  storage_replay_wal(db);

  // Kick off the autosave thread before starting the server
  pthread_t autosave_tid;
  if (pthread_create(&autosave_tid, NULL, autosave_thread, NULL) != 0) {
    perror("pthread_create autosave");
    exit(EXIT_FAILURE);
  }

  // This blocks until keep_running becomes 0 (via SIGINT/SIGTERM)
  server_start(PORT, db);

  // Graceful shutdown: wait for autosave to finish, then do
  // one final checkpoint so we don't lose recent writes
  printf("\n [Server] Shutting down, please wait...\n");
  pthread_join(autosave_tid, NULL);
  printf("\n [Server] Running final snapshot...\n");
  checkpoint_database(db, "data/dump.txt");
  hashmap_free(db);
  printf("\n [Server] Safe write completed.\n");

  return 0;
}