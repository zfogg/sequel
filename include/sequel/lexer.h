#pragma once

#include <stddef.h>
#include <stdint.h>

#include <sequel/source.h>

typedef enum {
  SEQUEL_TOKEN_EOF = 0,
  SEQUEL_TOKEN_IDENTIFIER,
  SEQUEL_TOKEN_QUOTED_IDENTIFIER,
  SEQUEL_TOKEN_KEYWORD,
  SEQUEL_TOKEN_STRING,
  SEQUEL_TOKEN_NUMBER,
  SEQUEL_TOKEN_BOOLEAN,
  SEQUEL_TOKEN_NULL,
  SEQUEL_TOKEN_PARAMETER,
  SEQUEL_TOKEN_OPERATOR,
  SEQUEL_TOKEN_PUNCTUATION,
  SEQUEL_TOKEN_LINE_COMMENT,
  SEQUEL_TOKEN_BLOCK_COMMENT,
  SEQUEL_TOKEN_WHITESPACE,
  SEQUEL_TOKEN_ERROR
} sequel_token_kind_t;

enum {
  SEQUEL_TOKEN_FLAG_NONE = 0,
  SEQUEL_TOKEN_FLAG_CASE_FOLDED = 1u << 0,
  SEQUEL_TOKEN_FLAG_UNTERMINATED = 1u << 1,
  SEQUEL_TOKEN_FLAG_RECOVERED = 1u << 2,
  SEQUEL_TOKEN_FLAG_DIALECT_SPECIFIC = 1u << 3,
  SEQUEL_TOKEN_FLAG_LITERAL = 1u << 4,
  SEQUEL_TOKEN_FLAG_BIND = 1u << 5,
  SEQUEL_TOKEN_FLAG_COMMENT = 1u << 6
};

/** A lossless lexical token represented entirely by source ranges. */
typedef struct {
  sequel_token_kind_t kind;
  uint32_t flags;
  sequel_range_t range;
  sequel_range_t leading_trivia;
  sequel_range_t trailing_trivia;
  uint32_t keyword_id;
  uint32_t dialect_id;
} sequel_token_t;
