#pragma once

/** @file
 * @brief Public lexing, parsing, recovery, and incremental reparse API.
 *
 * Parsing produces lossless tokens and a recovered syntax tree. Returned
 * objects own their internal arrays; accessor results are borrowed.
 */

#include <stddef.h>
#include <stdbool.h>

#include <sequel/dialect.h>
#include <sequel/lexer.h>
#include <sequel/limits.h>
#include <sequel/status.h>
#include <sequel/syntax.h>

SEQUEL_EXTERN_C_BEGIN

typedef struct sequel_syntax_document sequel_syntax_document_t;
typedef struct sequel_diagnostic_record sequel_diagnostic_record_t;

typedef struct {
  bool (*enter_node)(const sequel_syntax_document_t *document, size_t node_index, void *userdata);
  bool (*leave_node)(const sequel_syntax_document_t *document, size_t node_index, void *userdata);
  void *userdata;
} sequel_syntax_visitor_t;

typedef struct {
  size_t start;
  size_t old_end;
  const char *replacement;
  size_t replacement_length;
} sequel_text_edit_t;

typedef struct {
  sequel_source_range_t old_range;
  sequel_source_range_t new_range;
  size_t preserved_prefix;
  size_t preserved_suffix;
} sequel_reparse_impact_t;

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
  bool enable_incremental_metadata;
} sequel_parse_options_t;

/** Initialize parser options with the library defaults. */
void sequel_parse_options_init(sequel_parse_options_t *options);

/** Lex explicit UTF-8 source bytes into a lossless token document. */
sequel_status_t sequel_lex(const sequel_source_view_t *source, const sequel_parse_options_t *options, const sequel_cancellation_t *cancellation, sequel_lexed_document_t **out);

/**
 * Parse a lexical document into a recovered syntax document.
 *
 * The returned syntax document borrows `lexed`; callers must keep it alive
 * until the syntax document is released. `sequel_parse_sql` transfers the
 * lexical document's ownership to the returned syntax document instead.
 */
sequel_status_t sequel_parse(const sequel_lexed_document_t *lexed, const sequel_parse_options_t *options, const sequel_cancellation_t *cancellation, sequel_syntax_document_t **out);

sequel_status_t sequel_syntax_visit(const sequel_syntax_document_t *document, size_t root_node, const sequel_syntax_visitor_t *visitor);

/** Parse and lex explicit UTF-8 source bytes in one operation. */
sequel_status_t sequel_parse_sql(const sequel_source_view_t *source, const sequel_parse_options_t *options, const sequel_cancellation_t *cancellation, sequel_syntax_document_t **out);

/**
 * Reparse a document after replacing [edit->start, edit->old_end).
 *
 * The returned document owns its replacement source and lexical document; it
 * is independent of both `previous` and `edit` after this call returns.
 * Unchanged ranges retain equivalent source coordinates, but callers must not
 * depend on pointer identity for tokens or syntax nodes.
 */
sequel_status_t sequel_reparse(const sequel_syntax_document_t *previous, const sequel_text_edit_t *edit, const sequel_parse_options_t *options, const sequel_cancellation_t *cancellation,
                               sequel_syntax_document_t **out);

/** Compute the byte regions affected by an edit without reparsing. */
sequel_status_t sequel_reparse_impact(size_t old_length, const sequel_text_edit_t *edit, sequel_reparse_impact_t *impact);

SEQUEL_EXTERN_C_END
