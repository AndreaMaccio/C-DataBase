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

typedef struct {
  int client_fd;
  hashmap_t *db;
} client_args_t;

static void *client_handler(void *arg) {
  client_args_t *args = (client_args_t *)arg;
  int client_fd = args->client_fd;
  hashmap_t *db = args->db;
  free(args);

  protocol_header_t header;
  char *key = NULL;
  char *value = NULL;
  ssize_t bytes_received;

  printf("Thread client avviato\n");

  while (1) {
    bytes_received =
        recv(client_fd, &header, sizeof(protocol_header_t), MSG_WAITALL);

    if (bytes_received < 0) {
      perror("recv");
      break;
    }

    if (bytes_received == 0) {
      printf("Client disconnesso\n");
      break;
    }

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

    printf("[Server] Ricevuto pacchetto: OP=%d, Key='%s', Val='%s'\n",
           header.opcode, (key != NULL) ? key : "nessuna",
           (value != NULL) ? value : "nessuno");

    switch (header.opcode) {
    case OP_SET:
      client_execute_set(db, key, value);
      header.opcode = OP_OK;
      header.key_len = 0;
      header.val_len = 0;
      send(client_fd, &header, sizeof(protocol_header_t), 0);
      break;
    case OP_GET:
      hashmap_get(db, key);
      break;
    case OP_DEL:
      client_execute_del(db, key);
      break;
    case OP_SAVE:
      checkpoint_database(db, "data/dump.txt");
      break;
    }

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

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  if (server_fd < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("bind");
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  if (listen(server_fd, 10) < 0) {
    perror("listen");
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  printf("Server in ascolto sulla porta %d\n", port);

  while (keep_running) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd =
        accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

    if (client_fd < 0) {

      if (!keep_running) {
        break;
      }

      perror("accept");
      continue;
    }

    printf("Nuovo client connesso\n");

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

    pthread_detach(tid);
  }

  close(server_fd);
}
