#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

// Commands sent by the client, responses sent back by the server
enum opcode { OP_SET, OP_GET, OP_DEL, OP_SAVE, OP_OK, OP_ERR, OP_NULL };

// Fixed-size binary header (9 bytes total).
// Packed to avoid compiler padding, so it can be sent/received
// directly over the wire without serialization issues.
typedef struct __attribute__((packed)) header {
  uint8_t opcode;    // which operation (see enum above)
  uint32_t key_len;  // how many bytes the key occupies
  uint32_t val_len;  // how many bytes the value occupies (0 for GET/DEL)
} protocol_header_t;

#endif