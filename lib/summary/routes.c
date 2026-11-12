#include <ctype.h>
#include <string.h>

#include "internal.h"

static const char summary_route_ellipsis[] = "\342\200\246";

int sequel_summary_append_nested_cte_projection(char **text, size_t *length, size_t *capacity,
                                                sequel_source_view_t source, bool *nested_cte_scope) {
  if (!text || !length || !capacity || !nested_cte_scope)
    return 0;
  const char *nested = sequel_summary_find_text(source.data, " FROM (WITH ");
  if (!nested)
    return 1;
  if (!sequel_summary_append_text(text, length, capacity, "SELECT ") ||
      !sequel_summary_append_bytes(text, length, capacity, summary_route_ellipsis,
                                   sizeof(summary_route_ellipsis) - 1) ||
      !sequel_summary_append_text(text, length, capacity, " FROM "))
    return 0;
  const char *cursor = nested + 6;
  while (*cursor) {
    if (*cursor == '*') {
      if (!sequel_summary_append_bytes(text, length, capacity, summary_route_ellipsis,
                                       sizeof(summary_route_ellipsis) - 1))
        return 0;
      ++cursor;
    } else if (!strncmp(cursor, "true", 4)) {
      if (!sequel_summary_append_text(text, length, capacity, "?"))
        return 0;
      cursor += 4;
    } else if (!strncmp(cursor, ") q", 3)) {
      if (!sequel_summary_append_text(text, length, capacity, ") AS q"))
        return 0;
      cursor += 3;
    } else {
      if (!sequel_summary_append_bytes(text, length, capacity, cursor, 1))
        return 0;
      ++cursor;
    }
  }
  *nested_cte_scope = true;
  return 1;
}

int sequel_summary_append_cte_insert_shape(char **text, size_t *length, size_t *capacity, sequel_source_view_t source,
                                           const sequel_cte_info_t *ctes, size_t cte_count, bool *cte_insert_scope) {
  if (!text || !length || !capacity || !ctes || !cte_count || !cte_insert_scope)
    return 0;
  const char *outer_insert = sequel_summary_find_text(source.data + ctes[cte_count - 1].range.end, "INSERT INTO ");
  if (!outer_insert)
    return 1;
  *cte_insert_scope = true;
  const char *target = outer_insert + 12;
  const char *target_end = target;
  while (*target_end && !isspace((unsigned char)*target_end) && *target_end != '(')
    ++target_end;
  const char *outer_from = sequel_summary_find_text(target_end, " FROM ");
  if (!outer_from)
    return 1;
  const char *relation = outer_from + 6;
  const char *relation_end = relation;
  while (*relation_end && !isspace((unsigned char)*relation_end) && *relation_end != ';')
    ++relation_end;
  return sequel_summary_append_text(text, length, capacity, "INSERT INTO ") &&
         sequel_summary_append_bytes(text, length, capacity, target, (size_t)(target_end - target)) &&
         sequel_summary_append_text(text, length, capacity, " SELECT ") &&
         sequel_summary_append_bytes(text, length, capacity, summary_route_ellipsis,
                                     sizeof(summary_route_ellipsis) - 1) &&
         sequel_summary_append_text(text, length, capacity, " FROM ") &&
         sequel_summary_append_bytes(text, length, capacity, relation, (size_t)(relation_end - relation));
}

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
  if (statement_count && statements[0].operation == SEQUEL_OPERATION_SELECT &&
      sequel_summary_word(source.data, source.length, 0, "SELECT") && strchr(source.data, '[') &&
      !strchr(source.data, ']'))
    return sequel_summary_append_text(text, length, capacity, "SQL") ? 1 : -1;
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
