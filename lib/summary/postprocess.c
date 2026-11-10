#include <string.h>

#include "internal.h"

void sequel_summary_normalize_projection(char *text, size_t *length) {
  if (!text || !length)
    return;
  char *projection = strstr(text, "SELECT …, ");
  if (!projection)
    return;
  char *from = strstr(projection + strlen("SELECT …, "), " FROM ");
  if (!from)
    return;
  const char replacement[] = "SELECT …";
  size_t old_length = (size_t)(from - projection);
  size_t new_length = sizeof(replacement) - 1;
  memmove(projection + new_length, from, strlen(from) + 1);
  memcpy(projection, replacement, new_length);
  *length -= old_length - new_length;
}
