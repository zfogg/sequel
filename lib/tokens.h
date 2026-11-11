#pragma once

#include <sequel/parser.h>

#include "runtime_types.h"

sequel_status_t adapt_lexed_tokens(const sequel_lexed_document_t *lexed, analysis_token_t **out, size_t *count);

int sequel_token_is_comment(const sequel_token_t *token);
int sequel_token_is_punctuation(const sequel_token_t *token);
int sequel_token_is_keyword(const sequel_token_t *token);
int sequel_token_is_operator(const sequel_token_t *token);
int sequel_token_is_literal(const sequel_token_t *token);
int sequel_token_is_bind(const sequel_token_t *token);
size_t sequel_token_count_keyword(const sequel_lexed_document_t *lexed, uint32_t keyword);
int sequel_token_has_range(const sequel_token_t *token, size_t source_length);
const char *sequel_token_source_text(sequel_source_view_t source, const sequel_token_t *token, size_t *length);
int sequel_token_is_symbol(sequel_source_view_t source, const sequel_token_t *token, char symbol);

int sequel_keyword_is_clause(uint32_t keyword);
int sequel_keyword_starts_list_clause(uint32_t keyword);
int sequel_keyword_is_case_expression_boundary(uint32_t keyword);
