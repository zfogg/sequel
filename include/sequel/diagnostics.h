#pragma once

#include <stddef.h>

#include <sequel/parser.h>
#include <sequel/query.h>
#include <sequel/status.h>

/** Stable diagnostic categories. */
typedef enum {
  SEQUEL_DIAG_NONE,
  SEQUEL_DIAG_NULL_INPUT,
  SEQUEL_DIAG_INVALID_ARGUMENT,
  SEQUEL_DIAG_INPUT_LIMIT,
  SEQUEL_DIAG_INVALID_UTF8,
  SEQUEL_DIAG_CONTROL_CHARACTER,
  SEQUEL_DIAG_TOKEN_LIMIT,
  SEQUEL_DIAG_NESTING_LIMIT,
  SEQUEL_DIAG_UNTERMINATED_STRING,
  SEQUEL_DIAG_UNTERMINATED_IDENTIFIER,
  SEQUEL_DIAG_UNTERMINATED_COMMENT,
  SEQUEL_DIAG_UNTERMINATED_PARAMETER,
  SEQUEL_DIAG_INVALID_PARAMETER,
  SEQUEL_DIAG_UNMATCHED_CLOSE,
  SEQUEL_DIAG_UNCLOSED_GROUP,
  SEQUEL_DIAG_UNSUPPORTED_PAYLOAD,
  SEQUEL_DIAG_UNSUPPORTED_LAYOUT,
  SEQUEL_DIAG_OUTPUT_LIMIT,
  SEQUEL_DIAG_OUT_OF_MEMORY,
  SEQUEL_DIAG_RESOURCE_LIMIT,
  SEQUEL_DIAG_CANCELLED,
  SEQUEL_DIAG_AMBIGUOUS_LITERAL,
  SEQUEL_DIAG_EMPTY_STATEMENT,
  SEQUEL_DIAG_MULTIPLE_STATEMENTS,
  SEQUEL_DIAG_MIXED_OPERATIONS,
  SEQUEL_DIAG_SELECT_STAR,
  SEQUEL_DIAG_UNBOUNDED_WRITE,
  SEQUEL_DIAG_LARGE_IN_LIST,
  SEQUEL_DIAG_DEEP_QUERY_NESTING,
  SEQUEL_DIAG_EXCESSIVE_JOINS,
  SEQUEL_DIAG_REPEATED_PREDICATE,
  SEQUEL_DIAG_UNKNOWN_OPERATION,
  SEQUEL_DIAG_NO_REFERENCED_TABLE,
  SEQUEL_DIAG_DIALECT_PORTABILITY,
  SEQUEL_DIAG_SQLC_ANNOTATION
} sequel_diagnostic_code_t;

typedef enum { SEQUEL_NOTE, SEQUEL_WARNING, SEQUEL_ERROR } sequel_severity_t;

/** Layer that produced a structured diagnostic. */
typedef enum {
  SEQUEL_DIAGNOSTIC_LEXICAL = 0,
  SEQUEL_DIAGNOSTIC_SYNTAX,
  SEQUEL_DIAGNOSTIC_SEMANTIC,
  SEQUEL_DIAGNOSTIC_POLICY,
  SEQUEL_DIAGNOSTIC_PORTABILITY,
  SEQUEL_DIAGNOSTIC_BINDING,
  SEQUEL_DIAGNOSTIC_API
} sequel_diagnostic_origin_t;

/** A collected parser/analysis issue with stable source ownership semantics. */
typedef struct {
  sequel_diagnostic_origin_t origin;
  sequel_diagnostic_code_t code;
  sequel_severity_t severity;
  sequel_source_range_t range;
  sequel_source_range_t related_range;
  size_t statement_index;
  size_t syntax_node;
  size_t query_item;
  int incomplete;
  int recovered;
  int statement_complete;
} sequel_diagnostic_record_t;

typedef struct {
  sequel_diagnostic_record_t *records;
  size_t count;
  size_t capacity;
} sequel_diagnostic_list_t;

void sequel_diagnostic_list_free(sequel_diagnostic_list_t *diagnostics);

/** Append one record, growing the owned list; returns false on overflow or allocation failure. */
bool sequel_diagnostic_list_append(sequel_diagnostic_list_t *diagnostics, sequel_diagnostic_record_t record);

/** Collect recoverable lexical-token diagnostics from a lossless token document. */
sequel_status_t sequel_collect_lexical_diagnostics(const sequel_lexed_document_t *lexed,
                                                   sequel_diagnostic_list_t *diagnostics);

/** Collect semantic and policy diagnostics from the normalized query model. */
sequel_status_t sequel_collect_query_diagnostics(const sequel_query_document_t *query,
                                                 sequel_diagnostic_list_t *diagnostics);

/** Collect only semantic diagnostics from the normalized query model. */
sequel_status_t sequel_collect_semantic_diagnostics(const sequel_query_document_t *query,
                                                    sequel_diagnostic_list_t *diagnostics);

/** Collect only policy diagnostics from the normalized query model. */
sequel_status_t sequel_collect_policy_diagnostics(const sequel_query_document_t *query,
                                                  sequel_diagnostic_list_t *diagnostics);

/** Collect dialect portability diagnostics for a syntax document. */
sequel_status_t sequel_collect_portability_diagnostics(const sequel_syntax_document_t *syntax, sequel_dialect_t target,
                                                       sequel_diagnostic_list_t *diagnostics);

/** Collect every recoverable syntax issue from a parsed document. */
sequel_status_t sequel_collect_syntax_diagnostics(const sequel_syntax_document_t *syntax,
                                                  sequel_diagnostic_list_t *diagnostics);
