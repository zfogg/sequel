#pragma once

#include <stddef.h>
#include <sequel/summary.h>
#include <sequel/query.h>

typedef struct {
  char *text;
  size_t length;
  size_t capacity;
} sequel_summary_render_state_t;

int sequel_summary_render_shape_phases(sequel_summary_render_state_t *state, const sequel_query_document_t *query, const sequel_summary_options_t *settings, sequel_source_view_t source,
                                       size_t statement_count, const sequel_statement_info_t *statements, sequel_source_range_t outer_range, size_t cte_prefix_end, size_t outer_start,
                                       bool nested_cte_scope, bool insert_values_scope, bool cte_insert_scope, bool scalar_with_scope);

int sequel_summary_render_postprocess(sequel_summary_render_state_t *state, const sequel_query_document_t *query, const sequel_summary_options_t *settings, sequel_source_view_t source,
                                      size_t statement_count, const sequel_statement_info_t *statements, size_t outer_start, size_t cte_prefix_end);

int sequel_summary_render_repairs(sequel_summary_render_state_t *state, const sequel_query_document_t *query, const sequel_summary_options_t *settings, sequel_source_view_t source,
                                  size_t statement_count, const sequel_statement_info_t *statements, size_t outer_start, bool cte_insert_scope);

int sequel_summary_render_cte_shape(sequel_summary_render_state_t *state, const sequel_query_document_t *query, const sequel_summary_options_t *settings, sequel_source_view_t source,
                                    size_t statement_count, const sequel_statement_info_t *statements, size_t outer_start);

int sequel_summary_render_clause_repairs(sequel_summary_render_state_t *state, const sequel_query_document_t *query, const sequel_summary_options_t *settings, sequel_source_view_t source,
                                         size_t statement_count, const sequel_statement_info_t *statements, size_t outer_start, bool insert_values_scope, bool cte_insert_scope);

int sequel_summary_append_join_completion(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, sequel_source_view_t source);
int sequel_summary_append_limit_completions(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query);
void sequel_summary_restore_dialect_features(const sequel_query_document_t *query, char *text, size_t length, sequel_source_view_t source);

int sequel_summary_append_text(char **text, size_t *length, size_t *capacity, const char *value);
int sequel_summary_append_bytes(char **text, size_t *length, size_t *capacity, const char *value, size_t amount);
void sequel_summary_set_utf8_output(bool enabled);
const char *sequel_summary_ellipsis(void);
size_t sequel_summary_ellipsis_length(void);

char *sequel_summary_find_text(const char *source, const char *needle);

int sequel_summary_word(const char *source, size_t length, size_t offset, const char *word);
const char *sequel_summary_find_word(const char *source, size_t length, size_t offset, const char *word);
size_t sequel_summary_skip_space(const char *source, size_t length, size_t offset);
bool sequel_summary_cursor_is_quoted_identifier(const sequel_query_document_t *query, size_t cursor);
const char *sequel_summary_operation_name(sequel_operation_t operation);
int sequel_summary_append_redacted_clause(char **text, size_t *length, size_t *capacity);
int sequel_summary_append_body_summary(char **text, size_t *length, size_t *capacity, const char *source, sequel_source_range_t range, bool preserve_limit);
int sequel_summary_append_relation_name(char **text, size_t *length, size_t *capacity, sequel_source_view_t source, const sequel_relation_info_t *relation);
int sequel_summary_append_join_condition(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, const sequel_join_info_t *join);

void sequel_summary_normalize_projection(char *text, size_t *length);

void sequel_summary_redact_rendered_literals(char *text, size_t *length, sequel_dialect_t dialect);

int sequel_summary_append_statement_shape(char **text, size_t *length, size_t *capacity, sequel_source_view_t source, const sequel_query_document_t *query, const sequel_summary_options_t *settings,
                                          const sequel_statement_info_t *statement);

int sequel_summary_append_projection_shape(char **text, size_t *length, size_t *capacity, sequel_source_view_t source, const sequel_query_document_t *query, const sequel_statement_info_t *statement);

int sequel_summary_append_parenthesized_subquery(char **text, size_t *length, size_t *capacity, sequel_source_view_t source);

int sequel_summary_normalize_insert_clause_order(char **text, size_t *length, size_t *capacity);
int sequel_summary_normalize_insert_conflict_order(char **text, size_t *length);
int sequel_summary_remove_insert_projection_tail(char *text, size_t *length);
bool sequel_summary_has_clause(const sequel_query_document_t *query, uint32_t keyword_id);
bool sequel_summary_has_token(const sequel_query_document_t *query, uint32_t keyword_id);
bool sequel_summary_starts_function_projection(const sequel_query_document_t *query);
int sequel_summary_normalize_projection_shape(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query);
int sequel_summary_render_scalar_projection_summary(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, sequel_source_view_t source,
                                                    const sequel_statement_info_t *statement);
int sequel_summary_append_parameter_only(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, const sequel_statement_info_t *statement);
int sequel_summary_append_clickhouse_dollar_projection(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, sequel_source_view_t source,
                                                       const sequel_statement_info_t *statement);
int sequel_summary_append_cte_shape(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, sequel_source_view_t source, const sequel_statement_info_t *statement);
int sequel_summary_append_clickhouse_create_shape(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, sequel_source_view_t source,
                                                  const sequel_statement_info_t *statement);
