#include "../include/server.h"
#include "../include/protocol.h"
#include "../include/signalhandling.h"
#include "../include/storage.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <unistd.h>

// Per-client state machine for non-blocking I/O
typedef enum {
  STATE_HEADER,
  STATE_KEY,
  STATE_VALUE,
  STATE_PROCESS
} client_state_t;

typedef struct {
  int fd;
  client_state_t state;
  protocol_header_t header;
  char *key;
  char *value;
  size_t bytes_read;
} client_t;

void set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void handle_client(client_t *c, hashmap_t *db) {
  if (c->state == STATE_HEADER) {
    ssize_t n = read(c->fd, (char *)&c->header + c->bytes_read,
                     sizeof(protocol_header_t) - c->bytes_read);
    if (n <= 0)
      return;
    c->bytes_read += n;
    if (c->bytes_read == sizeof(protocol_header_t)) {
      c->bytes_read = 0;
      if (c->header.key_len > 0) {
        c->key = malloc(c->header.key_len + 1);
        c->state = STATE_KEY;
      } else if (c->header.val_len > 0) {
        c->value = malloc(c->header.val_len + 1);
        c->state = STATE_VALUE;
      } else {
        c->state = STATE_PROCESS;
      }
    }
  }

  if (c->state == STATE_KEY) {
    ssize_t n =
        read(c->fd, c->key + c->bytes_read, c->header.key_len - c->bytes_read);
    if (n <= 0)
      return;
    c->bytes_read += n;
    if (c->bytes_read == c->header.key_len) {
      c->key[c->header.key_len] = '\0';
      c->bytes_read = 0;
      if (c->header.val_len > 0) {
        c->value = malloc(c->header.val_len + 1);
        c->state = STATE_VALUE;
      } else {
        c->state = STATE_PROCESS;
      }
    }
  }

  if (c->state == STATE_VALUE) {
    ssize_t n = read(c->fd, c->value + c->bytes_read,
                     c->header.val_len - c->bytes_read);
    if (n <= 0)
      return;
    c->bytes_read += n;
    if (c->bytes_read == c->header.val_len) {
      c->value[c->header.val_len] = '\0';
      c->bytes_read = 0;
      c->state = STATE_PROCESS;
    }
  }

  if (c->state == STATE_PROCESS) {
    protocol_header_t resp;

    switch (c->header.opcode) {
    case OP_SET:
      client_execute_set(db, c->key, c->value);
      resp.opcode = OP_OK;
      resp.key_len = 0;
      resp.val_len = 0;
      send(c->fd, &resp, sizeof(protocol_header_t), 0);
      break;
    case OP_GET: {
      char *result = hashmap_get(db, c->key);
      if (!result) {
        resp.opcode = OP_NULL;
        resp.key_len = 0;
        resp.val_len = 0;
        send(c->fd, &resp, sizeof(protocol_header_t), 0);
      } else {
        resp.opcode = OP_OK;
        resp.key_len = 0;
        resp.val_len = strlen(result);
        send(c->fd, &resp, sizeof(protocol_header_t), 0);
        send(c->fd, result, resp.val_len, 0);
        free(result);
      }
      break;
    }
    case OP_DEL:
      client_execute_del(db, c->key);
      resp.opcode = OP_OK;
      resp.key_len = 0;
      resp.val_len = 0;
      send(c->fd, &resp, sizeof(protocol_header_t), 0);
      break;
    case OP_SAVE:
      checkpoint_database(db, "data/dump.txt");
      resp.opcode = OP_OK;
      resp.key_len = 0;
      resp.val_len = 0;
      send(c->fd, &resp, sizeof(protocol_header_t), 0);
      break;
    }

    // Reset for the next command
    if (c->key) {
      free(c->key);
      c->key = NULL;
    }
    if (c->value) {
      free(c->value);
      c->value = NULL;
    }
    c->state = STATE_HEADER;
    c->bytes_read = 0;
  }
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

  int kq = kqueue();

  struct kevent change;
  EV_SET(&change, server_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
  kevent(kq, &change, 1, NULL, 0, NULL);
  EV_SET(&change, 1, EVFILT_TIMER, EV_ADD, NOTE_SECONDS, 30, NULL);
  kevent(kq, &change, 1, NULL, 0, NULL);

  struct kevent events[64];
  // Event loop: kevent() sleeps until a socket has data, a new client
  // connects, or the autosave timer fires. Single-threaded, zero locks.
  while (keep_running) {
    int n = kevent(kq, NULL, 0, events, 64, NULL);

    for (int i = 0; i < n; i++) {
      if ((int)events[i].ident == server_fd) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd =
            accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        client_t *client = malloc(sizeof(client_t));
        client->fd = client_fd;
        client->state = STATE_HEADER;
        client->bytes_read = 0;
        client->key = NULL;
        client->value = NULL;
        set_nonblocking(client_fd);
        EV_SET(&change, client_fd, EVFILT_READ, EV_ADD, 0, 0, client);
        kevent(kq, &change, 1, NULL, 0, NULL);
        printf("New client connected\n");
      } else if (events[i].filter == EVFILT_TIMER) {
        bgsave_database(db, "data/dump.txt", "data/wal.log");
      } else {
        client_t *c = (client_t *)events[i].udata;
        if (events[i].flags & EV_EOF) {
          printf("Client disconnected\n");
          close(c->fd);
          if (c->key)
            free(c->key);
          if (c->value)
            free(c->value);
          free(c);
        } else {
          handle_client(c, db);
        }
      }
    }
  }

  close(server_fd);
}
