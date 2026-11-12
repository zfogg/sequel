#pragma once

#include <sequel/parser.h>
#include <sequel/formatter.h>

#include "runtime_types.h"

sequel_status_t sequel_analysis_adapt_lexed_tokens(const sequel_lexed_document_t *lexed, analysis_token_t **out,
                                                   size_t *count);

int sequel_token_is_comment(const sequel_token_t *token);
int sequel_token_is_trivia(const sequel_token_t *token);
int sequel_token_is_identifier(const sequel_token_t *token);
int sequel_token_is_punctuation(const sequel_token_t *token);
int sequel_token_is_keyword(const sequel_token_t *token);
int sequel_token_is_operator(const sequel_token_t *token);
int sequel_token_is_literal(const sequel_token_t *token);
int sequel_token_is_bind(const sequel_token_t *token);
size_t sequel_token_count_keyword(const sequel_lexed_document_t *lexed, uint32_t keyword);
int sequel_token_has_range(const sequel_token_t *token, size_t source_length);
const char *sequel_token_source_text(sequel_source_view_t source, const sequel_token_t *token, size_t *length);
int sequel_token_is_symbol(sequel_source_view_t source, const sequel_token_t *token, char symbol);
int sequel_token_is_single_punctuation(const sequel_token_t *token);
int sequel_token_kind_is_word(sequel_token_kind_t kind);
int sequel_token_kind_is_comment(sequel_token_kind_t kind);
size_t sequel_token_next_significant(const sequel_token_t *tokens, size_t count, size_t index);
int sequel_token_is_question(sequel_source_view_t source, const sequel_token_t *token);
sequel_status_t sequel_tokens_collapse_in_lists(sequel_text_result_t *result, sequel_dialect_t dialect);

int sequel_keyword_is_clause(uint32_t keyword);
int sequel_token_is_clause_keyword(const sequel_token_t *token);
int sequel_keyword_starts_list_clause(uint32_t keyword);
int sequel_keyword_is_case_expression_boundary(uint32_t keyword);
