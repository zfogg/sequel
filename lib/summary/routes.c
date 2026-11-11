#include "internal.h"

int sequel_summary_begin_render(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query,
                                const sequel_summary_options_t *settings, sequel_source_view_t source,
                                size_t statement_count, const sequel_statement_info_t *statements, int *route) {
  if (!text || !length || !capacity || !query || !settings || !route)
    return 0;
  *route =
      sequel_summary_try_special_routes(text, length, capacity, query, settings, source, statement_count, statements);
  return *route >= 0;
}

int sequel_summary_append_table_prefix(char **text, size_t *length, size_t *capacity,
                                       const sequel_query_document_t *query, const sequel_summary_options_t *settings,
                                       sequel_source_view_t source, size_t statement_count) {
  if (!text || !length || !capacity || !query || !settings)
    return 0;
  if (!settings->include_tables || !statement_count || sequel_query_cte_count(query) ||
      sequel_summary_find_word(source.data, source.length, 0, "UNION"))
    return 1;
  return sequel_summary_append_parenthesized_subquery(text, length, capacity, source);
}

int sequel_summary_try_special_routes(char **text, size_t *length, size_t *capacity,
                                      const sequel_query_document_t *query, const sequel_summary_options_t *settings,
                                      sequel_source_view_t source, size_t statement_count,
                                      const sequel_statement_info_t *statements) {
  const sequel_statement_info_t *first = statement_count ? &statements[0] : NULL;
  int result = sequel_summary_render_scalar_projection_summary(text, length, capacity, query, source, first);
  if (result != 0)
    return result;
  result = sequel_summary_append_parameter_only(text, length, capacity, query, first);
  if (result != 0)
    return result;
  result = sequel_summary_append_clickhouse_dollar_projection(text, length, capacity, query, source, first);
  if (result != 0)
    return result;
  result = sequel_summary_append_cte_shape(text, length, capacity, query, source, first);
  if (result != 0)
    return result;
  result = sequel_summary_append_clickhouse_create_shape(text, length, capacity, query, source, first);
  if (result != 0)
    return result;
  result = sequel_summary_append_dialect_recovery(text, length, capacity, query, settings, source, statement_count,
                                                  statements);
  if (result == 2)
    return 2;
  return result;
}
