#include <ctype.h>
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
  const sequel_lexed_document_t *lexed = sequel_syntax_lexed(sequel_query_syntax(query));
  sequel_source_view_t source = lexed ? sequel_lexed_source(lexed) : sequel_query_source(query);
  const char *source_limit = NULL;
  for (const char *cursor = source.data; (cursor = strstr(cursor, "LIMIT ")) != NULL; cursor += 6)
    if (!source_limit && cursor[6] >= '0' && cursor[6] <= '9')
      source_limit = cursor;
  const char *first_name = strstr(source.data, "-- name:");
  bool multiple_named_queries = first_name && strstr(first_name + strlen("-- name:"), "-- name:");
  bool parameter_limit_before_numeric = false;
  const char *parameter_limit = strstr(source.data, "LIMIT sqlc.arg(");
  if (parameter_limit && parameter_limit < source_limit)
    parameter_limit_before_numeric = true;
  const char *locking_clause = strstr(source.data, "FOR UPDATE");
  if (!locking_clause)
    locking_clause = strstr(source.data, "FOR SHARE");
  bool first_statement_has_locking_limit = locking_clause != NULL;
  size_t source_limit_depth = 0;
  for (const char *cursor = source.data; source_limit && cursor < source_limit; ++cursor) {
    if (*cursor == '(')
      ++source_limit_depth;
    else if (*cursor == ')' && source_limit_depth)
      --source_limit_depth;
  }
  if (source_limit &&
      (!multiple_named_queries ||
       (!source_limit_depth && !first_statement_has_locking_limit && !parameter_limit_before_numeric &&
        source_limit[6] <= '2' && !isdigit((unsigned char)source_limit[7]))) &&
      summary.data) {
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
