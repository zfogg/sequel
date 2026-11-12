/* Normalize list punctuation after the shared sanitizer has emitted text. */
#include "internal.h"

void sequel_analysis_append_text(analysis_text *text, const char *data, size_t length) {
  sequel_work_tick(length);
  if (length > SIZE_MAX - text->n - 1)
    sequel_abort(SEQUEL_ERR_LIMIT);
  size_t needed = text->n + length + 1;
  if (needed > text->capacity) {
    size_t next = text->capacity ? text->capacity : 32;
    while (next < needed) {
      if (next > SIZE_MAX / 2) {
        next = needed;
        break;
      }
      next *= 2;
    }
    text->p = realloc(text->p, next);
    text->capacity = next;
  }
  memcpy(text->p + text->n, data, length);
  text->n += length;
  text->p[text->n] = 0;
}

void sequel_analysis_utf8_cut(char *text, size_t limit) {
  size_t length = strlen(text);
  if (length <= limit)
    return;
  limit = sequel_utf8_prefix(text, length, limit);
  text[limit] = 0;
  while (limit && isspace((unsigned char)text[limit - 1]))
    text[--limit] = 0;
}

void sequel_analysis_compact_list_text(analysis_text *text) {
  size_t read = 0, write = 0;
  while (read < text->n) {
    char c = text->p[read++];
    if (c == '(' || c == ',') {
      text->p[write++] = c;
      while (read < text->n && text->p[read] == ' ')
        ++read;
      if (c == '(' && read + 1 < text->n && text->p[read] == '?' && text->p[read + 1] == '?')
        ++read;
      continue;
    }
    if (c == ' ' && read < text->n && (text->p[read] == ',' || text->p[read] == ')'))
      continue;
    text->p[write++] = c;
  }
  text->n = write;
  text->p[write] = 0;
}
