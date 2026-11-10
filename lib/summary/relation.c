#include "internal.h"

#include <string.h>

#define append_text sequel_summary_append_text
#define append_bytes sequel_summary_append_bytes
#define summary_word sequel_summary_word

int sequel_summary_append_relation_name(char **text, size_t *length, size_t *capacity,
                                        sequel_source_view_t source, const sequel_relation_info_t *relation) {
  size_t start = relation->name_range.start;
  size_t end = relation->name_range.end;
  if (start > end || end > source.length || !append_bytes(text, length, capacity, source.data + start, end - start))
    return 0;
  if (end < source.length && source.data[end] == '(') {
    const char *close = strchr(source.data + end, ')');
    if (close && (!append_text(text, length, capacity, "(…)") ))
      return 0;
  }
  if (relation->alias_range.end > relation->alias_range.start) {
    size_t alias_start = relation->alias_range.start;
    size_t alias_end = relation->alias_range.end;
    if (alias_end > source.length || alias_start > alias_end)
      return 0;
    if (summary_word(source.data, source.length, alias_start, "SAMPLE"))
      return append_text(text, length, capacity, " SAMPLE");
    if (!append_text(text, length, capacity, " AS ") ||
        !append_bytes(text, length, capacity, source.data + alias_start, alias_end - alias_start))
      return 0;
  }
  return 1;
}
