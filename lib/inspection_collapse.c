#include <stdlib.h>
#include <string.h>

#include <sequel/parser.h>

#include "inspection_collapse.h"

static size_t next_significant_token(const sequel_token_t *tokens, size_t count, size_t index) {
  while (index < count &&
         (tokens[index].kind == SEQUEL_TOKEN_WHITESPACE || tokens[index].kind == SEQUEL_TOKEN_LINE_COMMENT ||
          tokens[index].kind == SEQUEL_TOKEN_BLOCK_COMMENT))
    ++index;
  return index;
}

static bool token_is_symbol(const sequel_source_view_t *source, const sequel_token_t *token, char symbol) {
  return token->kind == SEQUEL_TOKEN_PUNCTUATION && token->range.end == token->range.start + 1 &&
         source->data[token->range.start] == symbol;
}

static bool token_is_question(const sequel_source_view_t *source, const sequel_token_t *token) {
  return (token->kind == SEQUEL_TOKEN_PARAMETER || token->kind == SEQUEL_TOKEN_OPERATOR) &&
         token->range.end == token->range.start + 1 && source->data[token->range.start] == '?';
}

sequel_status_t sequel_inspection_collapse_in_lists(sequel_text_result_t *result, sequel_dialect_t dialect) {
  if (!result || !result->data || !result->length)
    return SEQUEL_OK;
  sequel_parse_options_t options;
  sequel_parse_options_init(&options);
  options.dialect = dialect;
  options.retain_comments = true;
  options.retain_whitespace = true;
  sequel_source_view_t source = {result->data, result->length};
  sequel_lexed_document_t *lexed = NULL;
  sequel_status_t status = sequel_lex(&source, &options, NULL, &lexed);
  if (status != SEQUEL_OK)
    return status;
  size_t count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &count);
  size_t *starts = NULL, *ends = NULL, replacements = 0, capacity = 0;
  for (size_t i = 0; i < count; ++i) {
    if (tokens[i].kind != SEQUEL_TOKEN_KEYWORD || tokens[i].keyword_id != SEQUEL_KEYWORD_IN)
      continue;
    size_t open = next_significant_token(tokens, count, i + 1);
    if (open >= count || !token_is_symbol(&source, &tokens[open], '('))
      continue;
    size_t cursor = next_significant_token(tokens, count, open + 1), values = 0;
    while (cursor < count && token_is_question(&source, &tokens[cursor])) {
      ++values;
      cursor = next_significant_token(tokens, count, cursor + 1);
      if (cursor < count && token_is_symbol(&source, &tokens[cursor], ','))
        cursor = next_significant_token(tokens, count, cursor + 1);
      else
        break;
    }
    if (values < 2 || cursor >= count || !token_is_symbol(&source, &tokens[cursor], ')'))
      continue;
    if (replacements == capacity) {
      size_t next = capacity ? capacity * 2 : 8;
      size_t *new_starts = malloc(next * sizeof(*starts));
      size_t *new_ends = malloc(next * sizeof(*ends));
      if (!new_starts || !new_ends) {
        free(new_starts);
        free(new_ends);
        free(starts);
        free(ends);
        sequel_lexed_document_free(lexed);
        return SEQUEL_ERR_NOMEM;
      }
      if (replacements) {
        memcpy(new_starts, starts, replacements * sizeof(*starts));
        memcpy(new_ends, ends, replacements * sizeof(*ends));
      }
      free(starts);
      free(ends);
      starts = new_starts;
      ends = new_ends;
      capacity = next;
    }
    starts[replacements] = tokens[open].range.start;
    ends[replacements++] = tokens[cursor].range.end;
  }
  for (size_t i = replacements; i > 0; --i) {
    size_t start = starts[i - 1], end = ends[i - 1];
    memmove(result->data + start + 3, result->data + end, result->length - end + 1);
    memcpy(result->data + start, "(?)", 3);
    result->length -= end - start - 3;
  }
  free(starts);
  free(ends);
  sequel_lexed_document_free(lexed);
  return SEQUEL_OK;
}
