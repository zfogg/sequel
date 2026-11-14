#pragma once

#include <stdlib.h>

#include <sequel/query.h>

struct sequel_syntax_document {
  const sequel_lexed_document_t *lexed;
  bool owns_lexed;
  sequel_syntax_node_t *nodes;
  size_t node_count;
  size_t *children;
  size_t child_count;
  sequel_parse_issue_t *issues;
  size_t issue_count;
  size_t max_diagnostics;
  size_t max_nodes;
};

sequel_bind_style_t sequel_query_bind_style(const char *text, size_t length, size_t *number);
sequel_operation_t sequel_query_operation_from_syntax(sequel_syntax_kind_t kind);
bool sequel_query_word_equals_ci(const char *word, size_t length, const char *expected);
sequel_operation_t sequel_query_operation_from_word(const char *word, size_t length);
size_t sequel_query_statement_for_range(const sequel_query_document_t *document, sequel_source_range_t range);
sequel_join_kind_t sequel_query_join_kind(const sequel_token_t *tokens, size_t token_count, size_t join_index, const sequel_source_view_t *source);
uint32_t sequel_query_join_flags(const sequel_token_t *tokens, size_t token_count, size_t join_index, const sequel_source_view_t *source);
size_t sequel_query_expression_node_for_range(const sequel_syntax_document_t *syntax, sequel_source_range_t range);

typedef struct {
  size_t capacity;
} query_array_header;

int sequel_query_array_append(void **array, size_t *count, size_t size, const void *value);
sequel_status_t sequel_query_build_statements(sequel_query_document_t *document, const sequel_syntax_document_t *syntax, const sequel_token_t *tokens, size_t token_count,
                                              const sequel_source_view_t *source, size_t max_nodes);
sequel_status_t sequel_query_build_parameters(sequel_query_document_t *document, const sequel_token_t *tokens, size_t token_count, const sequel_source_view_t *source);
sequel_status_t sequel_query_build_ctes(sequel_query_document_t *document, const sequel_token_t *tokens, size_t token_count, const sequel_source_view_t *source);

static inline void sequel_query_array_free(void *array) {
  if (array)
    free((query_array_header *)array - 1);
}

struct sequel_query_document {
  const sequel_syntax_document_t *syntax;
  sequel_statement_info_t *statements;
  size_t statement_count;
  sequel_statement_info_t *scope_statements;
  size_t scope_statement_count;
  sequel_relation_info_t *relations;
  size_t relation_count;
  sequel_cte_info_t *ctes;
  size_t cte_count;
  bool outer_distinct_on;
  bool outer_limit;
  sequel_source_range_t outer_limit_range;
  sequel_source_range_t outer_limit_clause_range;
  sequel_source_range_t outer_limit_value_range;
  sequel_source_range_t outer_range;
  sequel_parameter_info_t *parameters;
  size_t parameter_count;
  sequel_join_info_t *joins;
  size_t join_count;
  sequel_projection_info_t *projections;
  size_t projection_count;
  sequel_predicate_info_t *predicates;
  size_t predicate_count;
  sequel_clause_info_t *clauses;
  size_t clause_count;
};
