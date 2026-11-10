#include <stdlib.h>
#include <string.h>

#include <sequel/analysis.h>
#include <sequel/sanitizer.h>
#include <sequel/summary.h>

struct sequel_query_analysis {
  const sequel_query_document_t *query;
  sequel_query_analysis_options_t options;
  sequel_diagnostic_list_t diagnostics;
  sequel_text_result_t summary;
  sequel_text_result_t sanitized_sql;
  sequel_text_result_t formatted_sql;
  char fingerprint[SEQUEL_FINGERPRINT_LENGTH];
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
                                               .include_lints = true,
                                               .include_portability = false,
                                               .include_summary = true,
                                               .include_fingerprint = true,
                                               .include_sanitized_sql = false,
                                               .include_formatted_sql = false,
                                               .include_source_maps = false,
                                               .target_dialect = SEQUEL_GENERIC};
}

sequel_status_t sequel_analyze_document(const sequel_syntax_document_t *syntax, const sequel_query_document_t *query,
                                        const sequel_query_analysis_options_t *options,
                                        const sequel_cancellation_t *cancellation, sequel_query_analysis_t **out) {
  if (!syntax || !query || !out)
    return SEQUEL_ERR_NULL;
  *out = NULL;
  sequel_query_analysis_t *analysis = calloc(1, sizeof(*analysis));
  if (!analysis)
    return SEQUEL_ERR_NOMEM;
  sequel_query_analysis_options_init(&analysis->options);
  if (options)
    analysis->options = *options;
  analysis->query = query;
  if (cancellation && cancellation->is_cancelled && cancellation->is_cancelled(cancellation->userdata)) {
    free(analysis);
    return SEQUEL_ERR_CANCELLED;
  }
  sequel_status_t status = SEQUEL_OK;
  sequel_diagnostic_list_t lexical_diagnostics = {0};
  sequel_diagnostic_list_t syntax_diagnostics = {0};
  sequel_diagnostic_list_t query_diagnostics = {0};
  status = sequel_collect_lexical_diagnostics(sequel_syntax_lexed(syntax), &lexical_diagnostics);
  if (status == SEQUEL_OK)
    status = sequel_collect_syntax_diagnostics(syntax, &syntax_diagnostics);
  if (status == SEQUEL_OK && analysis->options.include_lints)
    status = sequel_collect_query_diagnostics(analysis->query, &query_diagnostics);
  if (status == SEQUEL_OK && analysis->options.include_summary) {
    sequel_summary_options_t summary_options;
    sequel_summary_options_init(&summary_options);
    status = sequel_summarize_query(query, &summary_options, &analysis->summary);
  }
  if (status == SEQUEL_OK && analysis->options.include_fingerprint)
    status = sequel_fingerprint_document(syntax, query, SEQUEL_FINGERPRINT_QUERY_SHAPE, analysis->fingerprint);
  if (status == SEQUEL_OK && analysis->options.include_sanitized_sql)
    status = sequel_sanitize_tokens(sequel_syntax_lexed(syntax), SEQUEL_SANITIZE_REMOVE_COMMENTS |
                                                               SEQUEL_SANITIZE_REPLACE_LITERALS,
                                     &analysis->sanitized_sql);
  if (status == SEQUEL_OK && analysis->options.include_formatted_sql)
    status = sequel_format_syntax(syntax, NULL, &analysis->formatted_sql);
  if (status == SEQUEL_OK && !append_diagnostics(&analysis->diagnostics, &lexical_diagnostics))
    status = SEQUEL_ERR_NOMEM;
  if (status == SEQUEL_OK && !append_diagnostics(&analysis->diagnostics, &syntax_diagnostics))
    status = SEQUEL_ERR_NOMEM;
  if (status == SEQUEL_OK && !append_diagnostics(&analysis->diagnostics, &query_diagnostics))
    status = SEQUEL_ERR_NOMEM;
  sequel_diagnostic_list_free(&lexical_diagnostics);
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
  sequel_diagnostic_list_free(&analysis->diagnostics);
  sequel_text_result_free(&analysis->summary);
  sequel_text_result_free(&analysis->sanitized_sql);
  sequel_text_result_free(&analysis->formatted_sql);
  free(analysis);
}

const sequel_text_result_t *sequel_query_analysis_summary(const sequel_query_analysis_t *analysis) {
  return analysis && analysis->summary.data ? &analysis->summary : NULL;
}

const sequel_text_result_t *sequel_query_analysis_sanitized_sql(const sequel_query_analysis_t *analysis) {
  return analysis && analysis->sanitized_sql.data ? &analysis->sanitized_sql : NULL;
}

const sequel_text_result_t *sequel_query_analysis_formatted_sql(const sequel_query_analysis_t *analysis) {
  return analysis && analysis->formatted_sql.data ? &analysis->formatted_sql : NULL;
}

sequel_status_t sequel_query_analysis_fingerprint(const sequel_query_analysis_t *analysis,
                                                  char out[SEQUEL_FINGERPRINT_LENGTH]) {
  if (!analysis || !out)
    return SEQUEL_ERR_NULL;
  if (!analysis->fingerprint[0]) {
    out[0] = 0;
    return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  memcpy(out, analysis->fingerprint, sizeof(analysis->fingerprint));
  return SEQUEL_OK;
}

const sequel_query_document_t *sequel_query_analysis_query(const sequel_query_analysis_t *analysis) {
  return analysis ? analysis->query : NULL;
}

const sequel_diagnostic_list_t *sequel_query_analysis_diagnostics(const sequel_query_analysis_t *analysis) {
  return analysis ? &analysis->diagnostics : NULL;
}

const sequel_parameter_info_t *sequel_query_analysis_parameters(const sequel_query_analysis_t *analysis,
                                                                size_t *count) {
  if (count)
    *count = analysis ? sequel_query_parameter_count(analysis->query) : 0;
  return analysis ? sequel_query_parameters(analysis->query) : NULL;
}