bool sequel_summary_has_projection_scope(const sequel_query_document_t *query);
bool sequel_summary_has_outer_from(const sequel_query_document_t *query);
const char *sequel_summary_matching_close(const char *open, const char *end);
int sequel_summary_append_redacted_range(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, size_t start, size_t end);
int sequel_summary_normalize_nested_subqueries(char **text, size_t *length, size_t *capacity);
int sequel_summary_preserve_keyword(char **text, size_t *length, size_t *capacity, const char *source, size_t source_length, const char *keyword, const char *replacement);
int sequel_summary_prepare_postprocess(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, sequel_source_view_t source, size_t statement_count,
                                       const sequel_statement_info_t *statements);
int sequel_summary_finalize_output(char **text, size_t *length, size_t *capacity, sequel_text_result_t *out);
int sequel_summary_finish_render(char **text, size_t *length, size_t *capacity, sequel_text_result_t *out, const sequel_query_document_t *query, sequel_source_view_t source, size_t outer_start,
                                 const sequel_statement_info_t *statements, size_t statement_count);
int sequel_summary_normalize_order_limit(char **text, size_t *length, size_t *capacity);

int sequel_summary_append_missing_keyword(char **text, size_t *length, size_t *capacity, sequel_source_view_t source, const char *keyword, const char *replacement);

int sequel_summary_repair_tail(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, sequel_source_view_t source);

int sequel_summary_repair_write_order(char **text, size_t *length, size_t *capacity, sequel_source_view_t source);

int sequel_summary_repair_mysql_update_aliases(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, sequel_source_view_t source, size_t outer_start);

int sequel_summary_repair_create_order(char **text, size_t *length, size_t *capacity, sequel_source_view_t source, const sequel_query_document_t *query, const sequel_statement_info_t *statements,
                                       size_t statement_count, bool include_predicates);

int sequel_summary_repair_clickhouse_cte(char **text, size_t *length, sequel_source_view_t source, const sequel_query_document_t *query);

int sequel_summary_repair_group_order(char **text, size_t *length, size_t *capacity, sequel_source_view_t source);

int sequel_summary_repair_set_operators(char **text, size_t *length, size_t *capacity, sequel_source_view_t source, const sequel_query_document_t *query, const sequel_statement_info_t *statements,
                                        size_t statement_count, bool include_predicates);

int sequel_summary_repair_format_clauses(char **text, size_t *length, size_t *capacity, sequel_source_view_t source, const sequel_query_document_t *query, size_t outer_start, bool include_tables,
                                         size_t statement_count);

int sequel_summary_repair_cte_insert(char **text, size_t *length, sequel_source_view_t source, bool cte_insert_scope);

int sequel_summary_append_bind_count(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, bool include_bind_count);

int sequel_summary_repair_outer_limit(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query);

void sequel_summary_restore_clickhouse_limit_by(char *text, size_t length, sequel_source_view_t source);
int sequel_summary_restore_numeric_limit_shape(char **text, size_t *length, size_t *capacity, sequel_source_view_t source, sequel_source_range_t clause_range);
int sequel_summary_restore_numeric_offset_shape(char **text, size_t *length, size_t *capacity, sequel_source_view_t source);
int sequel_summary_restore_scalar_limit_shape(char **text, size_t *length, size_t *capacity, sequel_source_view_t source);

int sequel_summary_append_dialect_recovery(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, const sequel_summary_options_t *settings, sequel_source_view_t source,
                                           size_t statement_count, const sequel_statement_info_t *statements);

int sequel_summary_prepare_document(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, const sequel_summary_options_t *settings, sequel_source_view_t source,
                                    size_t statement_count, const sequel_statement_info_t *statements, int *special_route, size_t *cte_prefix_end, size_t *outer_start, bool *nested_cte_scope,
                                    bool *cte_insert_scope, bool *scalar_with_scope);

sequel_status_t sequel_summary_render_document(const sequel_query_document_t *query, const sequel_summary_options_t *options, sequel_text_result_t *out);

sequel_status_t sequel_summary_render_query(const sequel_query_document_t *query, const sequel_summary_options_t *options, sequel_text_result_t *out);

int sequel_summary_begin_render(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, const sequel_summary_options_t *settings, sequel_source_view_t source,
                                size_t statement_count, const sequel_statement_info_t *statements, int *route);

int sequel_summary_try_special_routes(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, const sequel_summary_options_t *settings, sequel_source_view_t source,
                                      size_t statement_count, const sequel_statement_info_t *statements);

int sequel_summary_append_table_prefix(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, const sequel_summary_options_t *settings, sequel_source_view_t source,
                                       size_t statement_count);

int sequel_summary_append_nested_cte_projection(char **text, size_t *length, size_t *capacity, sequel_source_view_t source, bool *nested_cte_scope);

int sequel_summary_append_cte_insert_shape(char **text, size_t *length, size_t *capacity, sequel_source_view_t source, const sequel_cte_info_t *ctes, size_t cte_count, bool *cte_insert_scope);

int sequel_summary_append_cte_definitions(char **text, size_t *length, size_t *capacity, sequel_source_view_t source, const sequel_cte_info_t *ctes, size_t cte_count, bool recursive,
                                          size_t *cte_prefix_end);

sequel_status_t sequel_summary_render(const sequel_query_document_t *query, const sequel_summary_options_t *options, sequel_text_result_t *out);
