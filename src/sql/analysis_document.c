#include <stdlib.h>

#include <sequel/analysis.h>

struct sequel_query_analysis {
  sequel_query_document_t *query;
  sequel_query_analysis_options_t options;
};

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
  *out = analysis;
  return SEQUEL_OK;
}

void sequel_query_analysis_free(sequel_query_analysis_t *analysis) {
  if (!analysis)
    return;
  sequel_query_document_free(analysis->query);
  free(analysis);
}

const sequel_query_document_t *sequel_query_analysis_query(const sequel_query_analysis_t *analysis) {
  return analysis ? analysis->query : NULL;
}
