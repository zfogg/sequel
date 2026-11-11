#include <stddef.h>

#include "internal.h"

static const char summary_ellipsis[] = "\342\200\246";

int sequel_summary_append_parameter_only(char **text, size_t *length, size_t *capacity,
                                         const sequel_query_document_t *query,
                                         const sequel_statement_info_t *statement) {
  if (!statement || statement->operation != SEQUEL_OPERATION_SELECT || sequel_query_relation_count(query) != 0 ||
      sequel_query_parameter_count(query) == 0 || sequel_summary_starts_function_projection(query))
    return 0;
  const char *projection = sequel_query_parameter_count(query) == 1 ? "?" : summary_ellipsis;
  size_t projection_length = sequel_query_parameter_count(query) == 1 ? 1 : sizeof(summary_ellipsis) - 1;
  return sequel_summary_append_text(text, length, capacity, "SELECT ") &&
                 sequel_summary_append_bytes(text, length, capacity, projection, projection_length)
             ? 1
             : -1;
}

int sequel_summary_append_clickhouse_dollar_projection(char **text, size_t *length, size_t *capacity,
                                                       const sequel_query_document_t *query,
                                                       sequel_source_view_t source,
                                                       const sequel_statement_info_t *statement) {
  if (!statement || statement->operation != SEQUEL_OPERATION_SELECT ||
      sequel_query_dialect(query) != SEQUEL_CLICKHOUSE || sequel_query_projection_count(query) != 1 ||
      sequel_query_relation_count(query) != 1)
    return 0;
  const sequel_projection_info_t *projection = sequel_query_projections(query);
  const sequel_relation_info_t *relation = sequel_query_relations(query);
  if (projection[0].range.start >= source.length || source.data[projection[0].range.start] != '$' ||
      relation[0].name_range.end > source.length || relation[0].name_range.start >= relation[0].name_range.end)
    return 0;
  return sequel_summary_append_text(text, length, capacity, "SELECT … FROM ") &&
                 sequel_summary_append_bytes(text, length, capacity, source.data + relation[0].name_range.start,
                                             relation[0].name_range.end - relation[0].name_range.start)
             ? 1
             : -1;
}

int sequel_summary_append_cte_shape(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query,
                                    sequel_source_view_t source, const sequel_statement_info_t *statement) {
  if (!statement || statement->operation != SEQUEL_OPERATION_SELECT ||
      sequel_query_dialect(query) != SEQUEL_CLICKHOUSE || sequel_query_cte_count(query) == 0)
    return 0;
  if (!sequel_summary_append_text(text, length, capacity, "WITH … SELECT …"))
    return -1;
  size_t relation_count = sequel_query_relation_count(query);
  if (!relation_count)
    return 1;
  const sequel_relation_info_t *relations = sequel_query_relations(query);
  const sequel_relation_info_t *outer = &relations[relation_count - 1];
  if (outer->name_range.end <= outer->name_range.start || outer->name_range.end > source.length)
    return 1;
  return sequel_summary_append_text(text, length, capacity, " FROM ") &&
                 sequel_summary_append_bytes(text, length, capacity, source.data + outer->name_range.start,
                                             outer->name_range.end - outer->name_range.start)
             ? 1
             : -1;
}

int sequel_summary_append_clickhouse_create_shape(char **text, size_t *length, size_t *capacity,
                                                  const sequel_query_document_t *query, sequel_source_view_t source,
                                                  const sequel_statement_info_t *statement) {
  if (!statement || statement->operation != SEQUEL_OPERATION_CREATE ||
      sequel_query_dialect(query) != SEQUEL_CLICKHOUSE ||
      !sequel_summary_find_word(source.data, source.length, 0, "ORDER"))
    return 0;
  return sequel_summary_append_text(text, length, capacity, "CREATE ORDER BY") ? 1 : -1;
}
