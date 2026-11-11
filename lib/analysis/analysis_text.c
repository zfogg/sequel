/* Normalize list punctuation after the shared sanitizer has emitted text. */
#include "analysis_internal.h"

void analysis_compact_list_text(analysis_text *text) {
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
