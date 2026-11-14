#pragma once

/** @file @brief Lossless SQL token kinds, flags, and lexical-document accessors. */

#include <stddef.h>
#include <stdint.h>

#include <sequel/source.h>
#include <sequel/status.h>

SEQUEL_EXTERN_C_BEGIN

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
  sequel_source_range_t range;
  sequel_source_range_t leading_trivia;
  sequel_source_range_t trailing_trivia;
  uint32_t keyword_id;
  uint32_t dialect_id;
} sequel_token_t;

typedef struct sequel_lexed_document sequel_lexed_document_t;
typedef struct sequel_diagnostic_record sequel_diagnostic_record_t;

/** Release a lexical document. NULL is permitted. */
void sequel_lexed_document_free(sequel_lexed_document_t *document);

/** Return the source retained by a lexical document. */
sequel_source_view_t sequel_lexed_source(const sequel_lexed_document_t *document);

/** Return the owned source object retained by a lexical document. */
const sequel_source_t *sequel_lexed_source_object(const sequel_lexed_document_t *document);

/** Return the immutable lexical token array. */
const sequel_token_t *sequel_lexed_tokens(const sequel_lexed_document_t *document, size_t *count);

/** Return the immutable token count in a lexical document. */
size_t sequel_lexed_token_count(const sequel_lexed_document_t *document);

/** Return the dialect used to classify the document. */
sequel_dialect_t sequel_lexed_dialect(const sequel_lexed_document_t *document);

/** Return lexical diagnostics retained by the document. The array is borrowed. */
size_t sequel_lexed_diagnostic_count(const sequel_lexed_document_t *document);
const sequel_diagnostic_record_t *sequel_lexed_diagnostics(const sequel_lexed_document_t *document);

/** Return the exact source bytes covered by a token. */
sequel_status_t sequel_token_text(const sequel_source_view_t *source, const sequel_token_t *token, const char **data, size_t *length);

SEQUEL_EXTERN_C_END
