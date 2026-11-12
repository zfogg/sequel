#include "internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int sequel_summary_append_text(char **text, size_t *length, size_t *capacity, const char *value) {
  return sequel_summary_append_bytes(text, length, capacity, value, strlen(value));
}

int sequel_summary_append_bytes(char **text, size_t *length, size_t *capacity, const char *value, size_t amount) {
  /* Keep one byte beyond the terminator as a sentinel for legacy text
   * consumers that search the assembled buffer while it is being repaired. */
  if (amount > SIZE_MAX - *length - 2)
    return 0;
  size_t required = *length + amount + 2;
  if (required > *capacity) {
    size_t next = *capacity ? *capacity : 64;
    while (next < required) {
      if (next > SIZE_MAX / 2)
        return 0;
      next *= 2;
    }
    char *grown = realloc(*text, next);
    if (!grown)
      return 0;
    *text = grown;
    *capacity = next;
    (*text)[next - 1] = 0;
  }
  memcpy(*text + *length, value, amount);
  *length += amount;
  (*text)[*length] = 0;
  return 1;
}
