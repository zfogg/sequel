#include <stdlib.h>
#include <string.h>

#include "analysis_internal.h"

static int shared_token_kind(sequel_token_kind_t kind) {
  switch (kind) {
  case SEQUEL_TOKEN_LINE_COMMENT:
    return 3;
  case SEQUEL_TOKEN_BLOCK_COMMENT:
    return 4;
  case SEQUEL_TOKEN_IDENTIFIER:
  case SEQUEL_TOKEN_KEYWORD:
  case SEQUEL_TOKEN_BOOLEAN:
  case SEQUEL_TOKEN_NULL:
    return 1;
  case SEQUEL_TOKEN_QUOTED_IDENTIFIER:
  case SEQUEL_TOKEN_STRING:
  case SEQUEL_TOKEN_PARAMETER:
    return 2;
  default:
    return 0;
  }
}

sequel_status_t adapt_lexed_tokens(const sequel_lexed_document_t *lexed, format_token **out, size_t *count) {
  if (!lexed || !out || !count)
    return SEQUEL_ERR_NULL;
  *out = NULL;
  *count = 0;
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  sequel_source_view_t source = sequel_lexed_source(lexed);
  size_t capacity = 0;
  size_t previous_end = 0;
  bool gap = false;
  bool pending_newline = false;
  for (size_t i = 0; i < token_count; ++i) {
    const sequel_token_t *token = &tokens[i];
    if (token->kind == SEQUEL_TOKEN_EOF)
      break;
    if (token->range.end > source.length || token->range.start > token->range.end)
      goto invalid;
    if (token->kind == SEQUEL_TOKEN_WHITESPACE) {
      for (size_t offset = token->range.start; offset < token->range.end; ++offset)
        pending_newline |= source.data[offset] == '\r' || source.data[offset] == '\n';
      previous_end = token->range.end;
      gap = true;
      continue;
    }
    if (*count == capacity) {
      size_t next = capacity ? capacity * 2 : 64;
      if (next < *count || next > SIZE_MAX / sizeof(**out))
        goto overflow;
      format_token *grown = calloc(next, sizeof(*grown));
      if (!grown)
        goto no_memory;
      if (*out) {
        memcpy(grown, *out, *count * sizeof(*grown));
        free(*out);
      }
      *out = grown;
      capacity = next;
    }
    bool newline = false;
    for (size_t offset = previous_end; offset < token->range.start; ++offset)
      newline |= source.data[offset] == '\r' || source.data[offset] == '\n';
    int kind = shared_token_kind(token->kind);
    bool opening = kind == 0 && token->range.end == token->range.start + 1 &&
                   strchr("([{", source.data[token->range.start]) != NULL;
    bool previous_open = *count && (*out)[*count - 1].kind == 0 && (*out)[*count - 1].t.n == 1 &&
                         strchr("([{", *(*out)[*count - 1].t.p) != NULL;
    (*out)[(*count)++] = (format_token){.t = {.p = source.data + token->range.start,
                                              .n = token->range.end - token->range.start,
                                              .word = kind == 1,
                                              .space = gap,
                                              .clickhouse = sequel_lexed_dialect(lexed) == SEQUEL_CLICKHOUSE},
                                        .gap = gap,
                                        .newline = newline || pending_newline,
                                        .kind = kind,
                                        .matching = 0,
                                        .multiline = newline || (pending_newline && (!opening || previous_open)),
                                        .flat_width = (*count > 1 ? (*out)[*count - 2].flat_width : 0) +
                                                      token->range.end - token->range.start + 1,
                                        .source_offset = token->range.start,
                                        .rendered_offset = 0};
    previous_end = token->range.end;
    gap = false;
    pending_newline = false;
  }
  return SEQUEL_OK;
invalid:
  free(*out);
  *out = NULL;
  *count = 0;
  return SEQUEL_ERR_INVALID_ARGUMENT;
overflow:
  free(*out);
  *out = NULL;
  *count = 0;
  return SEQUEL_ERR_OVERFLOW;
no_memory:
  free(*out);
  *out = NULL;
  *count = 0;
  return SEQUEL_ERR_NOMEM;
}
