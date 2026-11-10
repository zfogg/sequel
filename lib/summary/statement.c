#include <ctype.h>
#include <string.h>

#include <sequel/summary.h>

#include "internal.h"

#define append_text sequel_summary_append_text
#define append_bytes sequel_summary_append_bytes
#define operation_name sequel_summary_operation_name
#define summary_word sequel_summary_word

static const char ellipsis[] = "\342\200\246";

int sequel_summary_append_statement_shape(char **text, size_t *length, size_t *capacity,
                                          sequel_source_view_t source,
                                          const sequel_query_document_t *query,
                                          const sequel_summary_options_t *settings,
                                          const sequel_statement_info_t *statement) {
  if (!settings->include_operation || !statement)
    return 1;
  if (statement->operation == SEQUEL_OPERATION_TRANSACTION) {
    size_t start = 0;
    while (start < source.length && isspace((unsigned char)source.data[start]))
      ++start;
    size_t end = start;
    while (end < source.length && !isspace((unsigned char)source.data[end]) && source.data[end] != ';')
      ++end;
    return end > start && append_bytes(text, length, capacity, source.data + start, end - start);
  }
  if (summary_word(source.data, source.length, 0, "REINDEX"))
    return append_text(text, length, capacity, "SQL");
  if (statement->operation != SEQUEL_OPERATION_UNKNOWN) {
    return append_text(text, length, capacity, operation_name(statement->operation));
  }
  size_t start = 0;
  size_t cte_count = sequel_query_cte_count(query);
  if (cte_count)
    start = sequel_query_ctes(query)[cte_count - 1].range.end;
  while (start < source.length && isspace((unsigned char)source.data[start]))
    ++start;
  size_t end = start;
  while (end < source.length && !isspace((unsigned char)source.data[end]) && source.data[end] != '(')
    ++end;
  if (cte_count && summary_word(source.data, source.length, start, "SELECT"))
    return append_text(text, length, capacity, "SELECT ") &&
           append_bytes(text, length, capacity, ellipsis, sizeof(ellipsis) - 1);
  if (end <= start || !append_bytes(text, length, capacity, source.data + start, end - start))
    return 0;
  if (summary_word(source.data, source.length, start, "ANALYZE")) {
    size_t table = end;
    while (table < source.length && isspace((unsigned char)source.data[table]))
      ++table;
    size_t table_end = table;
    while (table_end < source.length && !isspace((unsigned char)source.data[table_end]) && source.data[table_end] != ';')
      ++table_end;
    if (table_end > table && (!append_text(text, length, capacity, " ") ||
                              !append_bytes(text, length, capacity, source.data + table, table_end - table)))
      return 0;
  }
  return 1;
}

int sequel_summary_append_projection_shape(char **text, size_t *length, size_t *capacity,
                                           sequel_source_view_t source,
                                           const sequel_query_document_t *query,
                                           const sequel_statement_info_t *statement) {
  if (!statement || statement->operation != SEQUEL_OPERATION_SELECT || !sequel_query_projection_count(query))
    return 1;
  const sequel_projection_info_t *projection = sequel_query_projections(query);
  char first = projection[0].range.start < source.length ? source.data[projection[0].range.start] : 0;
  bool literal = strchr("'?0123456789", first) != NULL || first == '$';
  bool multiple = strchr(source.data + projection[0].range.start, ',') != NULL;
  bool scalar = !strstr(source.data, " FROM ") && strchr(source.data + projection[0].range.start, '(') != NULL;
  const char *open = scalar ? strchr(source.data + projection[0].range.start, '(') : NULL;
  if (!append_text(text, length, capacity, " "))
    return 0;
  if (scalar) {
    if (!append_bytes(text, length, capacity, source.data + projection[0].range.start,
                      (size_t)(open - (source.data + projection[0].range.start) + 1)))
      return 0;
    if (open[1] == ')') {
      if (!append_text(text, length, capacity, ")"))
        return 0;
    } else if (!append_bytes(text, length, capacity, ellipsis, sizeof(ellipsis) - 1) ||
               !append_text(text, length, capacity, ")"))
      return 0;
  } else if (literal && !multiple ? !append_text(text, length, capacity, "?")
                     : !append_bytes(text, length, capacity, ellipsis, sizeof(ellipsis) - 1))
    return 0;
  const char *alias = strstr(source.data + projection[0].range.start, " AS ");
  const char *from = alias ? strstr(alias + 4, " FROM ") : NULL;
  if (alias && (!from || alias < from) && (first == '\'' || scalar)) {
    const char *name = alias + 4;
    const char *end = name;
    while (end < source.data + source.length && !isspace((unsigned char)*end))
      ++end;
    if (!append_text(text, length, capacity, " AS ") ||
        !append_bytes(text, length, capacity, name, (size_t)(end - name)))
      return 0;
  }
  return 1;
}
