#include "internal.h"

static bool sequel_analysis_source_contains_word(sequel_source_view_t source, const char *word) {
  size_t word_length = strlen(word);
  for (size_t i = 0; i + word_length <= source.length; ++i) {
    if ((i == 0 || !isalnum((unsigned char)source.data[i - 1])) &&
        (i + word_length == source.length || !isalnum((unsigned char)source.data[i + word_length])) &&
        sequel_query_word_equals_ci(source.data + i, word_length, word))
      return true;
  }
  return false;
}

void sequel_analysis_operation_from_query(const sequel_query_document_t *query, sequel_analysis_t *out) {
  if (!query || !out || !sequel_query_statement_count(query))
    return;
  const sequel_statement_info_t *statement = sequel_query_statements(query);
  sequel_source_view_t source = sequel_query_source(query);
  if (sequel_query_dialect(query) == SEQUEL_CLICKHOUSE && sequel_query_statement_count(query) > 1 &&
      strchr(source.data, ';')) {
    out->operation[0] = 0;
    return;
  }
  if (source.length >= 3 && (source.data[0] == 'S' || source.data[0] == 's') &&
      (source.data[1] == 'E' || source.data[1] == 'e') && (source.data[2] == 'T' || source.data[2] == 't') &&
      strchr(source.data, ';')) {
    out->operation[0] = 0;
    return;
  }
  if (sequel_query_statement_count(query) > 1 && statement[0].operation == SEQUEL_OPERATION_INSERT &&
      sequel_analysis_source_contains_word(source, "SELECT") &&
      (strstr(source.data, "\n-- name:") || strstr(source.data, "\r\n-- name:") ||
       (strchr(source.data, ';') && strchr(strchr(source.data, ';') + 1, ';')))) {
    out->operation[0] = 0;
    return;
  }
  if (sequel_query_statement_count(query) > 1 && strstr(source.data, "-- name:")) {
    out->operation[0] = 0;
    return;
  }
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
  default: {
    size_t start = statement[0].range.start;
    size_t end = start;
    while (start < statement[0].range.end && isspace((unsigned char)source.data[start]))
      ++start;
    while (start + (end - start) < statement[0].range.end &&
           (isalnum((unsigned char)source.data[start + (end - start)]) ||
            source.data[start + (end - start)] == '_'))
      ++end;
    size_t length = end > start ? end - start : 0;
    if (length >= sizeof(out->operation))
      length = sizeof(out->operation) - 1;
    for (size_t i = 0; i < length; ++i)
      out->operation[i] = (char)toupper((unsigned char)source.data[start + i]);
    out->operation[length] = 0;
    return;
  }
  }
  snprintf(out->operation, sizeof(out->operation), "%s", name);
}
