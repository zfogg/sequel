#pragma once

#include <stdbool.h>

#include <sequel/query.h>

typedef struct sequel_query_analysis sequel_query_analysis_t;

typedef struct {
  bool include_bindings;
  bool include_relations;
  bool include_projections;
  bool include_predicates;
  bool include_lints;
} sequel_query_analysis_options_t;

/** Initialize document-analysis options with all semantic collections enabled. */
void sequel_query_analysis_options_init(sequel_query_analysis_options_t *options);

/** Build an owned semantic analysis object from an immutable syntax document. */
sequel_status_t sequel_analyze_document(const sequel_syntax_document_t *syntax,
                                        const sequel_query_analysis_options_t *options, sequel_query_analysis_t **out);

/** Release a document analysis object. NULL is permitted. */
void sequel_query_analysis_free(sequel_query_analysis_t *analysis);

/** Return the normalized query document owned by the analysis object. */
const sequel_query_document_t *sequel_query_analysis_query(const sequel_query_analysis_t *analysis);
