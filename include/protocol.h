#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

enum opcode { OP_SET, OP_GET, OP_DEL, OP_SAVE, OP_OK, OP_ERR, OP_NULL };

typedef struct __attribute__((packed)) header {
  uint8_t opcode;
  uint32_t key_len;
  uint32_t val_len;
} protocol_header_t;

#endif