#include <string.h>
#include <stdlib.h>

#include <sequel.h>
#include <sequel/formatter.h>
#include <sequel/limits.h>
#include <sequel/parser.h>

#include "util/utf8.h"

static sequel_status_t expand_deep_parentheses(sequel_text_result_t *result) {
  size_t depth = 0, maximum = 0;
  for (size_t i = 0; i < result->length; ++i) {
    if (result->data[i] == '(')
      maximum = ++depth > maximum ? depth : maximum;
    else if (result->data[i] == ')' && depth)
      --depth;
  }
  if (maximum <= 16)
    return SEQUEL_OK;
  if (maximum > SIZE_MAX / maximum || maximum * maximum > (SIZE_MAX - result->length - 1 - maximum * 2) / 8)
    return SEQUEL_ERR_OVERFLOW;
  size_t capacity = result->length + maximum * maximum * 8 + maximum * 2 + 1;
  char *expanded = malloc(capacity);
  if (!expanded)
    return SEQUEL_ERR_NOMEM;
  size_t length = 0;
  depth = 0;
  for (size_t i = 0; i < result->length; ++i) {
    char character = result->data[i];
    if (character == '(') {
      expanded[length++] = '\n';
      ++depth;
      for (size_t spaces = 0; spaces < depth * 8; ++spaces)
        expanded[length++] = ' ';
    } else if (character == ')') {
      expanded[length++] = '\n';
      if (depth)
        --depth;
      for (size_t spaces = 0; spaces < depth * 8; ++spaces)
        expanded[length++] = ' ';
    }
    expanded[length++] = character;
  }
  expanded[length] = 0;
  sequel_text_result_free(result);
  *result = (sequel_text_result_t){expanded, length, capacity, SEQUEL_FORMAT_SYNTAX_AWARE};
  return SEQUEL_OK;
}

sequel_status_t sequel_format(const char *sql, sequel_dialect_t dialect, char *out, size_t capacity, size_t *length) {
  if (out && capacity)
    *out = 0;
  if (!sql || !length)
    return SEQUEL_ERR_NULL;
  *length = 0;
  if (dialect < SEQUEL_GENERIC || dialect > SEQUEL_CLICKHOUSE || (!out && capacity) || (out && !capacity))
    return SEQUEL_ERR_INVALID_ARGUMENT;

  size_t source_length = 0;
  while (source_length <= SEQUEL_INPUT_MAX && sql[source_length])
    ++source_length;
  if (source_length > SEQUEL_INPUT_MAX)
    return SEQUEL_ERR_OVERFLOW;
  if (!sequel_utf8_valid(sql, source_length))
    return SEQUEL_ERR_INVALID_ARGUMENT;

  sequel_parse_options_t options;
  sequel_parse_options_init(&options);
  options.dialect = dialect;
  options.retain_comments = true;
  options.retain_whitespace = false;
  sequel_source_view_t source = {sql, source_length};
  sequel_lexed_document_t *lexed = NULL;
  sequel_status_t status = sequel_lex(&source, &options, NULL, &lexed);
  if (status != SEQUEL_OK)
    return status;
  if (sequel_lexed_diagnostic_count(lexed)) {
    sequel_lexed_document_free(lexed);
    return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  sequel_syntax_document_t *syntax = NULL;
  status = sequel_parse(lexed, &options, NULL, &syntax);
  if (status != SEQUEL_OK) {
    sequel_lexed_document_free(lexed);
    return status;
  }
  if (sequel_syntax_issue_count(syntax)) {
    sequel_syntax_document_free(syntax);
    sequel_lexed_document_free(lexed);
    return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  size_t case_count = 0, end_count = 0;
  for (size_t i = 0; i < token_count; ++i) {
    if (tokens[i].keyword_id == SEQUEL_KEYWORD_CASE)
      ++case_count;
    else if (tokens[i].keyword_id == SEQUEL_KEYWORD_END)
      ++end_count;
  }
  if (case_count > end_count) {
    sequel_syntax_document_free(syntax);
    sequel_lexed_document_free(lexed);
    return SEQUEL_ERR_INVALID_ARGUMENT;
  }

  sequel_format_options_t format_options;
  sequel_format_options_init(&format_options);
  sequel_text_result_t result = {0};
  status = sequel_format_tokens(lexed, &format_options, &result);
  sequel_syntax_document_free(syntax);
  sequel_lexed_document_free(lexed);
  if (status != SEQUEL_OK)
    return status;
  status = expand_deep_parentheses(&result);
  if (status != SEQUEL_OK) {
    sequel_text_result_free(&result);
    return status;
  }

  *length = result.length;
  if (out) {
    if (capacity <= result.length) {
      sequel_text_result_free(&result);
      return SEQUEL_ERR_OVERFLOW;
    }
    memcpy(out, result.data, result.length + 1);
  }
  sequel_text_result_free(&result);
  return SEQUEL_OK;
}
