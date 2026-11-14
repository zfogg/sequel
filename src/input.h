#pragma once

#include <sequel.h>
#include <stdio.h>
#include <stdlib.h>

/* Grow only for bytes actually read, retaining one overflow byte and a NUL. */
static inline int input_reserve(char **buffer, size_t *capacity, size_t needed) {
  if (needed > SEQUEL_INPUT_MAX + 2)
    return 0;
  if (needed <= *capacity)
    return 1;
  size_t next = *capacity ? *capacity : 8192;
  if (next > SEQUEL_INPUT_MAX + 2)
    next = SEQUEL_INPUT_MAX + 2;
  while (next < needed)
    next = next > (SEQUEL_INPUT_MAX + 2) / 2 ? SEQUEL_INPUT_MAX + 2 : next * 2;
  char *grown = realloc(*buffer, next);
  if (!grown)
    return 0;
  *buffer = grown;
  *capacity = next;
  return 1;
}

/* Read at most the SQL input limit plus one byte to detect overflow. */
static inline char *input_read(FILE *stream, size_t *length) {
  char *buffer = NULL;
  size_t capacity = 0, n = 0;
  do {
    if (!input_reserve(&buffer, &capacity, n + 2)) {
      free(buffer);
      return NULL;
    }
    size_t got = fread(buffer + n, 1, capacity - n - 1, stream);
    n += got;
    if (ferror(stream)) {
      free(buffer);
      return NULL;
    }
    if (feof(stream) || n > SEQUEL_INPUT_MAX)
      break;
  } while (1);
  buffer[n] = 0;
  *length = n;
  return buffer;
}
