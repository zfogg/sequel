#include "internal.h"

#include <ctype.h>
#include <string.h>

static const char ellipsis[] = "\342\200\246";

int sequel_summary_append_parenthesized_subquery(char **text, size_t *length, size_t *capacity,
                                                 sequel_source_view_t source) {
  size_t start = 0;
  while (start < source.length && isspace((unsigned char)source.data[start]))
    ++start;
  if (start >= source.length || source.data[start] != '(')
    return 1;
  const char *select = strstr(source.data + start, "SELECT ");
  const char *from = select ? strstr(select, " FROM ") : NULL;
  const char *close = from ? strchr(from, ')') : NULL;
  if (!select || !from || !close)
    return 1;
  const char *table = from + 6;
  while (table < close && isspace((unsigned char)*table))
    ++table;
  const char *table_end = table;
  while (table_end < close && !isspace((unsigned char)*table_end) && *table_end != ')')
    ++table_end;
  const char *where = strstr(table_end, " WHERE ");
  const char *limit = strstr(table_end, " LIMIT ");
  if (where && where > close)
    where = NULL;
  if (limit && limit > close)
    limit = NULL;
  if (!sequel_summary_append_text(text, length, capacity, "(SELECT ") ||
      !sequel_summary_append_bytes(text, length, capacity, ellipsis, sizeof(ellipsis) - 1) ||
      !sequel_summary_append_text(text, length, capacity, " FROM ") ||
      !sequel_summary_append_bytes(text, length, capacity, table, (size_t)(table_end - table)) ||
      (where && (!sequel_summary_append_text(text, length, capacity, " WHERE ") ||
                 !sequel_summary_append_bytes(text, length, capacity, ellipsis, sizeof(ellipsis) - 1))) ||
      (limit && (!sequel_summary_append_text(text, length, capacity, " LIMIT ") ||
                 !sequel_summary_append_text(text, length, capacity, limit[7] == '$' ? "?" : "1"))) ||
      !sequel_summary_append_text(text, length, capacity, ")"))
    return 0;
  const char *outer_limit = strstr(close + 1, " LIMIT ");
  if (outer_limit && (!sequel_summary_append_text(text, length, capacity, " LIMIT ") ||
                      !sequel_summary_append_text(text, length, capacity, outer_limit[7] == '$' ? "?" : "1")))
    return 0;
  return 2;
}
