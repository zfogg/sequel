#include <ctype.h>
#include <string.h>

#include "internal.h"

int sequel_summary_append_relation_name(char **text, size_t *length, size_t *capacity, sequel_source_view_t source,
                                        const sequel_relation_info_t *relation) {
  size_t start = relation->name_range.start;
  size_t end = relation->name_range.end;
  if (start > end || end > source.length ||
      !sequel_summary_append_bytes(text, length, capacity, source.data + start, end - start))
    return 0;
  if (relation->kind == SEQUEL_RELATION_SUBQUERY && start < end && source.data[start] == '(') {
    char *rendered = *text;
    char *open = rendered + *length - (end - start);
    char *select = sequel_summary_find_text(open, "SELECT ");
    char *from = select ? sequel_summary_find_text(select, " FROM ") : NULL;
    char *close = strrchr(open, ')');
    if (select && from && close && close < rendered + *length) {
      size_t offset = (size_t)(open - rendered);
      *length = offset;
      rendered[offset] = 0;
      if (!sequel_summary_append_text(text, length, capacity, "(SELECT … FROM "))
        return 0;
      const char *table = from + 6;
      while (table < close && isspace((unsigned char)*table))
        ++table;
      const char *table_end = table;
      while (table_end < close && !isspace((unsigned char)*table_end) && *table_end != ')')
        ++table_end;
      if (!sequel_summary_append_bytes(text, length, capacity, table, (size_t)(table_end - table)))
        return 0;
      const char *where = sequel_summary_find_text(table_end, " WHERE ");
      const char *order = sequel_summary_find_text(table_end, " ORDER BY ");
      const char *limit = sequel_summary_find_text(table_end, " LIMIT ");
      if (where && where < close && (!sequel_summary_append_text(text, length, capacity, " WHERE …")))
        return 0;
      if (order && order < close && (!sequel_summary_append_text(text, length, capacity, " ORDER BY …")))
        return 0;
      if (limit && limit < close && (!sequel_summary_append_text(text, length, capacity, " LIMIT ?")))
        return 0;
      if (!sequel_summary_append_text(text, length, capacity, ")"))
        return 0;
    }
  }
  if (end < source.length && source.data[end] == '(') {
    const char *close = strchr(source.data + end, ')');
    if (close && (!sequel_summary_append_text(text, length, capacity, "(…)")))
      return 0;
  }
  if (relation->alias_range.end > relation->alias_range.start) {
    size_t alias_start = relation->alias_range.start;
    size_t alias_end = relation->alias_range.end;
    if (alias_end > source.length || alias_start > alias_end)
      return 0;
    if (sequel_summary_word(source.data, source.length, alias_start, "SAMPLE"))
      return sequel_summary_append_text(text, length, capacity, " SAMPLE");
    if (!sequel_summary_append_text(text, length, capacity, " AS ") ||
        !sequel_summary_append_bytes(text, length, capacity, source.data + alias_start, alias_end - alias_start))
      return 0;
  }
  return 1;
}
