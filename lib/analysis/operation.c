#include "analysis_internal.h"

void analysis_operation_from_query(const sequel_query_document_t *query, sequel_analysis_t *out) {
  if (!query || !out || !sequel_query_statement_count(query))
    return;
  const sequel_statement_info_t *statement = sequel_query_statements(query);
  const char *name = NULL;
  switch (statement[0].operation) {
  case SEQUEL_OPERATION_SELECT:
    name = "SELECT";
    break;
  case SEQUEL_OPERATION_INSERT:
    name = "INSERT";
    break;
  case SEQUEL_OPERATION_UPDATE:
    name = "UPDATE";
    break;
  case SEQUEL_OPERATION_DELETE:
    name = "DELETE";
    break;
  case SEQUEL_OPERATION_CREATE:
    name = "CREATE";
    break;
  case SEQUEL_OPERATION_ALTER:
    name = "ALTER";
    break;
  case SEQUEL_OPERATION_DROP:
    name = "DROP";
    break;
  case SEQUEL_OPERATION_EXPLAIN:
    name = "EXPLAIN";
    break;
  default:
    return;
  }
  snprintf(out->operation, sizeof(out->operation), "%s", name);
}
