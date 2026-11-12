#pragma once

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sequel/analysis.h>
#include <sequel.h>
#include <sequel/binds.h>
#include <sequel/diagnostics.h>
#include <sequel/dialect.h>
#include <sequel/fingerprint.h>
#include <sequel/formatter.h>
#include <sequel/parser.h>
#include <sequel/query.h>
#include <sequel/source.h>
#include <sequel/status.h>
#include <sequel/summary.h>

#include "../runtime.h"
#include "../runtime_types.h"
#include "../resources.h"
#include "../diagnostics.h"
#include "../util/text.h"
#include "../sha256.h"
#include "../util/utf8.h"
#include "../tokens.h"

typedef struct {
  sequel_map_segment_t *p;
  size_t n, capacity;
} analysis_map;

typedef struct {
  sequel_analysis_t *out;
  sequel_analysis_options_t options;
} analysis_state;

typedef struct {
  char *p;
  size_t n, capacity;
} analysis_text;

typedef struct {
  const char *key;
  size_t length, slot, identity;
  uint64_t hash;
  bool occupied, owned, numbered;
} analysis_binding;

typedef struct {
  analysis_binding *p;
  size_t count, capacity, maximum, identities;
} analysis_bindings;

analysis_binding *sequel_analysis_binding_slot(analysis_bindings *bindings, const char *key, size_t length,
                                               size_t number);
analysis_binding *sequel_analysis_numeric_identity(analysis_bindings *bindings, size_t slot);
void sequel_analysis_dictionary_clear(analysis_bindings *bindings);
void sequel_analysis_append_bind_descriptor(sequel_analysis_t *analysis, size_t *bind_number, size_t slot, size_t statement,
                                            size_t start, size_t end, sequel_bind_style_t style);
analysis_binding *sequel_analysis_canonical_binding(analysis_bindings *bindings, analysis_bindings *canonical_bindings,
                                                    const char *key, size_t key_length, size_t number,
                                                    size_t *fingerprint_number);

void *sequel_analysis_array(void *pointer, size_t count, size_t element);

bool sequel_analysis_literal(analysis_token_t token, sequel_dialect_t dialect);
bool sequel_analysis_word(analysis_token_t token, const char *word);
void sequel_analysis_mark_in_list(analysis_token_t *tokens, size_t count, size_t keyword_index,
                                  sequel_dialect_t dialect, bool keep_in_lists);
size_t sequel_analysis_sqlite_suffix(analysis_token_t token, sequel_dialect_t dialect, bool is_bind);
bool sequel_analysis_source_contains_word(sequel_source_view_t source, const char *word);
bool sequel_analysis_source_contains(sequel_source_view_t source, const char *needle);
void sequel_analysis_emit_token(analysis_token_t token, analysis_token_t *tokens, size_t matching, analysis_state *state,
                                analysis_text *safe, analysis_map *map, bool literal, bool bind);
void sequel_fingerprint_hash_literal(sequel_sha256 *hash, size_t *fingerprint_number);
void sequel_fingerprint_hash_token(sequel_sha256 *hash, analysis_token_t token, sequel_dialect_t dialect);
void sequel_fingerprint_hash_bind_type(sequel_sha256 *hash, analysis_token_t token, sequel_dialect_t dialect);
const char *sequel_analysis_dollar_quoted_literal(const char *sql, size_t length);
const char *sequel_analysis_ambiguous_escaped_quote(const char *sql, size_t length);
bool sequel_analysis_bind_token(analysis_token_t token, sequel_dialect_t dialect, const char **key, size_t *length,
                                size_t *number, sequel_bind_style_t *style);
void sequel_analysis_compact_list_text(analysis_text *text);
void sequel_analysis_append_text(analysis_text *text, const char *data, size_t length);
void sequel_analysis_charge(analysis_state *state, size_t bytes);
void sequel_analysis_utf8_cut(char *text, size_t limit);
void sequel_analysis_segment(analysis_map *map, size_t from_start, size_t from_end, size_t to_start, size_t to_end,
                             sequel_map_kind_t kind);
void sequel_analysis_build_format_map(analysis_map *map, const char *source, size_t source_length,
                                      const char *formatted, size_t formatted_length);
void sequel_analysis_clip_map(analysis_map *map, size_t limit);
void sequel_analysis_policy_diagnostics_pass(const char *sql, size_t length, analysis_token_t *tokens, size_t count,
                                             sequel_dialect_t dialect, sequel_analysis_t *out);
void sequel_analysis_positions(const char *sql, sequel_analysis_t *out);
void sequel_analysis_operation_from_query(const sequel_query_document_t *query, sequel_analysis_t *out);
void sequel_analysis_empty(sequel_analysis_t *out, size_t n, sequel_dialect_t dialect);
sequel_status_t sequel_analysis_fail(sequel_analysis_t *out, size_t n, sequel_dialect_t dialect, sequel_status_t status,
                                     sequel_diagnostic_code_t code);
void sequel_analysis_add_diagnostic(sequel_analysis_t *out, sequel_diagnostic_code_t code, size_t start, size_t end);
bool sequel_analysis_has_diagnostic(const sequel_analysis_t *out, sequel_diagnostic_code_t code, size_t start,
                                    size_t end);
void sequel_analysis_sort_diagnostics(sequel_analysis_t *out);
int sequel_analysis_append_diagnostics(sequel_diagnostic_list_t *destination, const sequel_diagnostic_list_t *source);
void sequel_analysis_add_statement(sequel_analysis_t *out, size_t start, size_t end);
size_t sequel_analysis_statement_index(const sequel_analysis_t *analysis, size_t offset, size_t hint);
void sequel_analysis_recovered_statement_ranges(const sequel_lexed_document_t *lexed, sequel_analysis_t *out);
sequel_status_t sequel_analysis_build_sanitized_map(const sequel_lexed_document_t *lexed, unsigned flags,
                                                    sequel_text_result_t *sanitized, sequel_analysis_source_map_t *map,
                                                    sequel_analysis_map_segment_t **owned_segments);
sequel_status_t sequel_analysis_build_formatted_map(sequel_source_view_t original,
                                                    const sequel_text_result_t *formatted,
                                                    sequel_analysis_source_map_t *map,
                                                    sequel_analysis_map_segment_t **owned_segments);
void sequel_analysis_summary_from_query(const sequel_query_document_t *query, sequel_analysis_t *out);
void sequel_analysis_summary_default(sequel_analysis_t *out);
sequel_status_t sequel_analysis_format_sanitized(const char *data, size_t length, sequel_dialect_t dialect,
                                                 sequel_text_result_t *out);
