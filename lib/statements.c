#include <ctype.h>
#include <string.h>

#include <sequel/query.h>

sequel_operation_t sequel_query_operation_from_syntax(sequel_syntax_kind_t kind) {
  switch (kind) {
  case SEQUEL_SYNTAX_SELECT_STATEMENT:
    return SEQUEL_OPERATION_SELECT;
  case SEQUEL_SYNTAX_INSERT_STATEMENT:
    return SEQUEL_OPERATION_INSERT;
  case SEQUEL_SYNTAX_UPDATE_STATEMENT:
    return SEQUEL_OPERATION_UPDATE;
  case SEQUEL_SYNTAX_DELETE_STATEMENT:
    return SEQUEL_OPERATION_DELETE;
  case SEQUEL_SYNTAX_MERGE_STATEMENT:
    return SEQUEL_OPERATION_MERGE;
  case SEQUEL_SYNTAX_CREATE_STATEMENT:
    return SEQUEL_OPERATION_CREATE;
  case SEQUEL_SYNTAX_ALTER_STATEMENT:
    return SEQUEL_OPERATION_ALTER;
  case SEQUEL_SYNTAX_DROP_STATEMENT:
    return SEQUEL_OPERATION_DROP;
  case SEQUEL_SYNTAX_TRANSACTION_STATEMENT:
    return SEQUEL_OPERATION_TRANSACTION;
  case SEQUEL_SYNTAX_EXPLAIN_STATEMENT:
    return SEQUEL_OPERATION_EXPLAIN;
  default:
    return SEQUEL_OPERATION_UNKNOWN;
  }
}

bool sequel_query_word_equals_ci(const char *word, size_t length, const char *expected) {
  size_t expected_length = strlen(expected);
  if (length != expected_length)
    return false;
  for (size_t i = 0; i < length; ++i)
    if (toupper((unsigned char)word[i]) != (unsigned char)expected[i])
      return false;
  return true;
}

sequel_operation_t sequel_query_operation_from_word(const char *word, size_t length) {
  if (sequel_query_word_equals_ci(word, length, "SELECT"))
    return SEQUEL_OPERATION_SELECT;
  if (sequel_query_word_equals_ci(word, length, "INSERT"))
    return SEQUEL_OPERATION_INSERT;
  if (sequel_query_word_equals_ci(word, length, "UPDATE"))
    return SEQUEL_OPERATION_UPDATE;
  if (sequel_query_word_equals_ci(word, length, "DELETE"))
    return SEQUEL_OPERATION_DELETE;
  if (sequel_query_word_equals_ci(word, length, "MERGE"))
    return SEQUEL_OPERATION_MERGE;
  if (sequel_query_word_equals_ci(word, length, "OPTIMIZE"))
    return SEQUEL_OPERATION_OPTIMIZE;
  if (sequel_query_word_equals_ci(word, length, "TRUNCATE"))
    return SEQUEL_OPERATION_TRUNCATE;
  return SEQUEL_OPERATION_UNKNOWN;
}

size_t sequel_query_statement_for_range(const sequel_query_document_t *document, sequel_source_range_t range) {
  for (size_t i = 0; i < document->statement_count; ++i)
    if (sequel_source_range_contains(document->statements[i].range, range))
      return i;
  return SIZE_MAX;
}
