#include <stdlib.h>
#include <string.h>

#include "analysis/internal.h"

int sequel_token_is_comment(const sequel_token_t *token) {
  return token->kind == SEQUEL_TOKEN_LINE_COMMENT || token->kind == SEQUEL_TOKEN_BLOCK_COMMENT;
}

int sequel_token_is_trivia(const sequel_token_t *token) {
  return token->kind == SEQUEL_TOKEN_WHITESPACE || sequel_token_is_comment(token);
}

int sequel_token_is_identifier(const sequel_token_t *token) {
  return token->kind == SEQUEL_TOKEN_IDENTIFIER || token->kind == SEQUEL_TOKEN_QUOTED_IDENTIFIER;
}

int sequel_token_is_punctuation(const sequel_token_t *token) { return token->kind == SEQUEL_TOKEN_PUNCTUATION; }

int sequel_token_is_keyword(const sequel_token_t *token) { return token->kind == SEQUEL_TOKEN_KEYWORD; }

int sequel_token_is_operator(const sequel_token_t *token) { return token->kind == SEQUEL_TOKEN_OPERATOR; }

int sequel_token_is_literal(const sequel_token_t *token) { return (token->flags & SEQUEL_TOKEN_FLAG_LITERAL) != 0; }

int sequel_token_is_bind(const sequel_token_t *token) { return (token->flags & SEQUEL_TOKEN_FLAG_BIND) != 0; }

size_t sequel_token_count_keyword(const sequel_lexed_document_t *lexed, uint32_t keyword) {
  size_t count = 0;
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  for (size_t i = 0; i < token_count; ++i)
    if (tokens[i].kind == SEQUEL_TOKEN_KEYWORD && tokens[i].keyword_id == keyword)
      ++count;
  return count;
}

int sequel_token_has_range(const sequel_token_t *token, size_t source_length) {
  return token->range.start <= token->range.end && token->range.end <= source_length;
}

const char *sequel_token_source_text(sequel_source_view_t source, const sequel_token_t *token, size_t *length) {
  *length = token->range.end - token->range.start;
  return source.data + token->range.start;
}

int sequel_token_is_symbol(sequel_source_view_t source, const sequel_token_t *token, char symbol) {
  return sequel_token_is_punctuation(token) && token->range.end == token->range.start + 1 &&
         source.data[token->range.start] == symbol;
}

int sequel_token_is_single_punctuation(const sequel_token_t *token) {
  return sequel_token_is_punctuation(token) && token->range.end == token->range.start + 1;
}

int sequel_token_kind_is_word(sequel_token_kind_t kind) {
  return kind == SEQUEL_TOKEN_IDENTIFIER || kind == SEQUEL_TOKEN_KEYWORD || kind == SEQUEL_TOKEN_BOOLEAN ||
         kind == SEQUEL_TOKEN_NULL || kind == SEQUEL_TOKEN_NUMBER || kind == SEQUEL_TOKEN_STRING ||
         kind == SEQUEL_TOKEN_QUOTED_IDENTIFIER || kind == SEQUEL_TOKEN_PARAMETER;
}

int sequel_token_kind_is_comment(sequel_token_kind_t kind) {
  return kind == SEQUEL_TOKEN_LINE_COMMENT || kind == SEQUEL_TOKEN_BLOCK_COMMENT;
}

size_t sequel_token_next_significant(const sequel_token_t *tokens, size_t count, size_t index) {
  while (index < count && sequel_token_is_trivia(&tokens[index]))
    ++index;
  return index;
}

int sequel_token_is_question(sequel_source_view_t source, const sequel_token_t *token) {
  return (token->kind == SEQUEL_TOKEN_PARAMETER || token->kind == SEQUEL_TOKEN_OPERATOR) &&
         token->range.end == token->range.start + 1 && source.data[token->range.start] == '?';
}

sequel_status_t sequel_tokens_collapse_in_lists(sequel_text_result_t *result, sequel_dialect_t dialect) {
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
    size_t open = sequel_token_next_significant(tokens, count, i + 1);
    if (open >= count || !sequel_token_is_symbol(source, &tokens[open], '('))
      continue;
    size_t cursor = sequel_token_next_significant(tokens, count, open + 1), values = 0;
    while (cursor < count && sequel_token_is_question(source, &tokens[cursor])) {
      ++values;
      cursor = sequel_token_next_significant(tokens, count, cursor + 1);
      if (cursor < count && sequel_token_is_symbol(source, &tokens[cursor], ','))
        cursor = sequel_token_next_significant(tokens, count, cursor + 1);
      else
        break;
    }
    if (values < 2 || cursor >= count || !sequel_token_is_symbol(source, &tokens[cursor], ')'))
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

int sequel_keyword_is_clause(uint32_t keyword) {
  return keyword == SEQUEL_KEYWORD_FROM || keyword == SEQUEL_KEYWORD_WHERE || keyword == SEQUEL_KEYWORD_GROUP ||
         keyword == SEQUEL_KEYWORD_HAVING || keyword == SEQUEL_KEYWORD_ORDER || keyword == SEQUEL_KEYWORD_LIMIT ||
         keyword == SEQUEL_KEYWORD_OFFSET || keyword == SEQUEL_KEYWORD_RETURNING || keyword == SEQUEL_KEYWORD_VALUES ||
         keyword == SEQUEL_KEYWORD_SET || keyword == SEQUEL_KEYWORD_UNION || keyword == SEQUEL_KEYWORD_JOIN ||
         keyword == SEQUEL_KEYWORD_ON || keyword == SEQUEL_KEYWORD_QUALIFY;
}

int sequel_token_is_clause_keyword(const sequel_token_t *token) {
  return token && token->kind == SEQUEL_TOKEN_KEYWORD && sequel_keyword_is_clause(token->keyword_id);
}

int sequel_keyword_is_list_clause(uint32_t keyword) {
  return keyword == SEQUEL_KEYWORD_SELECT || keyword == SEQUEL_KEYWORD_GROUP || keyword == SEQUEL_KEYWORD_ORDER ||
         keyword == SEQUEL_KEYWORD_SET || keyword == SEQUEL_KEYWORD_VALUES || keyword == SEQUEL_KEYWORD_RETURNING;
}

int sequel_keyword_ends_case_branch(uint32_t keyword) {
  return keyword == SEQUEL_KEYWORD_WHEN || keyword == SEQUEL_KEYWORD_ELSE || keyword == SEQUEL_KEYWORD_END;
}

#include "analysis/tokens.c"
