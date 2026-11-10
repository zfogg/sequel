#pragma once

#include <stdbool.h>

#include <sequel/query.h>
#include <sequel/diagnostics.h>

typedef struct sequel_query_analysis sequel_query_analysis_t;

typedef struct {
  bool include_bindings;
  bool include_relations;
  bool include_projections;
  bool include_predicates;
  bool include_lints;
  bool include_portability;
  bool include_summary;
  bool include_fingerprint;
  bool include_sanitized_sql;
  bool include_formatted_sql;
  bool include_source_maps;
  sequel_dialect_t target_dialect;
  const sequel_dialect_t *portability_targets;
  size_t portability_target_count;
} sequel_query_analysis_options_t;

/** Initialize document-analysis options with all semantic collections enabled. */
void sequel_query_analysis_options_init(sequel_query_analysis_options_t *options);

/** Build an owned semantic analysis object from an immutable syntax document. */
sequel_status_t sequel_analyze_document(const sequel_syntax_document_t *syntax, const sequel_query_document_t *query,
                                        const sequel_query_analysis_options_t *options,
                                        const sequel_cancellation_t *cancellation, sequel_query_analysis_t **out);

/** Release a document analysis object. NULL is permitted. */
void sequel_query_analysis_free(sequel_query_analysis_t *analysis);

/** Return the normalized query document owned by the analysis object. */
const sequel_query_document_t *sequel_query_analysis_query(const sequel_query_analysis_t *analysis);

/** Return the diagnostics owned by the analysis object. */
const sequel_diagnostic_list_t *sequel_query_analysis_diagnostics(const sequel_query_analysis_t *analysis);

/** Return canonical bind metadata borrowed from the retained query document. */
const sequel_parameter_info_t *sequel_query_analysis_parameters(const sequel_query_analysis_t *analysis, size_t *count);
