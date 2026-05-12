#include "../include/protocol.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080

// Parses user text input (e.g. "SET foo bar") into a binary header
// and extracts pointers to key/value within the original string.
// Returns 0 on success, -1 if the syntax is wrong.
// Note: key and value point into `line` (modified by strtok_r),
// so they're only valid until the next call.
int parse_user_input(char *line, protocol_header_t *header, char **key,
                     char **value) {
  char *saveptr;
  char *cmd = strtok_r(line, " ", &saveptr);
  if (!cmd)
    return -1;

  if (strcmp(cmd, "SET") == 0) {
    header->opcode = OP_SET;
    *key = strtok_r(NULL, " ", &saveptr);
    *value = strtok_r(NULL, "", &saveptr); // rest of the line is the value

    if (!*key || !*value) {
      printf("Error. Usage: SET <key> <value>\n");
      return -1;
    }
    header->key_len = strlen(*key);
    header->val_len = strlen(*value);
    return 0;

  } else if (strcmp(cmd, "GET") == 0) {
    header->opcode = OP_GET;
    *key = strtok_r(NULL, " ", &saveptr);

    if (!*key) {
      printf("Error. Usage: GET <key>\n");
      return -1;
    }
    header->key_len = strlen(*key);
    header->val_len = 0;
    return 0;

  } else if (strcmp(cmd, "DEL") == 0) {
    header->opcode = OP_DEL;
    *key = strtok_r(NULL, " ", &saveptr);

    if (!*key) {
      printf("Error. Usage: DEL <key>\n");
      return -1;
    }
    header->key_len = strlen(*key);
    header->val_len = 0;
    return 0;
  } else if (strcmp(cmd, "SAVE") == 0) {
    header->opcode = OP_SAVE;
    header->key_len = 0;
    header->val_len = 0;
    return 0;
  }

  printf("Unknown command.\n");
  return -1;
}

int main(void) {
  int socket_fd;
  struct sockaddr_in server_addr;

  // Create a TCP socket to talk to the server
  socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  // We connect to localhost on the same port the server listens on
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(PORT);
  server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("connect failed");
    close(socket_fd);
    exit(EXIT_FAILURE);
  }

  printf("Connected to database!\n");

  char line[1024];
  while (1) {
    printf("db> ");
    fflush(stdout);
    if (fgets(line, sizeof(line), stdin) == NULL)
      break;
    line[(strcspn(line, "\n"))] = '\0';
    if (strlen(line) == 0)
      continue;

    protocol_header_t header;
    char *key = NULL;
    char *value = NULL;

    if (parse_user_input(line, &header, &key, &value) == 0) {
      // Send the 9-byte header first, then key and value payloads.
      // The server knows exactly how many bytes to expect from the header.
      send(socket_fd, &header, sizeof(header), 0);
      if (header.key_len > 0) {
        send(socket_fd, key, header.key_len, 0);
      }
      if (header.val_len > 0) {
        send(socket_fd, value, header.val_len, 0);
      }
    }
  }

  close(socket_fd);
  return 0;
}
