/** @file
 * @brief Deterministic summaries generated from normalized query data.
 */
#pragma once

#include <stdbool.h>

#include <sequel/formatter.h>
#include <sequel/query.h>

/** @brief Selects fields and redaction policies for a query summary. */
typedef struct {
  bool include_operation;
  bool include_tables;
  bool include_joins;
  bool include_predicates;
  bool include_bind_count;
  bool redact_literals;
  bool redact_relations;
  bool utf8_output;
  size_t max_length;
} sequel_summary_options_t;

/** @brief Initializes summary options with the default compact policy. */
void sequel_summary_options_init(sequel_summary_options_t *options);

/**
 * @brief Renders a deterministic summary of a normalized query.
 * @param query Query document to summarize.
 * @param options Optional summary policy.
 * @param[out] out Caller-owned text result.
 * @return @ref SEQUEL_STATUS_OK on success.
 */
sequel_status_t sequel_summarize_query(const sequel_query_document_t *query, const sequel_summary_options_t *options, sequel_text_result_t *out);
