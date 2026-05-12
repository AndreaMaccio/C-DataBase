#include "../include/server.h"
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

  char buffer[BUFFER_SIZE * 2];
  memset(buffer, 0, sizeof(buffer));
  char recv_buffer[BUFFER_SIZE];
  ssize_t bytes_received;

  printf("Thread client avviato\n");

  while (1) {
    bytes_received = recv(client_fd, recv_buffer, BUFFER_SIZE - 1, 0);

    if (bytes_received < 0) {
      perror("recv");
      break;
    }

    if (bytes_received == 0) {
      printf("Client disconnesso\n");
      break;
    }

    recv_buffer[bytes_received] = '\0';
    strcat(buffer, recv_buffer);

    char *newline;

    while ((newline = strchr(buffer, '\n')) != NULL) {
      *newline = '\0';
      char *start_next_cmd = newline + 1;
      printf("[Client %d] %s\n", client_fd, buffer);

      // parsing
      char *saveptr;
      char *cmd = strtok_r(buffer, " \r\n", &saveptr);

      if (cmd != NULL) {
        if (strcmp(cmd, "SET") == 0) {
          char *key = strtok_r(NULL, " \r\n", &saveptr);
          char *value = strtok_r(NULL, "\r\n", &saveptr);

          if (key == NULL || value == NULL) {
            char *msg = "ERR usage: SET key value\n";
            send(client_fd, msg, strlen(msg), 0);
          } else {
            client_execute_set(db, key, value);
            char *msg = "OK\n";
            send(client_fd, msg, strlen(msg), 0);
          }
        } else if (strcmp(cmd, "GET") == 0) {
          char *key = strtok_r(NULL, " \r\n", &saveptr);

          if (key == NULL) {
            char *msg = "ERR usage: GET key\n";
            send(client_fd, msg, strlen(msg), 0);
          } else {
            char *value = hashmap_get(db, key);
            if (value) {
              char response[BUFFER_SIZE];
              snprintf(response, sizeof(response), "%s\n", value);
              send(client_fd, response, strlen(response), 0);
              free(value);
            } else {
              char *msg = "NULL\n";
              send(client_fd, msg, strlen(msg), 0);
            }
          }
        } else if (strcmp(cmd, "DEL") == 0) {
          char *key = strtok_r(NULL, " \r\n", &saveptr);

          if (key == NULL) {
            char *msg = "ERR usage: DEL key\n";
            send(client_fd, msg, strlen(msg), 0);
          } else {
            client_execute_del(db, key);
            char *msg = "OK\n";
            send(client_fd, msg, strlen(msg), 0);
          }
        } else if (strcmp(cmd, "SAVE") == 0) {
          if (checkpoint_database(db, "data/dump.txt") == 0) {
            char *msg = "OK\n";
            send(client_fd, msg, strlen(msg), 0);
          } else {
            char *msg = "ERR save failed\n";
            send(client_fd, msg, strlen(msg), 0);
          }
        } else {
          char *msg = "ERR unknown command\n";
          send(client_fd, msg, strlen(msg), 0);
        }
      }

      memmove(buffer, start_next_cmd, strlen(start_next_cmd) + 1);
    }
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

  while (1) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd =
        accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

    if (client_fd < 0) {
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
