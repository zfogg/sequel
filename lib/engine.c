#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include <sequel.h>
#include <sequel/formatter.h>
#include <sequel/limits.h>
#include <sequel/parser.h>

#include "util/utf8.h"
#include "util/text.h"

static bool source_contains_json_operator(sequel_source_view_t source) {
  for (size_t i = 0; i + 1 < source.length; ++i) {
    if ((source.data[i] == '#' && source.data[i + 1] == '>') || (source.data[i] == '?' && (source.data[i + 1] == '|' || source.data[i + 1] == '&')) ||
        (source.data[i] == '@' && source.data[i + 1] == '>') || (source.data[i] == '<' && source.data[i + 1] == '@'))
      return true;
  }
  return false;
}

static bool engine_token_is_word(sequel_source_view_t source, const sequel_token_t *token, const char *word) {
  size_t length = token->range.end - token->range.start;
  size_t expected = strlen(word);
  if (length != expected)
    return false;
  for (size_t i = 0; i < length; ++i)
    if (toupper((unsigned char)source.data[token->range.start + i]) != toupper((unsigned char)word[i]))
      return false;
  return true;
}

static bool has_clickhouse_payload(sequel_source_view_t source, const sequel_token_t *tokens, size_t count) {
  if (!count)
    return false;
  bool insert = false;
  for (size_t i = 0; i < count; ++i) {
    if (tokens[i].kind == SEQUEL_TOKEN_EOF)
      break;
    if (engine_token_is_word(source, &tokens[i], "INSERT"))
      insert = true;
    else if (insert && engine_token_is_word(source, &tokens[i], "FORMAT"))
      return true;
    else if (tokens[i].kind == SEQUEL_TOKEN_PUNCTUATION && source.data[tokens[i].range.start] == ';')
      insert = false;
  }
  return false;
}

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
  bool within_group = sequel_text_find(sql, source_length, "WITHIN") != NULL;
  if (within_group) {
    *length = source_length;
    if (!out)
      return SEQUEL_OK;
    if (capacity <= source_length)
      return SEQUEL_ERR_OVERFLOW;
    memcpy(out, sql, source_length);
    out[source_length] = 0;
    return SEQUEL_OK;
  }

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
  if (dialect == SEQUEL_CLICKHOUSE && has_clickhouse_payload(source, tokens, token_count)) {
    sequel_syntax_document_free(syntax);
    sequel_lexed_document_free(lexed);
    return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  size_t comment_count = 0;
  bool comment_before_sql = false;
  bool saw_sql = false;
  bool json_operator = source_contains_json_operator(source);
  for (size_t i = 0; i < token_count; ++i) {
    if (tokens[i].kind == SEQUEL_TOKEN_LINE_COMMENT || tokens[i].kind == SEQUEL_TOKEN_BLOCK_COMMENT) {
      ++comment_count;
      if (saw_sql) {
        for (size_t j = i + 1; j < token_count; ++j)
          if (tokens[j].kind != SEQUEL_TOKEN_WHITESPACE && tokens[j].kind != SEQUEL_TOKEN_LINE_COMMENT && tokens[j].kind != SEQUEL_TOKEN_BLOCK_COMMENT && tokens[j].kind != SEQUEL_TOKEN_EOF) {
            comment_before_sql = true;
            break;
          }
      }
    } else if (tokens[i].kind != SEQUEL_TOKEN_WHITESPACE && tokens[i].kind != SEQUEL_TOKEN_EOF) {
      saw_sql = true;
    }
  }
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
  /* Dense generated SQL often carries comment attachment semantics that a
   * recovered tree cannot safely reflow. Keep those documents lossless until
   * the formatter has a comment-aware syntax tree. */
  if (comment_before_sql || json_operator || within_group || (comment_count > 8 && source_length > 4096))
    format_options.mode = SEQUEL_FORMAT_LEXICAL;
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
