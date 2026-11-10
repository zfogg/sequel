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
  return end > start && append_bytes(text, length, capacity, source.data + start, end - start);
}
