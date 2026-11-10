#pragma once

#include <stdbool.h>

#include <sequel/formatter.h>
#include <sequel/query.h>

typedef struct {
  bool include_operation;
  bool include_tables;
  bool include_joins;
  bool include_predicates;
  bool include_bind_count;
  bool redact_literals;
  size_t max_length;
} sequel_summary_options_t;

void sequel_summary_options_init(sequel_summary_options_t *options);
sequel_status_t sequel_summarize_query(const sequel_query_document_t *query, const sequel_summary_options_t *options,
                                       sequel_text_result_t *out);
