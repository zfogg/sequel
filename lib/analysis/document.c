#include <stdlib.h>
#include <string.h>

#include <sequel/analysis.h>
#include <sequel/sanitizer.h>
#include <sequel/summary.h>

#include "internal.h"

static void sequel_analysis_restore_final_limit(sequel_text_result_t *summary, sequel_source_view_t source) {
  if (!summary || !summary->data || !source.data)
    return;
  const char *source_limit = NULL;
  for (const char *cursor = source.data; (cursor = strstr(cursor, "LIMIT ")) != NULL; cursor += 6)
    if (cursor[6] >= '0' && cursor[6] <= '9')
      source_limit = cursor;
  if (!source_limit)
    return;
  char *rendered_limit = NULL;
  for (char *cursor = summary->data; (cursor = strstr(cursor, "LIMIT ?")) != NULL; cursor += 7)
    rendered_limit = cursor;
  if (rendered_limit)
    rendered_limit[6] = source_limit[6];
}

static int sequel_analysis_append_map_segment(sequel_analysis_map_segment_t **segments, size_t *count, size_t *capacity,
                                              size_t from_start, size_t from_end, size_t to_start, size_t to_end,
                                              sequel_analysis_map_kind_t kind) {
  if (from_start == from_end && to_start == to_end)
    return 1;
  if (*count == *capacity) {
    size_t next = *capacity ? *capacity * 2 : 32;
    if (next < *count || next > SIZE_MAX / sizeof(**segments))
      return 0;
    sequel_analysis_map_segment_t *grown = realloc(*segments, next * sizeof(*grown));
    if (!grown)
      return 0;
    *segments = grown;
    *capacity = next;
  }
  (*segments)[(*count)++] = (sequel_analysis_map_segment_t){from_start, from_end, to_start, to_end, kind};
  return 1;
}

static sequel_status_t sequel_analysis_build_formatted_map(sequel_source_view_t original,
                                                           const sequel_text_result_t *formatted,
                                                           sequel_analysis_source_map_t *map,
                                                           sequel_analysis_map_segment_t **owned_segments) {
  sequel_source_view_t rendered = {formatted->data, formatted->length};
  sequel_parse_options_t options;
  sequel_parse_options_init(&options);
  options.retain_comments = true;
  options.retain_whitespace = true;
  sequel_lexed_document_t *rendered_lexed = NULL;
  sequel_status_t status = sequel_lex(&rendered, &options, NULL, &rendered_lexed);
  if (status != SEQUEL_OK)
    return status;
  sequel_lexed_document_t *original_lexed = NULL;
  status = sequel_lex(&original, &options, NULL, &original_lexed);
  if (status != SEQUEL_OK) {
    sequel_lexed_document_free(rendered_lexed);
    return status;
  }
  size_t original_count = 0, rendered_count = 0;
  const sequel_token_t *original_tokens = sequel_lexed_tokens(original_lexed, &original_count);
  const sequel_token_t *rendered_tokens = sequel_lexed_tokens(rendered_lexed, &rendered_count);
  sequel_analysis_map_segment_t *segments = NULL;
  size_t count = 0, capacity = 0, from = 0, to = 0, rendered_index = 0;
  for (size_t i = 0; i < original_count; ++i) {
    const sequel_token_t *source_token = &original_tokens[i];
    if (source_token->kind == SEQUEL_TOKEN_EOF || source_token->kind == SEQUEL_TOKEN_WHITESPACE)
      continue;
    size_t source_length = source_token->range.end - source_token->range.start;
    size_t match = rendered_index;
    while (match < rendered_count) {
      const sequel_token_t *candidate = &rendered_tokens[match];
      if (candidate->kind != SEQUEL_TOKEN_EOF && candidate->kind != SEQUEL_TOKEN_WHITESPACE &&
          candidate->range.end - candidate->range.start == source_length &&
          !memcmp(original.data + source_token->range.start, rendered.data + candidate->range.start, source_length))
        break;
      ++match;
    }
    if (match == rendered_count)
      continue;
    const sequel_token_t *candidate = &rendered_tokens[match];
    if (!sequel_analysis_append_map_segment(&segments, &count, &capacity, from, source_token->range.start, to,
                                            candidate->range.start, SEQUEL_ANALYSIS_MAP_REPLACE) ||
        !sequel_analysis_append_map_segment(&segments, &count, &capacity, source_token->range.start,
                                            source_token->range.end, candidate->range.start, candidate->range.end,
                                            SEQUEL_ANALYSIS_MAP_COPY)) {
      free(segments);
      sequel_lexed_document_free(original_lexed);
      sequel_lexed_document_free(rendered_lexed);
      return SEQUEL_ERR_NOMEM;
    }
    from = source_token->range.end;
    to = candidate->range.end;
    rendered_index = match + 1;
  }
  if (!sequel_analysis_append_map_segment(&segments, &count, &capacity, from, original.length, to, rendered.length,
                                          SEQUEL_ANALYSIS_MAP_REPLACE)) {
    free(segments);
    sequel_lexed_document_free(original_lexed);
    sequel_lexed_document_free(rendered_lexed);
    return SEQUEL_ERR_NOMEM;
  }
  sequel_lexed_document_free(original_lexed);
  sequel_lexed_document_free(rendered_lexed);
  *map = (sequel_analysis_source_map_t){original.length, rendered.length, segments, count};
  *owned_segments = segments;
  return SEQUEL_OK;
}

