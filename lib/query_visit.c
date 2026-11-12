#include <sequel/query.h>

#include "model.h"

sequel_status_t sequel_query_visit(const sequel_query_document_t *document, const sequel_query_visitor_t *visitor) {
  if (!document || !visitor)
    return SEQUEL_ERR_NULL;
  for (size_t i = 0; i < document->statement_count; ++i)
    if (visitor->statement && !visitor->statement(document, i, visitor->userdata))
      return SEQUEL_ERR_CANCELLED;
  for (size_t i = 0; i < document->clause_count; ++i)
    if (visitor->clause && !visitor->clause(document, i, visitor->userdata))
      return SEQUEL_ERR_CANCELLED;
  for (size_t i = 0; i < document->cte_count; ++i)
    if (visitor->cte && !visitor->cte(document, i, visitor->userdata))
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
