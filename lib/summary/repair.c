#include <stddef.h>
#include <string.h>

#include "internal.h"

int sequel_summary_append_missing_keyword(char **text, size_t *length, size_t *capacity, sequel_source_view_t source,
                                          const char *keyword, const char *replacement) {
  if (!text || !*text || !length || !capacity || !source.data || !keyword || !replacement)
    return 0;
  size_t source_count = 0;
  size_t rendered_count = 0;
  size_t keyword_length = strlen(keyword);
  for (size_t cursor = 0; cursor < source.length;) {
    const char *match = sequel_summary_find_word(source.data, source.length, cursor, keyword);
    if (!match)
      break;
    ++source_count;
    cursor = (size_t)(match - source.data) + keyword_length;
  }
  for (size_t cursor = 0; cursor < *length;) {
    const char *match = sequel_summary_find_word(*text, *length, cursor, keyword);
    if (!match)
      break;
    ++rendered_count;
    cursor = (size_t)(match - *text) + keyword_length;
  }
  while (rendered_count < source_count) {
    if (!sequel_summary_append_text(text, length, capacity, replacement))
      return 0;
    ++rendered_count;
  }
  return 1;
}
