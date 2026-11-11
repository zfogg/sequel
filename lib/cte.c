#include <string.h>

#include <sequel/query.h>

#include "model.h"
#include "tokens.h"

sequel_status_t sequel_query_build_ctes(sequel_query_document_t *document, const sequel_token_t *tokens,
                                        size_t token_count, const sequel_source_view_t *source) {
  for (size_t i = 0; i < token_count; ++i) {
    if (tokens[i].kind != SEQUEL_TOKEN_KEYWORD || tokens[i].keyword_id != SEQUEL_KEYWORD_WITH)
      continue;
    size_t cursor = i + 1;
    while (cursor < token_count && sequel_token_is_trivia(&tokens[cursor]))
      ++cursor;
    if (cursor < token_count && tokens[cursor].range.end - tokens[cursor].range.start == 9 &&
        sequel_query_word_equals_ci(source->data + tokens[cursor].range.start, 9, "RECURSIVE"))
      ++cursor;
    for (;;) {
      while (cursor < token_count && sequel_token_is_trivia(&tokens[cursor]))
        ++cursor;
      if (cursor >= token_count || !sequel_token_is_identifier(&tokens[cursor]))
        break;
      size_t name = cursor++;
      while (cursor < token_count && sequel_token_is_trivia(&tokens[cursor]))
        ++cursor;
      if (cursor < token_count && tokens[cursor].kind == SEQUEL_TOKEN_PUNCTUATION &&
          source->data[tokens[cursor].range.start] == '(') {
        unsigned depth = 0;
        do {
          if (tokens[cursor].kind == SEQUEL_TOKEN_PUNCTUATION) {
            char punctuation = source->data[tokens[cursor].range.start];
            if (punctuation == '(')
              ++depth;
            else if (punctuation == ')' && depth)
              --depth;
          }
          ++cursor;
        } while (cursor < token_count && depth);
        while (cursor < token_count && sequel_token_is_trivia(&tokens[cursor]))
          ++cursor;
      }
      if (cursor >= token_count || tokens[cursor].range.end - tokens[cursor].range.start != 2 ||
          !sequel_query_word_equals_ci(source->data + tokens[cursor].range.start, 2, "AS"))
        break;
      ++cursor;
      while (cursor < token_count && sequel_token_is_trivia(&tokens[cursor]))
        ++cursor;
      if (cursor < token_count && sequel_token_is_identifier(&tokens[cursor])) {
        size_t modifier = tokens[cursor].range.end - tokens[cursor].range.start;
        if ((modifier == 12 &&
             sequel_query_word_equals_ci(source->data + tokens[cursor].range.start, 12, "MATERIALIZED")) ||
            (modifier == 3 && sequel_query_word_equals_ci(source->data + tokens[cursor].range.start, 3, "NOT"))) {
          ++cursor;
          while (cursor < token_count && sequel_token_is_trivia(&tokens[cursor]))
            ++cursor;
          if (modifier == 3 && cursor < token_count && sequel_token_is_identifier(&tokens[cursor]) &&
              tokens[cursor].range.end - tokens[cursor].range.start == 12 &&
              sequel_query_word_equals_ci(source->data + tokens[cursor].range.start, 12, "MATERIALIZED"))
            ++cursor;
          while (cursor < token_count && sequel_token_is_trivia(&tokens[cursor]))
            ++cursor;
        }
      }
      if (cursor >= token_count || tokens[cursor].kind != SEQUEL_TOKEN_PUNCTUATION ||
          source->data[tokens[cursor].range.start] != '(')
        break;
      size_t body_open = cursor++;
      unsigned depth = 1;
      while (cursor < token_count && depth) {
        if (tokens[cursor].kind == SEQUEL_TOKEN_PUNCTUATION) {
          char punctuation = source->data[tokens[cursor].range.start];
          if (punctuation == '(')
            ++depth;
          else if (punctuation == ')')
            --depth;
        }
        ++cursor;
      }
      size_t body_close = cursor ? cursor - 1 : body_open;
      sequel_cte_info_t cte = {.range = {tokens[name].range.start, tokens[body_close].range.end},
                               .name_range = tokens[name].range,
                               .body_range = {tokens[body_open].range.end, tokens[body_close].range.start},
                               .statement_index = sequel_query_statement_for_range(document, tokens[name].range),
                               .syntax_node = SIZE_MAX};
      if (!sequel_query_array_append((void **)&document->ctes, &document->cte_count, sizeof(cte), &cte))
        return SEQUEL_ERR_NOMEM;
      while (cursor < token_count && sequel_token_is_trivia(&tokens[cursor]))
        ++cursor;
      if (cursor >= token_count || tokens[cursor].kind != SEQUEL_TOKEN_PUNCTUATION ||
          source->data[tokens[cursor].range.start] != ',')
        break;
      ++cursor;
    }
    break;
  }
  return SEQUEL_OK;
}
