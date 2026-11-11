#include <stdlib.h>

#include <sequel/analysis.h>
#include <sequel/sanitizer.h>

#include "../resources.h"
#include "../tokens.h"

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
