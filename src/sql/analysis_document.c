#include <stdlib.h>
#include <string.h>

#include <sequel/analysis.h>

struct sequel_query_analysis {
  sequel_query_document_t *query;
  sequel_query_analysis_options_t options;
  sequel_diagnostic_list_t diagnostics;
};

static int append_diagnostics(sequel_diagnostic_list_t *destination, const sequel_diagnostic_list_t *source) {
  if (!source->count)
    return 1;
  if (source->count > SIZE_MAX - destination->count)
    return 0;
  size_t count = destination->count + source->count;
  if (count > SIZE_MAX / sizeof(*destination->records))
    return 0;
  sequel_diagnostic_record_t *records = realloc(destination->records, count * sizeof(*records));
  if (!records)
    return 0;
  memcpy(records + destination->count, source->records, source->count * sizeof(*records));
  destination->records = records;
  destination->count = count;
  destination->capacity = count;
  return 1;
}

void sequel_query_analysis_options_init(sequel_query_analysis_options_t *options) {
  if (!options)
    return;
  *options = (sequel_query_analysis_options_t){.include_bindings = true,
                                               .include_relations = true,
                                               .include_projections = true,
                                               .include_predicates = true,
                                               .include_lints = true};
}

sequel_status_t sequel_analyze_document(const sequel_syntax_document_t *syntax,
                                        const sequel_query_analysis_options_t *options, sequel_query_analysis_t **out) {
  if (!syntax || !out)
    return SEQUEL_ERR_NULL;
  *out = NULL;
  sequel_query_analysis_t *analysis = calloc(1, sizeof(*analysis));
  if (!analysis)
    return SEQUEL_ERR_NOMEM;
  sequel_query_analysis_options_init(&analysis->options);
  if (options)
    analysis->options = *options;
  sequel_parse_options_t parse_options;
  sequel_parse_options_init(&parse_options);
  sequel_status_t status = sequel_build_query_document(syntax, &parse_options, &analysis->query);
  if (status != SEQUEL_OK) {
    free(analysis);
    return status;
  }
  sequel_diagnostic_list_t syntax_diagnostics = {0};
  sequel_diagnostic_list_t query_diagnostics = {0};
  status = sequel_collect_syntax_diagnostics(syntax, &syntax_diagnostics);
  if (status == SEQUEL_OK && analysis->options.include_lints)
    status = sequel_collect_query_diagnostics(analysis->query, &query_diagnostics);
  if (status == SEQUEL_OK && !append_diagnostics(&analysis->diagnostics, &syntax_diagnostics))
    status = SEQUEL_ERR_NOMEM;
  if (status == SEQUEL_OK && !append_diagnostics(&analysis->diagnostics, &query_diagnostics))
    status = SEQUEL_ERR_NOMEM;
  sequel_diagnostic_list_free(&syntax_diagnostics);
  sequel_diagnostic_list_free(&query_diagnostics);
  if (status != SEQUEL_OK) {
    sequel_query_analysis_free(analysis);
    return status;
  }
  *out = analysis;
  return SEQUEL_OK;
}

void sequel_query_analysis_free(sequel_query_analysis_t *analysis) {
  if (!analysis)
    return;
  sequel_query_document_free(analysis->query);
  sequel_diagnostic_list_free(&analysis->diagnostics);
  free(analysis);
}

const sequel_query_document_t *sequel_query_analysis_query(const sequel_query_analysis_t *analysis) {
  return analysis ? analysis->query : NULL;
}

const sequel_diagnostic_list_t *sequel_query_analysis_diagnostics(const sequel_query_analysis_t *analysis) {
  return analysis ? &analysis->diagnostics : NULL;
}
