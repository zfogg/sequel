#include <string.h>

#include "internal.h"

static void normalize_function_arguments(char *text, size_t *length) {
  for (char *function = text; function;) {
    function = strstr(function, "argMax(");
    if (!function)
      return;
    char *close = strchr(function + strlen("argMax("), ')');
    if (!close) {
      function += strlen("argMax(");
      continue;
    }
    const char replacement[] = "argMax(…)";
    size_t old_length = (size_t)(close + 1 - function);
    size_t new_length = sizeof(replacement) - 1;
    memmove(function + new_length, close + 1, strlen(close + 1) + 1);
    memcpy(function, replacement, new_length);
    *length -= old_length - new_length;
    function += new_length;
  }
}

void sequel_summary_normalize_projection(char *text, size_t *length) {
  if (!text || !length)
    return;
  normalize_function_arguments(text, length);
  char *projection = strstr(text, "SELECT ");
  if (!projection)
    return;
  char *from = strstr(projection + strlen("SELECT "), "FROM ");
  char *projection_start = projection + strlen("SELECT ");
  char *comma = strchr(projection_start, ',');
  char *open = strchr(projection_start, '(');
  if (!from || ((!comma || comma > from) && (!open || open > from)))
    return;
  while (from > projection && (from[-1] == ' ' || from[-1] == '\t' || from[-1] == '\r' || from[-1] == '\n'))
    --from;
  const char replacement[] = "SELECT …";
  size_t old_length = (size_t)(from - projection);
  size_t new_length = sizeof(replacement) - 1;
  memmove(projection + new_length, from, strlen(from) + 1);
  memcpy(projection, replacement, new_length);
  *length -= old_length - new_length;
}
