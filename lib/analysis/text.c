/* Normalize list punctuation after the shared sanitizer has emitted text. */
#include "internal.h"

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
