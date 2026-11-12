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
      const char *inner_alias = sequel_summary_find_text(table_end, " AS ");
      if (inner_alias && inner_alias < close && (!where || inner_alias < where) && (!order || inner_alias < order)) {
        const char *alias = inner_alias + strlen(" AS ");
        const char *alias_end = alias;
        while (alias_end < close && !isspace((unsigned char)*alias_end) && *alias_end != ')')
          ++alias_end;
        if (!sequel_summary_append_text(text, length, capacity, " AS ") ||
            !sequel_summary_append_bytes(text, length, capacity, alias, (size_t)(alias_end - alias)))
          return 0;
      }
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
  if (relation->kind == SEQUEL_RELATION_FUNCTION && relation->alias_range.start > end) {
    const char *suffix = source.data + end;
    size_t suffix_length = relation->alias_range.start - end;
    if (sequel_summary_find_word(suffix, suffix_length, 0, "WITH") &&
        sequel_summary_find_word(suffix, suffix_length, 0, "ORDINALITY") &&
        !sequel_summary_append_text(text, length, capacity, " WITH ORDINALITY"))
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
    if (relation->kind == SEQUEL_RELATION_FUNCTION && relation->range.end > alias_end &&
        source.data[alias_end] == '(') {
      const char *column_end = sequel_summary_matching_close(source.data + alias_end, source.data + source.length);
      if (!column_end || !sequel_summary_append_bytes(text, length, capacity, source.data + alias_end,
                                                      (size_t)(column_end - (source.data + alias_end)) + 1))
        return 0;
    }
  }
  return 1;
}
