#include <stdlib.h>

#include "internal.h"

void sequel_analysis_segment(analysis_map *map, size_t from_start, size_t from_end, size_t to_start, size_t to_end,
                             sequel_map_kind_t kind) {
  if (from_start == from_end && to_start == to_end)
    return;
  if (map->n) {
    sequel_map_segment_t *last = &map->p[map->n - 1];
    if (last->kind == kind && last->from_end == from_start && last->to_end == to_start && kind != SEQUEL_MAP_REPLACE) {
      last->from_end = from_end;
      last->to_end = to_end;
      return;
    }
  }
  if (map->n == map->capacity) {
    size_t next = map->capacity ? map->capacity * 2 : 16;
    if (next < map->capacity || next > SIZE_MAX / sizeof(*map->p))
      sequel_abort(SEQUEL_ERR_LIMIT);
    map->p = realloc(map->p, next * sizeof(*map->p));
    map->capacity = next;
  }
  sequel_output_tick(sizeof(*map->p));
  map->p[map->n++] = (sequel_map_segment_t){from_start, from_end, to_start, to_end, kind};
}

void sequel_analysis_build_format_map(analysis_map *map, const char *source, size_t source_length,
                                      const char *formatted, size_t formatted_length) {
  size_t source_cursor = 0, formatted_cursor = 0;
  while (source_cursor < source_length && formatted_cursor < formatted_length) {
    if (source[source_cursor] != formatted[formatted_cursor]) {
      if (isspace((unsigned char)source[source_cursor]) && isspace((unsigned char)formatted[formatted_cursor])) {
        size_t source_start = source_cursor;
        size_t formatted_start = formatted_cursor;
        while (source_cursor < source_length && isspace((unsigned char)source[source_cursor]))
          ++source_cursor;
        while (formatted_cursor < formatted_length && isspace((unsigned char)formatted[formatted_cursor]))
          ++formatted_cursor;
        sequel_analysis_segment(map, source_start, source_cursor, formatted_start, formatted_cursor,
                                SEQUEL_MAP_REPLACE);
        continue;
      }
      const char *source_match =
          memchr(source + source_cursor, (unsigned char)formatted[formatted_cursor], source_length - source_cursor);
      const char *formatted_match =
          memchr(formatted + formatted_cursor, (unsigned char)source[source_cursor], formatted_length - formatted_cursor);
      if (source_match && (!formatted_match || source_match - source < formatted_match - formatted)) {
        sequel_analysis_segment(map, source_cursor, (size_t)(source_match - source), formatted_cursor, formatted_cursor,
                                SEQUEL_MAP_DELETE);
        source_cursor = (size_t)(source_match - source);
        continue;
      }
      if (formatted_match) {
        sequel_analysis_segment(map, source_cursor, source_cursor, formatted_cursor,
                                (size_t)(formatted_match - formatted), SEQUEL_MAP_INSERT);
        formatted_cursor = (size_t)(formatted_match - formatted);
        continue;
      }
      sequel_analysis_segment(map, source_cursor, source_cursor + 1, formatted_cursor, formatted_cursor + 1,
                              SEQUEL_MAP_REPLACE);
      ++source_cursor;
      ++formatted_cursor;
      continue;
    }
    size_t run_start = source_cursor;
    while (source_cursor < source_length && formatted_cursor < formatted_length &&
           source[source_cursor] == formatted[formatted_cursor]) {
      ++source_cursor;
      ++formatted_cursor;
    }
    if (source_cursor > run_start)
      sequel_analysis_segment(map, run_start, source_cursor, formatted_cursor - (source_cursor - run_start),
                              formatted_cursor, SEQUEL_MAP_COPY);
  }
  sequel_analysis_segment(map, source_cursor, source_length, formatted_cursor, formatted_length,
                          source_cursor == source_length         ? SEQUEL_MAP_INSERT
                          : formatted_cursor == formatted_length ? SEQUEL_MAP_DELETE
                                                                 : SEQUEL_MAP_REPLACE);
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

sequel_status_t sequel_analysis_build_formatted_map(sequel_source_view_t original,
                                                    const sequel_text_result_t *formatted,
                                                    sequel_analysis_source_map_t *map,
                                                    sequel_analysis_map_segment_t **owned_segments) {
  sequel_parse_options_t options;
  sequel_parse_options_init(&options);
  options.retain_comments = true;
  options.retain_whitespace = true;
  sequel_lexed_document_t *rendered_lexed = NULL;
  sequel_source_view_t rendered = {formatted->data, formatted->length};
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

sequel_status_t sequel_analysis_build_sanitized_map(const sequel_lexed_document_t *lexed, unsigned flags,
                                                    sequel_text_result_t *sanitized, sequel_analysis_source_map_t *map,
                                                    sequel_analysis_map_segment_t **owned_segments) {
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  sequel_source_view_t source = sequel_lexed_source(lexed);
  size_t segment_count = 0, output_offset = 0;
  for (size_t i = 0; i < token_count; ++i) {
    const sequel_token_t *token = &tokens[i];
    if (token->kind == SEQUEL_TOKEN_EOF)
      break;
    if (!sequel_token_has_range(token, source.length))
      return SEQUEL_ERR_INVALID_ARGUMENT;
    if (segment_count == SIZE_MAX / sizeof(**owned_segments))
      return SEQUEL_ERR_OVERFLOW;
    ++segment_count;
    output_offset += ((flags & SEQUEL_SANITIZE_REMOVE_COMMENTS) && sequel_token_is_comment(token)) ? 0
                     : (flags & SEQUEL_SANITIZE_REPLACE_LITERALS) && sequel_token_is_literal(token)
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
    size_t source_length = token->range.end - token->range.start;
    bool removed = (flags & SEQUEL_SANITIZE_REMOVE_COMMENTS) && sequel_token_is_comment(token);
    bool replaced = (flags & SEQUEL_SANITIZE_REPLACE_LITERALS) && sequel_token_is_literal(token);
    size_t result_length = removed ? 0 : replaced ? 1 : source_length;
    segments[segment++] = (sequel_analysis_map_segment_t){token->range.start, token->range.end, output_offset,
                                                          output_offset + result_length,
                                                          removed    ? SEQUEL_ANALYSIS_MAP_DELETE
                                                          : replaced ? SEQUEL_ANALYSIS_MAP_REPLACE
                                                                     : SEQUEL_ANALYSIS_MAP_COPY};
    output_offset += result_length;
  }
  *owned_segments = segments;
  *map = (sequel_analysis_source_map_t){source.length, sanitized->length, segments, segment};
  return SEQUEL_OK;
}
