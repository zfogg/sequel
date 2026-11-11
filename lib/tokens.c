#include <stdlib.h>
#include <string.h>

#include "analysis/analysis_internal.h"

int sequel_token_is_comment(const sequel_token_t *token) {
  return token->kind == SEQUEL_TOKEN_LINE_COMMENT || token->kind == SEQUEL_TOKEN_BLOCK_COMMENT;
}

int sequel_token_is_punctuation(const sequel_token_t *token) { return token->kind == SEQUEL_TOKEN_PUNCTUATION; }

int sequel_token_is_keyword(const sequel_token_t *token) { return token->kind == SEQUEL_TOKEN_KEYWORD; }

int sequel_token_is_operator(const sequel_token_t *token) { return token->kind == SEQUEL_TOKEN_OPERATOR; }

int sequel_token_is_literal(const sequel_token_t *token) { return (token->flags & SEQUEL_TOKEN_FLAG_LITERAL) != 0; }

int sequel_token_is_bind(const sequel_token_t *token) { return (token->flags & SEQUEL_TOKEN_FLAG_BIND) != 0; }

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

#include "analysis/tokens.c"
