#include "internal.h"

int sequel_summary_prepare_document(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query,
                                    const sequel_summary_options_t *settings, sequel_source_view_t source,
                                    size_t statement_count, const sequel_statement_info_t *statements,
                                    int *special_route, size_t *cte_prefix_end, size_t *outer_start,
                                    bool *nested_cte_scope, bool *cte_insert_scope, bool *scalar_with_scope) {
  if (!sequel_summary_begin_render(text, length, capacity, query, settings, source, statement_count, statements,
                                   special_route))
    return 0;
  if (*special_route == 1 || *special_route == 2)
    return 1;

  int table_prefix =
      sequel_summary_append_table_prefix(text, length, capacity, query, settings, source, statement_count);
  if (!table_prefix)
    return 0;
  if (table_prefix == 2) {
    *special_route = 2;
    return 1;
  }

  *cte_prefix_end = 0;
  *outer_start = 0;
  *nested_cte_scope = false;
  *cte_insert_scope = false;
  *scalar_with_scope = sequel_summary_word(source.data, source.length, 0, "WITH") && sequel_query_cte_count(query) == 0;
  if (settings->include_operation && sequel_query_cte_count(query)) {
    const sequel_cte_info_t *ctes = sequel_query_ctes(query);
    bool recursive = source.length >= 13 && sequel_summary_find_text(source.data, "WITH RECURSIVE") == source.data;
    if (!sequel_summary_append_cte_definitions(text, length, capacity, source, ctes, sequel_query_cte_count(query),
                                               recursive, cte_prefix_end))
      return 0;
    if (sequel_query_outer_distinct_on(query) &&
        !sequel_summary_append_text(text, length, capacity, "SELECT DISTINCT ON (…) …"))
      return 0;
    if (!sequel_summary_append_nested_cte_projection(text, length, capacity, source, nested_cte_scope))
      return 0;
    if (!sequel_summary_append_cte_insert_shape(text, length, capacity, source, ctes, sequel_query_cte_count(query),
                                                cte_insert_scope))
      return 0;
    *outer_start = sequel_query_outer_range(query).start;
  }
  if (settings->include_operation && *scalar_with_scope && !sequel_summary_append_text(text, length, capacity, "WITH "))
    return 0;
  return 1;
}
