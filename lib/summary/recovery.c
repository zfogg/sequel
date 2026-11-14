#include <ctype.h>
#include <string.h>

#include "internal.h"

int sequel_summary_append_dialect_recovery(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, const sequel_summary_options_t *settings, sequel_source_view_t source,
                                           size_t statement_count, const sequel_statement_info_t *statements) {
  if (sequel_lexed_dialect(sequel_syntax_lexed(sequel_query_syntax(query))) != SEQUEL_CLICKHOUSE || !sequel_summary_find_text(source.data, "$"))
    return 0;

  const char *operation = statement_count ? sequel_summary_operation_name(statements[0].operation) : "SQL";
  const char *from = sequel_summary_find_word(source.data, source.length, 0, "FROM");
  const char *table = from ? from + 4 : NULL;
  while (table && (size_t)(table - source.data) < source.length && isspace((unsigned char)*table))
    ++table;
  const char *table_end = table;
  while (table_end && (size_t)(table_end - source.data) < source.length && !isspace((unsigned char)*table_end) && *table_end != ';')
    ++table_end;

  if (!settings->include_operation || !settings->include_tables || !statement_count || !sequel_summary_word(source.data, source.length, 0, "SELECT") || !from)
    return !sequel_summary_append_text(text, length, capacity, operation) || !sequel_summary_append_text(text, length, capacity, " … FROM ") ||
                   !sequel_summary_append_bytes(text, length, capacity, table ? table : "…", table ? (size_t)(table_end - table) : 3)
               ? -1
               : 1;

  bool distinct_outer = sequel_query_outer_distinct_on(query);
  if (!sequel_summary_append_text(text, length, capacity, distinct_outer ? "SELECT DISTINCT ON (…) " : "SELECT ") ||
      !sequel_summary_append_bytes(text, length, capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) || !sequel_summary_append_text(text, length, capacity, " FROM ") ||
      !sequel_summary_append_bytes(text, length, capacity, table, (size_t)(table_end - table)))
    return -1;
  return 2;
}
