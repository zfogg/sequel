#include "internal.h"

#include <sequel/query.h>

const char *sequel_summary_operation_name(sequel_operation_t operation) {
  switch (operation) {
  case SEQUEL_OPERATION_SELECT: return "SELECT";
  case SEQUEL_OPERATION_INSERT: return "INSERT";
  case SEQUEL_OPERATION_UPDATE: return "UPDATE";
  case SEQUEL_OPERATION_DELETE: return "DELETE";
  case SEQUEL_OPERATION_MERGE: return "MERGE";
  case SEQUEL_OPERATION_CREATE: return "CREATE";
  case SEQUEL_OPERATION_ALTER: return "ALTER";
  case SEQUEL_OPERATION_DROP: return "DROP";
  case SEQUEL_OPERATION_TRANSACTION: return "BEGIN";
  case SEQUEL_OPERATION_EXPLAIN: return "EXPLAIN";
  case SEQUEL_OPERATION_TRUNCATE: return "TRUNCATE";
  default: return "SQL";
  }
}
