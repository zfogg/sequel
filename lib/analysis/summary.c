#include <stdlib.h>
#include <string.h>

#include "analysis_internal.h"

void analysis_summary_from_query(const sequel_query_document_t *query, sequel_analysis_t *out) {
  sequel_summary_options_t options;
  sequel_summary_options_init(&options);
  options.include_bind_count = false;
  sequel_text_result_t summary = {0};
  if (sequel_summarize_query(query, &options, &summary) != SEQUEL_OK)
    sequel_abort(SEQUEL_ERR_NOMEM);
  char *copy = malloc(summary.length + 1);
  if (!copy)
    sequel_abort(SEQUEL_ERR_NOMEM);
  if (summary.length && summary.data)
    memcpy(copy, summary.data, summary.length);
  copy[summary.length] = 0;
  out->summary = copy;
  sequel_text_result_free(&summary);
}

void analysis_summary_default(sequel_analysis_t *out) {
  static const char fallback[] = "SQL";
  char *summary = malloc(sizeof(fallback));
  if (!summary)
    sequel_abort(SEQUEL_ERR_NOMEM);
  memcpy(summary, fallback, sizeof(fallback));
  out->summary = summary;
}
