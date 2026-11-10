#pragma once

#include <stddef.h>

#include <sequel/dialect.h>
#include <sequel/lexer.h>
#include <sequel/status.h>

typedef struct sequel_lexed_document sequel_lexed_document_t;
typedef struct sequel_syntax_document sequel_syntax_document_t;

typedef bool (*sequel_cancel_callback_t)(void *userdata);

typedef struct {
  sequel_cancel_callback_t is_cancelled;
  void *userdata;
} sequel_cancellation_t;

typedef struct {
  sequel_dialect_t dialect;
  bool retain_comments;
  bool retain_whitespace;
  bool retain_source_text;
  bool tolerant;
  bool recover_errors;
  bool parse_vendor_extensions;
  size_t max_input_bytes;
  size_t max_tokens;
  size_t max_nodes;
  size_t max_nesting;
  size_t max_diagnostics;
  size_t max_memory_bytes;
  size_t max_work_units;
} sequel_parse_options_t;

/** Initialize parser options with the library defaults. */
void sequel_parse_options_init(sequel_parse_options_t *options);

/** Lex explicit UTF-8 source bytes into a lossless token document. */
sequel_status_t sequel_lex(const sequel_source_view_t *source, const sequel_parse_options_t *options,
                           const sequel_cancellation_t *cancellation, sequel_lexed_document_t **out);

/** Release a lexical document. NULL is permitted. */
void sequel_lexed_document_free(sequel_lexed_document_t *document);

/** Return the source retained by a lexical document. */
sequel_source_view_t sequel_lexed_source(const sequel_lexed_document_t *document);

/** Return the immutable lexical token array. */
const sequel_token_t *sequel_lexed_tokens(const sequel_lexed_document_t *document, size_t *count);

/** Parse a lexical document into a recovered syntax document. */
sequel_status_t sequel_parse(const sequel_lexed_document_t *lexed, const sequel_parse_options_t *options,
                             const sequel_cancellation_t *cancellation, sequel_syntax_document_t **out);

/** Release a syntax document. NULL is permitted. */
void sequel_syntax_document_free(sequel_syntax_document_t *document);

/** Parse and lex explicit UTF-8 source bytes in one operation. */
sequel_status_t sequel_parse_sql(const sequel_source_view_t *source, const sequel_parse_options_t *options,
                                 const sequel_cancellation_t *cancellation, sequel_syntax_document_t **out);
