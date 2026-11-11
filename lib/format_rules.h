#pragma once

#include <stddef.h>

#include <sequel/formatter.h>

#include "tokens.h"

int sequel_format_is_condition_operator(const sequel_token_t *token, size_t parenthesis_depth,
                                        size_t cte_parenthesis_depth);
int sequel_format_is_select_start(const sequel_token_t *token, size_t parenthesis_depth);
int sequel_format_is_literal_replacement(const sequel_token_t *token, const sequel_format_options_t *settings);
