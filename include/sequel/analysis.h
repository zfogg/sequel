#pragma once

#include <stdbool.h>

#include <sequel/query.h>
#include <sequel/diagnostics.h>
#include <sequel/fingerprint.h>
#include <sequel/formatter.h>

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

typedef enum {
  SEQUEL_ANALYSIS_MAP_COPY,
  SEQUEL_ANALYSIS_MAP_REPLACE,
  SEQUEL_ANALYSIS_MAP_DELETE,
  SEQUEL_ANALYSIS_MAP_INSERT
} sequel_analysis_map_kind_t;

typedef struct {
  size_t from_start;
  size_t from_end;
  size_t to_start;
  size_t to_end;
  sequel_analysis_map_kind_t kind;
} sequel_analysis_map_segment_t;

typedef struct {
  size_t from_length;
  size_t to_length;
  const sequel_analysis_map_segment_t *segments;
  size_t count;
} sequel_analysis_source_map_t;

/** Initialize document-analysis options with all semantic collections enabled. */
void sequel_query_analysis_options_init(sequel_query_analysis_options_t *options);

/** Build an owned semantic analysis object from an immutable syntax document. */
sequel_status_t sequel_analyze_document(const sequel_syntax_document_t *syntax, const sequel_query_document_t *query,
                                        const sequel_query_analysis_options_t *options,
                                        const sequel_cancellation_t *cancellation, sequel_query_analysis_t **out);

/** Release a document analysis object. NULL is permitted. */
void sequel_query_analysis_free(sequel_query_analysis_t *analysis);

/** Return the normalized query document supplied to the analysis object. The pointer is borrowed. */
const sequel_query_document_t *sequel_query_analysis_query(const sequel_query_analysis_t *analysis);

/** Return the diagnostics owned by the analysis object. */
const sequel_diagnostic_list_t *sequel_query_analysis_diagnostics(const sequel_query_analysis_t *analysis);

/** Return canonical bind metadata borrowed from the retained query document. */
const sequel_parameter_info_t *sequel_query_analysis_parameters(const sequel_query_analysis_t *analysis, size_t *count);

/** Return relations when relation collection was requested. */
const sequel_relation_info_t *sequel_query_analysis_relations(const sequel_query_analysis_t *analysis, size_t *count);

/** Return projections when projection collection was requested. */
const sequel_projection_info_t *sequel_query_analysis_projections(const sequel_query_analysis_t *analysis,
                                                                  size_t *count);

/** Return predicates when predicate collection was requested. */
const sequel_predicate_info_t *sequel_query_analysis_predicates(const sequel_query_analysis_t *analysis, size_t *count);

/** Return the optional normalized summary owned by the analysis object. */
const sequel_text_result_t *sequel_query_analysis_summary(const sequel_query_analysis_t *analysis);

/** Return the optional sanitized SQL owned by the analysis object. */
const sequel_text_result_t *sequel_query_analysis_sanitized_sql(const sequel_query_analysis_t *analysis);

/** Return the optional formatted SQL owned by the analysis object. */
const sequel_text_result_t *sequel_query_analysis_formatted_sql(const sequel_query_analysis_t *analysis);

/** Copy the optional requested fingerprint into a caller-provided buffer. */
sequel_status_t sequel_query_analysis_fingerprint(const sequel_query_analysis_t *analysis,
                                                  char out[SEQUEL_FINGERPRINT_LENGTH]);

/** Return the original-to-sanitized provenance map when requested. */
const sequel_analysis_source_map_t *sequel_query_analysis_sanitized_map(const sequel_query_analysis_t *analysis);

/** Return the sanitized-to-formatted provenance map when requested. */
const sequel_analysis_source_map_t *sequel_query_analysis_formatted_map(const sequel_query_analysis_t *analysis);
