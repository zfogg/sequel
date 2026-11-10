#pragma once

#include <stddef.h>
#include <stdbool.h>

#include <sequel/dialect.h>
#include <sequel/lexer.h>
#include <sequel/status.h>
#include <sequel/syntax.h>

SEQUEL_EXTERN_C_BEGIN

typedef struct sequel_lexed_document sequel_lexed_document_t;
typedef struct sequel_syntax_document sequel_syntax_document_t;

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

typedef enum {
  SEQUEL_PARSE_ISSUE_UNMATCHED_CLOSE = 1,
  SEQUEL_PARSE_ISSUE_UNCLOSED_GROUP,
  SEQUEL_PARSE_ISSUE_UNTERMINATED_TOKEN,
  SEQUEL_PARSE_ISSUE_UNEXPECTED_TOKEN
} sequel_parse_issue_code_t;

typedef enum {
  SEQUEL_RECOVERY_NONE = 0,
  SEQUEL_RECOVERY_INSERTED_TOKEN,
  SEQUEL_RECOVERY_SKIPPED_TOKEN,
  SEQUEL_RECOVERY_SYNCHRONIZED_CLAUSE,
  SEQUEL_RECOVERY_OPAQUE_REGION
} sequel_recovery_kind_t;

typedef struct {
  sequel_parse_issue_code_t code;
  sequel_recovery_kind_t recovery;
  sequel_source_range_t range;
  size_t statement_index;
} sequel_parse_issue_t;

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
sequel_status_t sequel_lex(const sequel_source_view_t *source, const sequel_parse_options_t *options,
                           const sequel_cancellation_t *cancellation, sequel_lexed_document_t **out);

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
sequel_dialect_t sequel_lexed_dialect(const sequel_lexed_document_t *document);

sequel_status_t sequel_token_text(const sequel_source_view_t *source, const sequel_token_t *token, const char **data,
                                  size_t *length);

/** Parse a lexical document into a recovered syntax document. */
sequel_status_t sequel_parse(const sequel_lexed_document_t *lexed, const sequel_parse_options_t *options,
                             const sequel_cancellation_t *cancellation, sequel_syntax_document_t **out);

/** Release a syntax document. NULL is permitted. */
void sequel_syntax_document_free(sequel_syntax_document_t *document);

/** Return the lexical document retained by a syntax document. */
const sequel_lexed_document_t *sequel_syntax_lexed(const sequel_syntax_document_t *document);

/** Return the immutable syntax-node array. */
const sequel_syntax_node_t *sequel_syntax_nodes(const sequel_syntax_document_t *document, size_t *count);

/** Return one immutable syntax node, or NULL for an invalid index. */
const sequel_syntax_node_t *sequel_syntax_node(const sequel_syntax_document_t *document, size_t node_index);

/** Return the number of syntax nodes in the document. */
size_t sequel_syntax_node_count(const sequel_syntax_document_t *document);

/** Return child node indexes for a syntax node. */
const size_t *sequel_syntax_children(const sequel_syntax_document_t *document, size_t node_index, size_t *count);

/** Return the immutable token span covered by a syntax node. */
const sequel_token_t *sequel_syntax_tokens(const sequel_syntax_document_t *document, size_t node_index, size_t *count);

const sequel_parse_issue_t *sequel_syntax_issues(const sequel_syntax_document_t *document, size_t *count);

/** Return the number of recovered syntax issues in a syntax document. */
size_t sequel_syntax_issue_count(const sequel_syntax_document_t *document);

sequel_status_t sequel_syntax_visit(const sequel_syntax_document_t *document, size_t root_node,
                                    const sequel_syntax_visitor_t *visitor);

/** Parse and lex explicit UTF-8 source bytes in one operation. */
sequel_status_t sequel_parse_sql(const sequel_source_view_t *source, const sequel_parse_options_t *options,
                                 const sequel_cancellation_t *cancellation, sequel_syntax_document_t **out);

sequel_status_t sequel_reparse(const sequel_syntax_document_t *previous, const sequel_text_edit_t *edit,
                               const sequel_parse_options_t *options, const sequel_cancellation_t *cancellation,
                               sequel_syntax_document_t **out);

/** Compute the byte regions affected by an edit without reparsing. */
sequel_status_t sequel_reparse_impact(size_t old_length, const sequel_text_edit_t *edit,
                                      sequel_reparse_impact_t *impact);

SEQUEL_EXTERN_C_END
