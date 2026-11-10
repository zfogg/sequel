#include "internal.h"

#include <ctype.h>
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
  if (relation->kind == SEQUEL_RELATION_SUBQUERY && start < end && source.data[start] == '(') {
    char *rendered = *text;
    char *open = rendered + *length - (end - start);
    char *select = strstr(open, "SELECT ");
    char *from = select ? strstr(select, " FROM ") : NULL;
    char *close = strrchr(open, ')');
    if (select && from && close && close < rendered + *length) {
      size_t offset = (size_t)(open - rendered);
      *length = offset;
      rendered[offset] = 0;
      if (!append_text(text, length, capacity, "(SELECT … FROM "))
        return 0;
      const char *table = from + 6;
      while (table < close && isspace((unsigned char)*table))
        ++table;
      const char *table_end = table;
      while (table_end < close && !isspace((unsigned char)*table_end) && *table_end != ')')
        ++table_end;
      if (!append_bytes(text, length, capacity, table, (size_t)(table_end - table)))
        return 0;
      const char *where = strstr(table_end, " WHERE ");
      const char *order = strstr(table_end, " ORDER BY ");
      const char *limit = strstr(table_end, " LIMIT ");
      if (where && where < close && (!append_text(text, length, capacity, " WHERE …")))
        return 0;
      if (order && order < close && (!append_text(text, length, capacity, " ORDER BY …")))
        return 0;
      if (limit && limit < close && (!append_text(text, length, capacity, " LIMIT ?")))
        return 0;
      if (!append_text(text, length, capacity, ")"))
        return 0;
    }
  }
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
