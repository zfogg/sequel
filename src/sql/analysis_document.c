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
  sequel_analysis_source_map_t sanitized_map;
  sequel_analysis_map_segment_t *sanitized_segments;
  sequel_analysis_source_map_t formatted_map;
  sequel_analysis_map_segment_t formatted_segment;
};

static int append_diagnostic_record(sequel_diagnostic_list_t *diagnostics,
                                    sequel_diagnostic_record_t record) {
  if (diagnostics->count == diagnostics->capacity) {
    size_t capacity = diagnostics->capacity ? diagnostics->capacity * 2 : 8;
    if (capacity < diagnostics->count || capacity > SIZE_MAX / sizeof(*diagnostics->records))
      return 0;
    sequel_diagnostic_record_t *records = realloc(diagnostics->records, capacity * sizeof(*records));
    if (!records)
      return 0;
    diagnostics->records = records;
    diagnostics->capacity = capacity;
  }
  diagnostics->records[diagnostics->count++] = record;
  return 1;
}

static sequel_status_t collect_portability_diagnostics(const sequel_syntax_document_t *syntax,
                                                       sequel_dialect_t target,
                                                       sequel_diagnostic_list_t *diagnostics) {
  const sequel_lexed_document_t *lexed = sequel_syntax_lexed(syntax);
  if (sequel_lexed_dialect(lexed) == target)
    return SEQUEL_OK;
  size_t count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &count);
  for (size_t i = 0; i < count; ++i) {
    if (!(tokens[i].flags & SEQUEL_TOKEN_FLAG_DIALECT_SPECIFIC))
      continue;
    if (!append_diagnostic_record(diagnostics,
                                  (sequel_diagnostic_record_t){.origin = SEQUEL_DIAGNOSTIC_PORTABILITY,
                                                               .code = SEQUEL_DIAG_DIALECT_PORTABILITY,
                                                               .severity = SEQUEL_WARNING,
                                                               .range = tokens[i].range,
                                                               .statement_index = SIZE_MAX,
                                                               .syntax_node = SIZE_MAX,
                                                               .recovered = 0})) {
      sequel_diagnostic_list_free(diagnostics);
      return SEQUEL_ERR_NOMEM;
    }
  }
  return SEQUEL_OK;
}

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

static sequel_status_t build_sanitized_map(const sequel_lexed_document_t *lexed, unsigned flags,
                                           sequel_text_result_t *sanitized,
                                           sequel_analysis_source_map_t *map,
                                           sequel_analysis_map_segment_t **owned_segments) {
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  sequel_source_view_t source = sequel_lexed_source(lexed);
  size_t segment_count = 0;
  size_t output_offset = 0;
  for (size_t i = 0; i < token_count; ++i) {
    const sequel_token_t *token = &tokens[i];
    if (token->kind == SEQUEL_TOKEN_EOF)
      break;
    if ((flags & SEQUEL_SANITIZE_REMOVE_COMMENTS) &&
        (token->kind == SEQUEL_TOKEN_LINE_COMMENT || token->kind == SEQUEL_TOKEN_BLOCK_COMMENT))
      continue;
    if (token->range.end > source.length || token->range.start > token->range.end)
      return SEQUEL_ERR_INVALID_ARGUMENT;
    if (segment_count == SIZE_MAX / sizeof(**owned_segments))
      return SEQUEL_ERR_OVERFLOW;
    ++segment_count;
    output_offset += (flags & SEQUEL_SANITIZE_REPLACE_LITERALS) && (token->flags & SEQUEL_TOKEN_FLAG_LITERAL)
                         ? 1
                         : token->range.end - token->range.start;
  }
  sequel_analysis_map_segment_t *segments = calloc(segment_count ? segment_count : 1, sizeof(*segments));
  if (!segments)
    return SEQUEL_ERR_NOMEM;
  size_t segment = 0;
  output_offset = 0;
  for (size_t i = 0; i < token_count; ++i) {
    const sequel_token_t *token = &tokens[i];
    if (token->kind == SEQUEL_TOKEN_EOF)
      break;
    if ((flags & SEQUEL_SANITIZE_REMOVE_COMMENTS) &&
        (token->kind == SEQUEL_TOKEN_LINE_COMMENT || token->kind == SEQUEL_TOKEN_BLOCK_COMMENT))
      continue;
    size_t source_length = token->range.end - token->range.start;
    bool replaced = (flags & SEQUEL_SANITIZE_REPLACE_LITERALS) && (token->flags & SEQUEL_TOKEN_FLAG_LITERAL);
    size_t result_length = replaced ? 1 : source_length;
    segments[segment++] = (sequel_analysis_map_segment_t){
        token->range.start, token->range.end, output_offset, output_offset + result_length,
        replaced ? SEQUEL_ANALYSIS_MAP_REPLACE : SEQUEL_ANALYSIS_MAP_COPY};
    output_offset += result_length;
  }
  *owned_segments = segments;
  *map = (sequel_analysis_source_map_t){source.length, sanitized->length, segments, segment};
  return SEQUEL_OK;
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
  sequel_diagnostic_list_t portability_diagnostics = {0};
  status = sequel_collect_lexical_diagnostics(sequel_syntax_lexed(syntax), &lexical_diagnostics);
  if (status == SEQUEL_OK)
    status = sequel_collect_syntax_diagnostics(syntax, &syntax_diagnostics);
  if (status == SEQUEL_OK && analysis->options.include_lints)
    status = sequel_collect_query_diagnostics(analysis->query, &query_diagnostics);
  if (status == SEQUEL_OK && analysis->options.include_portability)
    status = collect_portability_diagnostics(syntax, analysis->options.target_dialect, &portability_diagnostics);
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
  if (status == SEQUEL_OK && analysis->options.include_source_maps) {
    if (analysis->sanitized_sql.data) {
      status = build_sanitized_map(sequel_syntax_lexed(syntax), SEQUEL_SANITIZE_REMOVE_COMMENTS |
                                                                    SEQUEL_SANITIZE_REPLACE_LITERALS,
                                   &analysis->sanitized_sql, &analysis->sanitized_map,
                                   &analysis->sanitized_segments);
    }
    if (status == SEQUEL_OK && analysis->formatted_sql.data) {
      analysis->formatted_segment = (sequel_analysis_map_segment_t){0, sequel_query_source(query).length, 0,
                                                                    analysis->formatted_sql.length,
                                                                    SEQUEL_ANALYSIS_MAP_REPLACE};
      analysis->formatted_map = (sequel_analysis_source_map_t){sequel_query_source(query).length,
                                                               analysis->formatted_sql.length,
                                                               &analysis->formatted_segment, 1};
    }
  }
  if (status == SEQUEL_OK && !append_diagnostics(&analysis->diagnostics, &lexical_diagnostics))
    status = SEQUEL_ERR_NOMEM;
  if (status == SEQUEL_OK && !append_diagnostics(&analysis->diagnostics, &syntax_diagnostics))
    status = SEQUEL_ERR_NOMEM;
  if (status == SEQUEL_OK && !append_diagnostics(&analysis->diagnostics, &query_diagnostics))
    status = SEQUEL_ERR_NOMEM;
  if (status == SEQUEL_OK && !append_diagnostics(&analysis->diagnostics, &portability_diagnostics))
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
    *count = analysis ? sequel_query_parameter_count(analysis->query) : 0;
  return analysis ? sequel_query_parameters(analysis->query) : NULL;
}
