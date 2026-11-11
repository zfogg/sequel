#include <stdlib.h>

#include <sequel/query.h>

#include "model.h"

void sequel_query_document_free(sequel_query_document_t *document) {
  if (!document)
    return;
  sequel_query_array_free(document->statements);
  sequel_query_array_free(document->relations);
  sequel_query_array_free(document->ctes);
  sequel_query_array_free(document->parameters);
  sequel_query_array_free(document->joins);
  sequel_query_array_free(document->projections);
  sequel_query_array_free(document->predicates);
  sequel_query_array_free(document->clauses);
  free(document);
}

size_t sequel_query_statement_count(const sequel_query_document_t *document) {
  return document ? document->statement_count : 0;
}

const sequel_statement_info_t *sequel_query_statements(const sequel_query_document_t *document) {
  return document ? document->statements : NULL;
}

size_t sequel_query_relation_count(const sequel_query_document_t *document) {
  return document ? document->relation_count : 0;
}

const sequel_relation_info_t *sequel_query_relations(const sequel_query_document_t *document) {
  return document ? document->relations : NULL;
}

size_t sequel_query_cte_count(const sequel_query_document_t *document) { return document ? document->cte_count : 0; }

const sequel_cte_info_t *sequel_query_ctes(const sequel_query_document_t *document) {
  return document ? document->ctes : NULL;
}

bool sequel_query_outer_distinct_on(const sequel_query_document_t *document) {
  return document ? document->outer_distinct_on : false;
}

bool sequel_query_outer_limit(const sequel_query_document_t *document) {
  return document ? document->outer_limit : false;
}

sequel_source_range_t sequel_query_outer_range(const sequel_query_document_t *document) {
  return document ? document->outer_range : (sequel_source_range_t){0, 0};
}

size_t sequel_query_parameter_count(const sequel_query_document_t *document) {
  return document ? document->parameter_count : 0;
}

const sequel_parameter_info_t *sequel_query_parameters(const sequel_query_document_t *document) {
  return document ? document->parameters : NULL;
}

size_t sequel_query_join_count(const sequel_query_document_t *document) { return document ? document->join_count : 0; }

const sequel_join_info_t *sequel_query_joins(const sequel_query_document_t *document) {
  return document ? document->joins : NULL;
}

size_t sequel_query_projection_count(const sequel_query_document_t *document) {
  return document ? document->projection_count : 0;
}

const sequel_projection_info_t *sequel_query_projections(const sequel_query_document_t *document) {
  return document ? document->projections : NULL;
}

size_t sequel_query_predicate_count(const sequel_query_document_t *document) {
  return document ? document->predicate_count : 0;
}

const sequel_predicate_info_t *sequel_query_predicates(const sequel_query_document_t *document) {
  return document ? document->predicates : NULL;
}

size_t sequel_query_clause_count(const sequel_query_document_t *document) {
  return document ? document->clause_count : 0;
}

const sequel_clause_info_t *sequel_query_clauses(const sequel_query_document_t *document) {
  return document ? document->clauses : NULL;
}

sequel_source_view_t sequel_query_source(const sequel_query_document_t *document) {
  return document ? sequel_lexed_source(sequel_syntax_lexed(document->syntax)) : (sequel_source_view_t){0};
}

sequel_dialect_t sequel_query_dialect(const sequel_query_document_t *document) {
  return document ? sequel_lexed_dialect(sequel_syntax_lexed(document->syntax)) : SEQUEL_GENERIC;
}

const sequel_syntax_document_t *sequel_query_syntax(const sequel_query_document_t *document) {
  return document ? document->syntax : NULL;
}

sequel_status_t sequel_query_visit(const sequel_query_document_t *document, const sequel_query_visitor_t *visitor) {
  if (!document || !visitor)
    return SEQUEL_ERR_NULL;
  for (size_t i = 0; i < document->statement_count; ++i)
    if (visitor->statement && !visitor->statement(document, i, visitor->userdata))
      return SEQUEL_ERR_CANCELLED;
  for (size_t i = 0; i < document->clause_count; ++i)
    if (visitor->clause && !visitor->clause(document, i, visitor->userdata))
      return SEQUEL_ERR_CANCELLED;
  for (size_t i = 0; i < document->relation_count; ++i)
    if (visitor->relation && !visitor->relation(document, i, visitor->userdata))
      return SEQUEL_ERR_CANCELLED;
  for (size_t i = 0; i < document->parameter_count; ++i)
    if (visitor->parameter && !visitor->parameter(document, i, visitor->userdata))
      return SEQUEL_ERR_CANCELLED;
  for (size_t i = 0; i < document->join_count; ++i)
    if (visitor->join && !visitor->join(document, i, visitor->userdata))
      return SEQUEL_ERR_CANCELLED;
  for (size_t i = 0; i < document->projection_count; ++i)
    if (visitor->projection && !visitor->projection(document, i, visitor->userdata))
      return SEQUEL_ERR_CANCELLED;
  for (size_t i = 0; i < document->predicate_count; ++i)
    if (visitor->predicate && !visitor->predicate(document, i, visitor->userdata))
      return SEQUEL_ERR_CANCELLED;
  return SEQUEL_OK;
}
