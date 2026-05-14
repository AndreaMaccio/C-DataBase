#include "../include/hashmap.h"
#include "../include/server.h"
#include "../include/signalhandling.h"
#include "../include/storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PORT 8080
#define AUTOSAVE_INTERVAL 30 // seconds between automatic background saves

hashmap_t *db;

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

  // This blocks until keep_running becomes 0 (via SIGINT/SIGTERM)
  server_start(PORT, db);

  // Graceful shutdown: finalize any pending bgsave, then do
  // one final checkpoint so we don't lose recent writes
  printf("\n [Server] Shutting down, please wait...\n");
  bgsave_finalize();
  printf("\n [Server] Running final snapshot...\n");
  checkpoint_database(db, "data/dump.txt");
  hashmap_free(db);
  printf("\n [Server] Safe write completed.\n");

  return 0;
}