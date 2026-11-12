#include <stdlib.h>
#include <string.h>

#include "internal.h"

bool sequel_analysis_word(analysis_token_t token, const char *word) {
  size_t length = strlen(word);
  if (token.kind != 1 || token.t.n != length)
    return false;
  for (size_t i = 0; i < length; ++i)
    if (toupper((unsigned char)token.t.p[i]) != toupper((unsigned char)word[i]))
      return false;
  return true;
}

void sequel_analysis_mark_in_list(analysis_token_t *tokens, size_t count, size_t keyword_index,
                                  sequel_dialect_t dialect, bool keep_in_lists) {
  if (keep_in_lists || !sequel_analysis_word(tokens[keyword_index], "IN"))
    return;
  size_t open = keyword_index + 1;
  while (open < count && tokens[open].kind >= 3)
    open++;
  if (open >= count || !sequel_runtime_symbol(tokens[open].t, '('))
    return;
  size_t item = open + 1, items = 0;
  bool expecting = true;
  for (; item < count; item++) {
    sequel_work_tick(1);
    if (tokens[item].kind >= 3)
      continue;
    if (sequel_runtime_symbol(tokens[item].t, ')'))
      break;
    if (expecting) {
      const char *key;
      size_t length, number;
      sequel_bind_style_t style;
      if (!sequel_analysis_literal(tokens[item], dialect) &&
          !sequel_analysis_bind_token(tokens[item], dialect, &key, &length, &number, &style))
        return;
      items++;
      expecting = false;
    } else if (sequel_runtime_symbol(tokens[item].t, ',')) {
      expecting = true;
    } else {
      return;
    }
  }
  if (!expecting && items > 1 && item < count)
    tokens[open].matching = item;
}

static int sequel_analysis_shared_token_kind(sequel_token_kind_t kind) {
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

sequel_status_t sequel_analysis_adapt_lexed_tokens(const sequel_lexed_document_t *lexed, analysis_token_t **out,
                                                   size_t *count) {
  if (!lexed || !out || !count)
    return SEQUEL_ERR_NULL;
  *out = NULL;
  *count = 0;
  size_t token_count = 0, capacity = 0, previous_end = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  sequel_source_view_t source = sequel_lexed_source(lexed);
  bool gap = false, pending_newline = false;
  for (size_t i = 0; i < token_count; ++i) {
    const sequel_token_t *token = &tokens[i];
    if (token->kind == SEQUEL_TOKEN_EOF)
      break;
    if (!sequel_token_has_range(token, source.length))
      goto invalid;
    if (token->kind == SEQUEL_TOKEN_WHITESPACE) {
      for (size_t j = token->range.start; j < token->range.end; ++j)
        pending_newline |= source.data[j] == '\r' || source.data[j] == '\n';
      previous_end = token->range.end;
      gap = true;
      continue;
    }
    if (*count == capacity) {
      size_t next = capacity ? capacity * 2 : 64;
      if (next < *count || next > SIZE_MAX / sizeof(**out))
        goto overflow;
      analysis_token_t *grown = calloc(next, sizeof(*grown));
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
    for (size_t j = previous_end; j < token->range.start; ++j)
      newline |= source.data[j] == '\r' || source.data[j] == '\n';
    int kind = sequel_analysis_shared_token_kind(token->kind);
    bool opening =
        kind == 0 && token->range.end == token->range.start + 1 && strchr("([{", source.data[token->range.start]);
    bool previous_open =
        *count && (*out)[*count - 1].kind == 0 && (*out)[*count - 1].t.n == 1 && strchr("([{", *(*out)[*count - 1].t.p);
    (*out)[(*count)++] = (analysis_token_t){.t = {.p = source.data + token->range.start,
                                                  .n = token->range.end - token->range.start,
                                                  .word = kind == 1,
                                                  .keyword = token->kind == SEQUEL_TOKEN_KEYWORD,
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
