#include "format_rules.h"

int sequel_format_is_condition_operator(const sequel_token_t *token, size_t parenthesis_depth,
                                        size_t cte_parenthesis_depth) {
  return token->kind == SEQUEL_TOKEN_KEYWORD &&
         (parenthesis_depth == 0 ||
          (cte_parenthesis_depth && parenthesis_depth == cte_parenthesis_depth)) &&
         (token->keyword_id == SEQUEL_KEYWORD_AND || token->keyword_id == SEQUEL_KEYWORD_OR);
}

int sequel_format_is_select_start(const sequel_token_t *token, size_t parenthesis_depth) {
  return token->kind == SEQUEL_TOKEN_KEYWORD && token->keyword_id == SEQUEL_KEYWORD_SELECT && parenthesis_depth == 0;
}

int sequel_format_is_literal_replacement(const sequel_token_t *token, const sequel_format_options_t *settings) {
  return sequel_token_is_literal(token) && !sequel_token_is_bind(token) && !settings->preserve_literals;
}
