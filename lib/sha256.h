#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t h[8];
  uint64_t bytes;
  unsigned char block[64];
  size_t used;
} sequel_sha256;

void sha_init(sequel_sha256 *state);
void sha_update(sequel_sha256 *state, const void *data, size_t length);
void sha_u64(sequel_sha256 *state, uint64_t value);
void sha_frame(sequel_sha256 *state, unsigned char kind, const char *data, size_t length, bool fold);
void sha_final(sequel_sha256 *state, char output[65]);
