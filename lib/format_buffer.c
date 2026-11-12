#include <stdlib.h>
#include <string.h>

#include <stdint.h>

#include "format_buffer.h"

int sequel_format_append_text(char **data, size_t *length, size_t *capacity, const char *text, size_t amount) {
  if (amount > SIZE_MAX - *length - 1)
    return 0;
  size_t required = *length + amount + 1;
  if (required > *capacity) {
    size_t next = *capacity ? *capacity : 64;
    while (next < required) {
      if (next > SIZE_MAX / 2)
        return 0;
      next *= 2;
    }
    char *grown = realloc(*data, next);
    if (!grown)
      return 0;
    *data = grown;
    *capacity = next;
  }
  memcpy(*data + *length, text, amount);
  *length += amount;
  (*data)[*length] = 0;
  return 1;
}

int sequel_format_append_space(char **data, size_t *length, size_t *capacity) {
  return !*length || (*data)[*length - 1] == ' ' || (*data)[*length - 1] == '\n' ||
         sequel_format_append_text(data, length, capacity, " ", 1);
}

int sequel_format_append_line_start(char **data, size_t *length, size_t *capacity, size_t indent) {
  while (*length && (*data)[*length - 1] == ' ')
    --*length;
  if (*length && (*data)[*length - 1] != '\n' && !sequel_format_append_text(data, length, capacity, "\n", 1))
    return 0;
  for (size_t i = 0; i < indent; ++i)
    if (!sequel_format_append_text(data, length, capacity, "  ", 2))
      return 0;
  return 1;
}
