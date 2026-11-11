#include <stddef.h>

#include "internal.h"

static const char summary_ellipsis[] = "\342\200\246";

int sequel_summary_append_parameter_only(char **text, size_t *length, size_t *capacity,
                                         const sequel_query_document_t *query,
                                         const sequel_statement_info_t *statement) {
  if (!statement || statement->operation != SEQUEL_OPERATION_SELECT || sequel_query_relation_count(query) != 0 ||
      sequel_query_parameter_count(query) == 0 || sequel_summary_starts_function_projection(query))
    return 0;
  const char *projection = sequel_query_parameter_count(query) == 1 ? "?" : summary_ellipsis;
  size_t projection_length = sequel_query_parameter_count(query) == 1 ? 1 : sizeof(summary_ellipsis) - 1;
  return sequel_summary_append_text(text, length, capacity, "SELECT ") &&
                 sequel_summary_append_bytes(text, length, capacity, projection, projection_length)
             ? 1
             : -1;
}
