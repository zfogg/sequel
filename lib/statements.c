#include <ctype.h>
#include <string.h>

#include <sequel/query.h>

#include "model.h"

static int is_trivia(const sequel_token_t *token) {
  return token->kind == SEQUEL_TOKEN_WHITESPACE || token->kind == SEQUEL_TOKEN_LINE_COMMENT ||
         token->kind == SEQUEL_TOKEN_BLOCK_COMMENT;
}

sequel_status_t sequel_query_build_statements(sequel_query_document_t *document, const sequel_syntax_document_t *syntax,
                                              const sequel_token_t *tokens, size_t token_count,
                                              const sequel_source_view_t *source, size_t max_nodes) {
  size_t node_count = 0;
  const sequel_syntax_node_t *nodes = sequel_syntax_nodes(syntax, &node_count);

  for (size_t i = 0; i < node_count && document->statement_count < max_nodes; ++i) {
    sequel_syntax_kind_t kind = nodes[i].kind;
    if (kind < SEQUEL_SYNTAX_SELECT_STATEMENT || kind > SEQUEL_SYNTAX_UNKNOWN_STATEMENT)
      continue;
    sequel_statement_info_t statement = {.range = nodes[i].range,
                                         .operation = sequel_query_operation_from_syntax(kind),
                                         .syntax_node = i,
                                         .query_body = SIZE_MAX};
    if (!sequel_query_array_append((void **)&document->statements, &document->statement_count, sizeof(statement),
                                   &statement))
      return SEQUEL_ERR_NOMEM;
    if (statement.operation == SEQUEL_OPERATION_UNKNOWN) {
      size_t first = nodes[i].first_token;
      while (first < token_count && (is_trivia(&tokens[first]) || (tokens[first].kind == SEQUEL_TOKEN_PUNCTUATION &&
                                                                   source->data[tokens[first].range.start] == '(')))
        ++first;
      if (first >= token_count)
        continue;
      const sequel_token_t *first_token = &tokens[first];
      document->statements[document->statement_count - 1].operation = sequel_query_operation_from_word(
          source->data + first_token->range.start, first_token->range.end - first_token->range.start);
      if (document->statements[document->statement_count - 1].operation == SEQUEL_OPERATION_UNKNOWN &&
          first_token->kind == SEQUEL_TOKEN_KEYWORD && first_token->keyword_id == SEQUEL_KEYWORD_WITH) {
        for (size_t candidate = first + 1; candidate < nodes[i].first_token + nodes[i].token_count; ++candidate) {
          if (is_trivia(&tokens[candidate]))
            continue;
          sequel_operation_t recovered =
              sequel_query_operation_from_word(source->data + tokens[candidate].range.start,
                                               tokens[candidate].range.end - tokens[candidate].range.start);
          if (recovered != SEQUEL_OPERATION_UNKNOWN) {
            document->statements[document->statement_count - 1].operation = recovered;
            break;
          }
        }
      }
    }
  }

  if (!document->statement_count && token_count) {
    for (size_t token = 0; token < token_count; ++token) {
      if (is_trivia(&tokens[token]))
        continue;
      sequel_operation_t recovered = sequel_query_operation_from_word(
          source->data + tokens[token].range.start, tokens[token].range.end - tokens[token].range.start);
      if (recovered == SEQUEL_OPERATION_UNKNOWN)
        continue;
      sequel_statement_info_t statement = {
          .range = {0, source->length}, .operation = recovered, .syntax_node = SIZE_MAX, .query_body = SIZE_MAX};
      if (!sequel_query_array_append((void **)&document->statements, &document->statement_count, sizeof(statement),
                                     &statement))
        return SEQUEL_ERR_NOMEM;
      break;
    }
  }
  return SEQUEL_OK;
}

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
