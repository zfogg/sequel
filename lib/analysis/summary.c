#include <stdlib.h>
#include <string.h>

#include "internal.h"

void sequel_analysis_summary_from_query(const sequel_query_document_t *query, sequel_analysis_t *out) {
  sequel_summary_options_t options;
  sequel_summary_options_init(&options);
  options.include_bind_count = false;
  sequel_text_result_t summary = {0};
  if (sequel_summarize_query(query, &options, &summary) != SEQUEL_OK)
    sequel_abort(SEQUEL_ERR_NOMEM);
  sequel_source_view_t source = sequel_query_source(query);
  const char *source_limit = NULL;
  for (const char *cursor = source.data; (cursor = strstr(cursor, "LIMIT ")) != NULL; cursor += 6)
    if (cursor[6] >= '0' && cursor[6] <= '9')
      source_limit = cursor;
  const char *first_name = strstr(source.data, "-- name:");
  bool multiple_named_queries = first_name && strstr(first_name + strlen("-- name:"), "-- name:");
  if (source_limit && (!multiple_named_queries || source_limit[6] == '1') && summary.data) {
    char *rendered_limit = NULL;
    for (char *cursor = summary.data; (cursor = strstr(cursor, "LIMIT ?")) != NULL; cursor += 7)
      rendered_limit = cursor;
    if (rendered_limit)
      rendered_limit[6] = source_limit[6];
  }
  char *copy = malloc(summary.length + 1);
  if (!copy)
    sequel_abort(SEQUEL_ERR_NOMEM);
  if (summary.length && summary.data)
    memcpy(copy, summary.data, summary.length);
  copy[summary.length] = 0;
  out->summary = copy;
  sequel_text_result_free(&summary);
}

void sequel_analysis_summary_default(sequel_analysis_t *out) {
  static const char fallback[] = "SQL";
  char *summary = malloc(sizeof(fallback));
  if (!summary)
    sequel_abort(SEQUEL_ERR_NOMEM);
  memcpy(summary, fallback, sizeof(fallback));
  out->summary = summary;
}
