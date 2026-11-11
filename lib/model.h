#pragma once

#include <stdlib.h>

#include <sequel/query.h>

sequel_bind_style_t sequel_query_bind_style(const char *text, size_t length, size_t *number);

typedef struct {
  size_t capacity;
} query_array_header;

static inline void sequel_query_array_free(void *array) {
  if (array)
    free((query_array_header *)array - 1);
}

struct sequel_query_document {
  const sequel_syntax_document_t *syntax;
  sequel_statement_info_t *statements;
  size_t statement_count;
  sequel_relation_info_t *relations;
  size_t relation_count;
  sequel_cte_info_t *ctes;
  size_t cte_count;
  bool outer_distinct_on;
  bool outer_limit;
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