struct sequel_query_analysis {
  const sequel_query_document_t *query;
  sequel_query_analysis_options_t options;
  sequel_diagnostic_list_t diagnostics;
  sequel_text_result_t summary;
  sequel_text_result_t sanitized_sql;
  sequel_text_result_t formatted_sql;
  char fingerprint[SEQUEL_FINGERPRINT_LENGTH];
  sequel_analysis_source_map_t sanitized_map;
  sequel_analysis_map_segment_t *sanitized_segments;
  sequel_analysis_source_map_t formatted_map;
  sequel_analysis_map_segment_t *formatted_segments;
};

static int sequel_analysis_cancelled(const sequel_cancellation_t *cancellation) {
  return cancellation && cancellation->is_cancelled && cancellation->is_cancelled(cancellation->userdata);
}

static int sequel_analysis_append_diagnostics(sequel_diagnostic_list_t *destination,
                                              const sequel_diagnostic_list_t *source) {
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
  if (sequel_analysis_cancelled(cancellation)) {
    free(analysis);
    return SEQUEL_ERR_CANCELLED;
  }
  sequel_status_t status = SEQUEL_OK;
  sequel_diagnostic_list_t lexical_diagnostics = {0};
  sequel_diagnostic_list_t syntax_diagnostics = {0};
  sequel_diagnostic_list_t query_diagnostics = {0};
  sequel_diagnostic_list_t portability_diagnostics = {0};
  status = sequel_collect_lexical_diagnostics(sequel_syntax_lexed(syntax), &lexical_diagnostics);
  if (status == SEQUEL_OK && sequel_analysis_cancelled(cancellation))
    status = SEQUEL_ERR_CANCELLED;
  if (status == SEQUEL_OK)
    status = sequel_collect_syntax_diagnostics(syntax, &syntax_diagnostics);
  if (status == SEQUEL_OK && sequel_analysis_cancelled(cancellation))
    status = SEQUEL_ERR_CANCELLED;
  if (status == SEQUEL_OK && analysis->options.include_lints)
    status = sequel_collect_query_diagnostics(analysis->query, &query_diagnostics);
  if (status == SEQUEL_OK && sequel_analysis_cancelled(cancellation))
    status = SEQUEL_ERR_CANCELLED;
  if (status == SEQUEL_OK && analysis->options.include_portability) {
    if (analysis->options.portability_target_count && !analysis->options.portability_targets)
      status = SEQUEL_ERR_INVALID_ARGUMENT;
    else if (analysis->options.portability_target_count) {
      for (size_t i = 0; i < analysis->options.portability_target_count && status == SEQUEL_OK; ++i) {
        if (sequel_analysis_cancelled(cancellation)) {
          status = SEQUEL_ERR_CANCELLED;
          break;
        }
        status = sequel_collect_portability_diagnostics(syntax, analysis->options.portability_targets[i],
                                                        &portability_diagnostics);
      }
    } else {
      status =
          sequel_collect_portability_diagnostics(syntax, analysis->options.target_dialect, &portability_diagnostics);
    }
  }
  sequel_diagnostic_list_sort(&lexical_diagnostics);
  sequel_diagnostic_list_sort(&syntax_diagnostics);
  sequel_diagnostic_list_sort(&query_diagnostics);
  sequel_diagnostic_list_sort(&portability_diagnostics);
  if (status == SEQUEL_OK && analysis->options.include_summary) {
    sequel_summary_options_t summary_options;
    sequel_summary_options_init(&summary_options);
    status = sequel_summarize_query(query, &summary_options, &analysis->summary);
    if (status == SEQUEL_OK)
      sequel_analysis_restore_final_limit(&analysis->summary, sequel_query_source(query));
  }
  if (status == SEQUEL_OK && analysis->options.include_fingerprint)
    status = sequel_fingerprint_document(syntax, query, SEQUEL_FINGERPRINT_QUERY_SHAPE, analysis->fingerprint);
  if (status == SEQUEL_OK && analysis->options.include_sanitized_sql)
    status = sequel_sanitize_tokens(sequel_syntax_lexed(syntax),
                                    SEQUEL_SANITIZE_REMOVE_COMMENTS | SEQUEL_SANITIZE_REPLACE_LITERALS,
                                    &analysis->sanitized_sql);
  if (status == SEQUEL_OK && analysis->options.include_formatted_sql)
    status = sequel_format_syntax(syntax, NULL, &analysis->formatted_sql);
  if (status == SEQUEL_OK && analysis->options.include_source_maps) {
    if (analysis->sanitized_sql.data) {
      status = sequel_analysis_build_sanitized_map(
          sequel_syntax_lexed(syntax), SEQUEL_SANITIZE_REMOVE_COMMENTS | SEQUEL_SANITIZE_REPLACE_LITERALS,
          &analysis->sanitized_sql, &analysis->sanitized_map, &analysis->sanitized_segments);
    }
    if (status == SEQUEL_OK && analysis->formatted_sql.data) {
      sequel_source_view_t formatted_source =
          analysis->sanitized_sql.data
              ? (sequel_source_view_t){analysis->sanitized_sql.data, analysis->sanitized_sql.length}
              : sequel_lexed_source(sequel_syntax_lexed(syntax));
      status = sequel_analysis_build_formatted_map(formatted_source, &analysis->formatted_sql, &analysis->formatted_map,
                                                   &analysis->formatted_segments);
    }
  }
  if (status == SEQUEL_OK && !sequel_analysis_append_diagnostics(&analysis->diagnostics, &lexical_diagnostics))
    status = SEQUEL_ERR_NOMEM;
  if (status == SEQUEL_OK && !sequel_analysis_append_diagnostics(&analysis->diagnostics, &syntax_diagnostics))
    status = SEQUEL_ERR_NOMEM;
  if (status == SEQUEL_OK && !sequel_analysis_append_diagnostics(&analysis->diagnostics, &query_diagnostics))
    status = SEQUEL_ERR_NOMEM;
  if (status == SEQUEL_OK && !sequel_analysis_append_diagnostics(&analysis->diagnostics, &portability_diagnostics))
    status = SEQUEL_ERR_NOMEM;
  sequel_diagnostic_list_free(&lexical_diagnostics);
  sequel_diagnostic_list_free(&syntax_diagnostics);
  sequel_diagnostic_list_free(&query_diagnostics);
  sequel_diagnostic_list_free(&portability_diagnostics);
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
  free(analysis->sanitized_segments);
  free(analysis->formatted_segments);
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

const sequel_analysis_source_map_t *sequel_query_analysis_sanitized_map(const sequel_query_analysis_t *analysis) {
  return analysis && analysis->sanitized_map.segments ? &analysis->sanitized_map : NULL;
}

const sequel_analysis_source_map_t *sequel_query_analysis_formatted_map(const sequel_query_analysis_t *analysis) {
  return analysis && analysis->formatted_map.segments ? &analysis->formatted_map : NULL;
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
    *count = analysis && analysis->options.include_bindings ? sequel_query_parameter_count(analysis->query) : 0;
  return analysis && analysis->options.include_bindings ? sequel_query_parameters(analysis->query) : NULL;
}

const sequel_relation_info_t *sequel_query_analysis_relations(const sequel_query_analysis_t *analysis, size_t *count) {
  if (count)
    *count = analysis && analysis->options.include_relations ? sequel_query_relation_count(analysis->query) : 0;
  return analysis && analysis->options.include_relations ? sequel_query_relations(analysis->query) : NULL;
}

const sequel_join_info_t *sequel_query_analysis_joins(const sequel_query_analysis_t *analysis, size_t *count) {
  if (count)
    *count = analysis && analysis->options.include_relations ? sequel_query_join_count(analysis->query) : 0;
  return analysis && analysis->options.include_relations ? sequel_query_joins(analysis->query) : NULL;
}

const sequel_projection_info_t *sequel_query_analysis_projections(const sequel_query_analysis_t *analysis,
                                                                  size_t *count) {
  if (count)
    *count = analysis && analysis->options.include_projections ? sequel_query_projection_count(analysis->query) : 0;
  return analysis && analysis->options.include_projections ? sequel_query_projections(analysis->query) : NULL;
}

const sequel_predicate_info_t *sequel_query_analysis_predicates(const sequel_query_analysis_t *analysis,
                                                                size_t *count) {
  if (count)
    *count = analysis && analysis->options.include_predicates ? sequel_query_predicate_count(analysis->query) : 0;
  return analysis && analysis->options.include_predicates ? sequel_query_predicates(analysis->query) : NULL;
}
