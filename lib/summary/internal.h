#pragma once

#include <stddef.h>
#include <sequel/summary.h>
#include <sequel/query.h>

int sequel_summary_append_text(char **text, size_t *length, size_t *capacity, const char *value);
int sequel_summary_append_bytes(char **text, size_t *length, size_t *capacity, const char *value, size_t amount);

int sequel_summary_word(const char *source, size_t length, size_t offset, const char *word);
size_t sequel_summary_skip_space(const char *source, size_t length, size_t offset);
const char *sequel_summary_operation_name(sequel_operation_t operation);
int sequel_summary_append_redacted_clause(char **text, size_t *length, size_t *capacity);
int sequel_summary_append_body_summary(char **text, size_t *length, size_t *capacity, const char *source,
                                       sequel_source_range_t range);
int sequel_summary_append_relation_name(char **text, size_t *length, size_t *capacity,
                                        sequel_source_view_t source, const sequel_relation_info_t *relation);

void sequel_summary_normalize_projection(char *text, size_t *length);

int sequel_summary_append_statement_shape(char **text, size_t *length, size_t *capacity,
                                          sequel_source_view_t source,
                                          const sequel_query_document_t *query,
                                          const sequel_summary_options_t *settings,
                                          const sequel_statement_info_t *statement);
