#ifndef HOBBY_OS_SHA256_H
#define HOBBY_OS_SHA256_H

#include <stdint.h>

/*
 * Compute the SHA-256 of `len` bytes at `data` and write the 32-byte
 * digest to `out`. Self-contained, no allocations.
 */
void sha256(const void *data, uint32_t len, uint8_t out[32]);

/* Convert a 32-byte digest into 64 lowercase hex characters
 * (NUL-terminated, so out_hex must hold 65 bytes). */
void sha256_hex(const uint8_t digest[32], char out_hex[65]);

#endif
