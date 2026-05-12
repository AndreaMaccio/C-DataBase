#include "../include/server.h"
#include "../include/protocol.h"
#include "../include/signalhandling.h"
#include "../include/storage.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

// Struct we malloc for each new client thread.
// Has to be heap-allocated because the accept loop would overwrite
// a stack variable before the new thread gets a chance to read it.
typedef struct {
  int client_fd;
  hashmap_t *db;
} client_args_t;

// Each connected client gets its own thread running this function.
// Reads binary packets (header + payload), executes the command,
// and sends back a response on the same client_fd.
static void *client_handler(void *arg) {
  client_args_t *args = (client_args_t *)arg;
  int client_fd = args->client_fd;
  hashmap_t *db = args->db;
  free(args); // we copied what we need, free the heap struct

  protocol_header_t header;
  char *key = NULL;
  char *value = NULL;
  ssize_t bytes_received;

  printf("Client thread started\n");

  while (1) {
    // Block until we get a full 9-byte header.
    // MSG_WAITALL prevents partial reads from waking us up early.
    bytes_received =
        recv(client_fd, &header, sizeof(protocol_header_t), MSG_WAITALL);

    if (bytes_received < 0) {
      perror("recv");
      break;
    }

    if (bytes_received == 0) {
      printf("Client disconnected\n");
      break;
    }

    // Allocate exact-size buffers based on lengths from the header.
    // +1 for the null terminator that we add ourselves (the client
    // doesn't send it, to save bandwidth).
    if (header.key_len > 0) {
      key = malloc(header.key_len + 1);
      recv(client_fd, key, header.key_len, MSG_WAITALL);
      key[header.key_len] = '\0';
    }

    if (header.val_len > 0) {
      value = malloc(header.val_len + 1);
      recv(client_fd, value, header.val_len, MSG_WAITALL);
      value[header.val_len] = '\0';
    }

    printf("[Server] Received packet: OP=%d, Key='%s', Val='%s'\n",
           header.opcode, (key != NULL) ? key : "(none)",
           (value != NULL) ? value : "(none)");

    switch (header.opcode) {
    case OP_SET:
      client_execute_set(db, key, value);
      // Reuse the header struct for the response
      header.opcode = OP_OK;
      header.key_len = 0;
      header.val_len = 0;
      send(client_fd, &header, sizeof(protocol_header_t), 0);
      break;
    case OP_GET: {
      char *result = hashmap_get(db, key);

      if (!result) {
        header.opcode = OP_NULL;
        header.key_len = 0;
        header.val_len = 0;
        send(client_fd, &header, sizeof(protocol_header_t), 0);
        break;
      }

      header.opcode = OP_OK;
      header.key_len = 0;
      header.val_len = strlen(result);

      send(client_fd, &header, sizeof(protocol_header_t), 0);
      send(client_fd, &result, header.val_len, 0);
      free(result);
      break;
    }
    case OP_DEL:
      client_execute_del(db, key);
      break;
    case OP_SAVE:
      checkpoint_database(db, "data/dump.txt");
      break;
    }

    // Free this iteration's buffers to avoid leaking memory.
    // The hashmap already made its own copies via strdup().
    if (header.key_len > 0)
      free(key);
    if (header.val_len > 0)
      free(value);
    key = NULL;
    value = NULL;
  }

  close(client_fd);
  return NULL;
}

void server_start(int port, hashmap_t *db) {
  int server_fd;
  struct sockaddr_in server_addr;

  server_fd = socket(AF_INET, SOCK_STREAM, 0);

  // Allow immediate restart after crash (avoids "Address already in use")
  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  if (server_fd < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY; // listen on all interfaces
  server_addr.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("bind");
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  // Backlog of 10: max queued connections before refusing new ones
  if (listen(server_fd, 10) < 0) {
    perror("listen");
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  printf("Server listening on port %d\n", port);

  // Main accept loop. Blocks on accept() waiting for new clients.
  // For each one, we spawn a dedicated thread.
  while (keep_running) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd =
        accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

    if (client_fd < 0) {
      // If we got interrupted by a signal (SIGINT), just exit cleanly
      if (!keep_running) {
        break;
      }

      perror("accept");
      continue;
    }

    printf("New client connected\n");

    // Heap-allocate args so the thread can read them safely
    // even after we loop back to accept the next client
    client_args_t *args = malloc(sizeof(client_args_t));
    args->client_fd = client_fd;
    args->db = db;

    pthread_t tid;
    if (pthread_create(&tid, NULL, client_handler, args) != 0) {
      perror("pthread_create");
      close(client_fd);
      free(args);
      continue;
    }

    // Detach so we don't have to join each thread manually
    pthread_detach(tid);
  }

  close(server_fd);
}
