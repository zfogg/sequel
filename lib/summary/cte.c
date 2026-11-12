#include <ctype.h>
#include <string.h>

#include "internal.h"

static const char summary_ellipsis[] = "\342\200\246";

int sequel_summary_append_nested_cte_projection(char **text, size_t *length, size_t *capacity,
                                                sequel_source_view_t source, bool *nested_cte_scope) {
  if (!text || !length || !capacity || !nested_cte_scope)
    return 0;
  const char *nested = sequel_summary_find_text(source.data, " FROM (WITH ");
  if (!nested)
    return 1;
  if (!sequel_summary_append_text(text, length, capacity, "SELECT ") ||
      !sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
      !sequel_summary_append_text(text, length, capacity, " FROM "))
    return 0;
  const char *cursor = nested + 6;
  while (*cursor) {
    if (*cursor == '*') {
      if (!sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
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
         sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) &&
         sequel_summary_append_text(text, length, capacity, " FROM ") &&
         sequel_summary_append_bytes(text, length, capacity, relation, (size_t)(relation_end - relation));
}

int sequel_summary_append_cte_definitions(char **text, size_t *length, size_t *capacity, sequel_source_view_t source,
                                          const sequel_cte_info_t *ctes, size_t cte_count, bool recursive,
                                          size_t *cte_prefix_end) {
  if (!text || !length || !capacity || !ctes || !cte_count || !cte_prefix_end)
    return 0;
  if (!sequel_summary_append_text(text, length, capacity, recursive ? "WITH RECURSIVE " : "WITH "))
    return 0;
  for (size_t i = 0; i < cte_count; ++i) {
    if (i && !sequel_summary_append_text(text, length, capacity, ", "))
      return 0;
    if (!sequel_summary_append_bytes(text, length, capacity, source.data + ctes[i].name_range.start,
                                     ctes[i].name_range.end - ctes[i].name_range.start) ||
        !sequel_summary_append_text(text, length, capacity,
                                    sequel_summary_find_text(source.data + ctes[i].name_range.end, "MATERIALIZED")
                                        ? " AS MATERIALIZED ("
                                        : " AS ("))
      return 0;
    bool preserve_limit = false;
    for (size_t cursor = ctes[i].name_range.end; cursor < ctes[i].body_range.start; ++cursor)
      if (cursor + 12 <= source.length && !memcmp(source.data + cursor, "MATERIALIZED", 12)) {
        preserve_limit = true;
        break;
      }
    if (!sequel_summary_append_body_summary(text, length, capacity, source.data, ctes[i].body_range, preserve_limit) ||
        !sequel_summary_append_text(text, length, capacity, ")"))
      return 0;
  }
  if (!sequel_summary_append_text(text, length, capacity, " "))
    return 0;
  *cte_prefix_end = *length;
  return 1;
}
