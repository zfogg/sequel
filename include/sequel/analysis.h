#pragma once

#include <stdbool.h>

#include <sequel/query.h>
#include <sequel/diagnostics.h>
#include <sequel/fingerprint.h>
#include <sequel/formatter.h>

typedef struct sequel_query_analysis sequel_query_analysis_t;

/* Legacy analysis ABI retained in the analysis domain while callers migrate
 * to sequel_query_analysis_t.  Keep these layouts stable for bindings. */
#ifndef SEQUEL_ANALYSIS_LEGACY_TYPES
#define SEQUEL_ANALYSIS_LEGACY_TYPES
#define SEQUEL_ANALYSIS_VERSION 1u
#define SEQUEL_DIAGNOSTIC_VERSION 1u

typedef struct {
  size_t offset, end_offset;
} sequel_range_t;

typedef enum { SEQUEL_MAP_COPY, SEQUEL_MAP_REPLACE, SEQUEL_MAP_DELETE, SEQUEL_MAP_INSERT } sequel_map_kind_t;

typedef struct {
  size_t from_start, from_end, to_start, to_end;
  sequel_map_kind_t kind;
} sequel_map_segment_t;

typedef struct {
  size_t from_length, to_length;
  const sequel_map_segment_t *segments;
  size_t count;
} sequel_source_map_t;

typedef struct {
  size_t start, end;
  bool exact;
} sequel_mapped_range_t;

typedef bool (*sequel_cancel_fn)(void *userdata);

typedef struct {
  sequel_dialect_t dialect;
  size_t max_input_bytes, max_output_bytes, max_memory_bytes, max_work_units, max_query_text_bytes;
  bool omit_query_text, format, keep_in_lists, omit_source_maps, omit_bind_descriptors, include_lints;
  sequel_cancel_fn cancelled;
  void *cancel_userdata;
} sequel_analysis_options_t;

typedef struct {
  unsigned schema_version;
  sequel_dialect_t dialect;
  const char *status, *omission_reason, *text, *summary, *formatted;
  char operation[32];
  char fingerprint[96];
  bool truncated;
  size_t statement_count, original_length;
  const sequel_diagnostic_t *diagnostics;
  size_t diagnostic_count;
  const sequel_bind_descriptor_t *binds;
  size_t bind_count;
  const sequel_range_t *statements;
  sequel_source_map_t original_to_sanitized, sanitized_to_formatted;
  size_t memory_peak_bytes, work_units;
  void *_storage;
  sequel_diagnostic_t _failure_diagnostic;
} sequel_analysis_t;
#endif

sequel_status_t sequel_analyze_n(const char *sql, size_t length, const sequel_analysis_options_t *options,
                                 sequel_analysis_t *out);
sequel_status_t sequel_analyze(const char *sql, const sequel_analysis_options_t *options, sequel_analysis_t *out);
void sequel_analysis_free(sequel_analysis_t *out);

sequel_status_t sequel_source_map_range(const sequel_source_map_t *map, size_t start, size_t end, bool reverse,
                                        sequel_mapped_range_t *out);
sequel_status_t sequel_utf8_to_utf16(const char *text, size_t length, size_t offset, size_t *units);
sequel_status_t sequel_utf16_to_utf8(const char *text, size_t length, size_t units, size_t *offset);

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

/** Return joins when relation collection was requested. */
const sequel_join_info_t *sequel_query_analysis_joins(const sequel_query_analysis_t *analysis, size_t *count);

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
