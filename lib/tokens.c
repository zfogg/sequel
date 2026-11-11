#include <stdlib.h>
#include <string.h>

#include "analysis/analysis_internal.h"

int sequel_token_is_comment(const sequel_token_t *token) {
  return token->kind == SEQUEL_TOKEN_LINE_COMMENT || token->kind == SEQUEL_TOKEN_BLOCK_COMMENT;
}

int sequel_token_is_trivia(const sequel_token_t *token) {
  return token->kind == SEQUEL_TOKEN_WHITESPACE || sequel_token_is_comment(token);
}

int sequel_token_is_identifier(const sequel_token_t *token) {
  return token->kind == SEQUEL_TOKEN_IDENTIFIER || token->kind == SEQUEL_TOKEN_QUOTED_IDENTIFIER;
}

int sequel_token_is_punctuation(const sequel_token_t *token) { return token->kind == SEQUEL_TOKEN_PUNCTUATION; }

int sequel_token_is_keyword(const sequel_token_t *token) { return token->kind == SEQUEL_TOKEN_KEYWORD; }

int sequel_token_is_operator(const sequel_token_t *token) { return token->kind == SEQUEL_TOKEN_OPERATOR; }

int sequel_token_is_literal(const sequel_token_t *token) { return (token->flags & SEQUEL_TOKEN_FLAG_LITERAL) != 0; }

int sequel_token_is_bind(const sequel_token_t *token) { return (token->flags & SEQUEL_TOKEN_FLAG_BIND) != 0; }

size_t sequel_token_count_keyword(const sequel_lexed_document_t *lexed, uint32_t keyword) {
  size_t count = 0;
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  for (size_t i = 0; i < token_count; ++i)
    if (tokens[i].kind == SEQUEL_TOKEN_KEYWORD && tokens[i].keyword_id == keyword)
      ++count;
  return count;
}

int sequel_token_has_range(const sequel_token_t *token, size_t source_length) {
  return token->range.start <= token->range.end && token->range.end <= source_length;
}

const char *sequel_token_source_text(sequel_source_view_t source, const sequel_token_t *token, size_t *length) {
  *length = token->range.end - token->range.start;
  return source.data + token->range.start;
}

int sequel_token_is_symbol(sequel_source_view_t source, const sequel_token_t *token, char symbol) {
  return sequel_token_is_punctuation(token) && token->range.end == token->range.start + 1 &&
         source.data[token->range.start] == symbol;
}

int sequel_keyword_is_clause(uint32_t keyword) {
  return keyword == SEQUEL_KEYWORD_FROM || keyword == SEQUEL_KEYWORD_WHERE || keyword == SEQUEL_KEYWORD_GROUP ||
         keyword == SEQUEL_KEYWORD_HAVING || keyword == SEQUEL_KEYWORD_ORDER || keyword == SEQUEL_KEYWORD_LIMIT ||
         keyword == SEQUEL_KEYWORD_OFFSET || keyword == SEQUEL_KEYWORD_RETURNING || keyword == SEQUEL_KEYWORD_VALUES ||
         keyword == SEQUEL_KEYWORD_SET || keyword == SEQUEL_KEYWORD_UNION || keyword == SEQUEL_KEYWORD_JOIN ||
         keyword == SEQUEL_KEYWORD_ON || keyword == SEQUEL_KEYWORD_QUALIFY;
}

int sequel_keyword_starts_list_clause(uint32_t keyword) {
  return keyword == SEQUEL_KEYWORD_SELECT || keyword == SEQUEL_KEYWORD_GROUP || keyword == SEQUEL_KEYWORD_ORDER ||
         keyword == SEQUEL_KEYWORD_SET || keyword == SEQUEL_KEYWORD_VALUES || keyword == SEQUEL_KEYWORD_RETURNING;
}

int sequel_keyword_is_case_expression_boundary(uint32_t keyword) {
  return keyword == SEQUEL_KEYWORD_WHEN || keyword == SEQUEL_KEYWORD_ELSE || keyword == SEQUEL_KEYWORD_END;
}

#include "analysis/tokens.c"
