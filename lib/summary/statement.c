#include <ctype.h>
#include <string.h>

#include <sequel/summary.h>

#include "internal.h"

static const char ellipsis[] = "\342\200\246";

int sequel_summary_append_statement_shape(char **text, size_t *length, size_t *capacity, sequel_source_view_t source,
                                          const sequel_query_document_t *query,
                                          const sequel_summary_options_t *settings,
                                          const sequel_statement_info_t *statement) {
  if (!settings->include_operation || !statement)
    return 1;
  if (statement->operation == SEQUEL_OPERATION_TRANSACTION) {
    size_t start = 0;
    while (start < source.length && isspace((unsigned char)source.data[start]))
      ++start;
    size_t end = start;
    while (end < source.length && !isspace((unsigned char)source.data[end]) && source.data[end] != ';')
      ++end;
    return end > start && sequel_summary_append_bytes(text, length, capacity, source.data + start, end - start);
  }
  if (sequel_summary_word(source.data, source.length, 0, "REINDEX"))
    return sequel_summary_append_text(text, length, capacity, "SQL");
  if (statement->operation != SEQUEL_OPERATION_UNKNOWN) {
    return sequel_summary_append_text(text, length, capacity, sequel_summary_operation_name(statement->operation));
  }
  size_t start = 0;
  size_t cte_count = sequel_query_cte_count(query);
  if (cte_count)
    start = sequel_query_ctes(query)[cte_count - 1].range.end;
  while (start < source.length && isspace((unsigned char)source.data[start]))
    ++start;
  size_t end = start;
  while (end < source.length && !isspace((unsigned char)source.data[end]) && source.data[end] != '(')
    ++end;
  if (cte_count && sequel_summary_word(source.data, source.length, start, "SELECT"))
    return sequel_summary_append_text(text, length, capacity, "SELECT ") &&
           sequel_summary_append_bytes(text, length, capacity, ellipsis, sizeof(ellipsis) - 1);
  if (end <= start || !sequel_summary_append_bytes(text, length, capacity, source.data + start, end - start))
    return 0;
  if (sequel_summary_word(source.data, source.length, start, "ANALYZE")) {
    size_t table = end;
    while (table < source.length && isspace((unsigned char)source.data[table]))
      ++table;
    size_t table_end = table;
    while (table_end < source.length && !isspace((unsigned char)source.data[table_end]) &&
           source.data[table_end] != ';')
      ++table_end;
    if (table_end > table &&
        (!sequel_summary_append_text(text, length, capacity, " ") ||
         !sequel_summary_append_bytes(text, length, capacity, source.data + table, table_end - table)))
      return 0;
  }
  return 1;
}

int sequel_summary_append_projection_shape(char **text, size_t *length, size_t *capacity, sequel_source_view_t source,
                                           const sequel_query_document_t *query,
                                           const sequel_statement_info_t *statement) {
  if (!statement || statement->operation != SEQUEL_OPERATION_SELECT || !sequel_query_projection_count(query))
    return 1;
  const sequel_projection_info_t *projection = sequel_query_projections(query);
  if (sequel_query_projection_count(query) > 1) {
    bool scalar_list = false;
    /* Scalar-subquery detection is intentionally bounded.  Each projection
     * range may end at the statement boundary, so searching from every item
     * can turn a wide projection list into quadratic work. */
    size_t detection_limit = sequel_query_projection_count(query) > 1024 ? 0 : sequel_query_projection_count(query);
    for (size_t i = 0; i < detection_limit; ++i) {
      const char *nested_select = sequel_summary_find_text(source.data + projection[i].range.start, "(SELECT");
      bool scoped_relation = false;
      const sequel_relation_info_t *relations = sequel_query_relations(query);
      for (size_t r = 0; r < sequel_query_relation_count(query); ++r)
        if ((relations[r].flags & SEQUEL_RELATION_FLAG_PROJECTION_SCOPE) &&
            sequel_source_range_contains(projection[i].range, relations[r].range)) {
          scoped_relation = true;
          break;
        }
      scalar_list = nested_select && (size_t)(nested_select - source.data) < projection[i].range.end && scoped_relation;
      if (scalar_list)
        break;
    }
    if (scalar_list) {
      for (size_t i = 0; i < sequel_query_projection_count(query); ++i) {
        const char *item = source.data + projection[i].range.start;
        const char *item_end = source.data + projection[i].range.end;
        const char *select = sequel_summary_find_text(item, "SELECT ");
        const char *from = select ? sequel_summary_find_text(select, " FROM ") : NULL;
        if (!select || !from || from >= item_end)
          continue;
        const char *table = from + 6;
        while (table < item_end && isspace((unsigned char)*table))
          ++table;
        const char *table_end = table;
        while (table_end < item_end && !isspace((unsigned char)*table_end) && *table_end != ')')
          ++table_end;
        const char *alias = table_end;
        while (alias < item_end && isspace((unsigned char)*alias))
          ++alias;
        const char *alias_end = alias;
        while (alias_end < item_end && !isspace((unsigned char)*alias_end) && *alias_end != ')')
          ++alias_end;
        const char *where = sequel_summary_find_text(table_end, " WHERE ");
        const char *order = sequel_summary_find_text(table_end, " ORDER BY ");
        const char *limit = sequel_summary_find_text(table_end, " LIMIT ");
        if (where && where >= item_end)
          where = NULL;
        if (order && order >= item_end)
          order = NULL;
        if (limit && limit >= item_end)
          limit = NULL;
        if (!sequel_summary_append_text(text, length, capacity, " (SELECT … FROM ") ||
            !sequel_summary_append_bytes(text, length, capacity, table, (size_t)(table_end - table)) ||
            (alias < item_end &&
             (!sequel_summary_append_text(text, length, capacity, " AS ") ||
              !sequel_summary_append_bytes(text, length, capacity, alias, (size_t)(alias_end - alias)))) ||
            (where && !sequel_summary_append_text(text, length, capacity, " WHERE …")) ||
            (order && !sequel_summary_append_text(text, length, capacity, " ORDER BY …")) ||
            (limit && !sequel_summary_append_text(text, length, capacity, " LIMIT 1")) ||
            !sequel_summary_append_text(text, length, capacity, ")"))
          return 0;
      }
      return 1;
    }
  }
  char first = projection[0].range.start < source.length ? source.data[projection[0].range.start] : 0;
  bool literal = strchr("'?0123456789", first) != NULL ||
                 (first == 'U' && projection[0].range.start + 1 < source.length &&
                  source.data[projection[0].range.start + 1] == '&') ||
                 (first == '$' && !sequel_summary_find_text(source.data, " FROM "));
  size_t projection_end = projection[0].range.end;
  const char *from_clause = sequel_summary_find_text(source.data + projection[0].range.start, " FROM ");
  if (from_clause && (size_t)(from_clause - source.data) < projection_end)
    projection_end = (size_t)(from_clause - source.data);
  while (projection_end > projection[0].range.start && isspace((unsigned char)source.data[projection_end - 1]))
    --projection_end;
  bool multiple =
      sequel_query_projection_count(query) > 1 ||
      memchr(source.data + projection[0].range.start, ',', projection_end - projection[0].range.start) != NULL;
  bool array_projection =
      projection[0].range.start + 5 <= source.length && !memcmp(source.data + projection[0].range.start, "ARRAY", 5);
  if (multiple && array_projection) {
    if (!sequel_summary_append_text(text, length, capacity, " ") ||
        !sequel_summary_append_bytes(text, length, capacity, ellipsis, sizeof(ellipsis) - 1))
      return 0;
    return 1;
  }
  bool scalar = false;
  unsigned projection_depth = 0;
  for (size_t offset = projection[0].range.start; offset < projection_end; ++offset) {
    char character = source.data[offset];
    if (character == '(') {
      ++projection_depth;
    } else if (character == ')' && projection_depth) {
      --projection_depth;
    } else if (projection_depth && sequel_summary_word(source.data, source.length, offset, "SELECT")) {
      scalar = true;
      break;
    }
  }
  if (!scalar && !sequel_summary_find_text(source.data, " FROM ") &&
      strchr(source.data + projection[0].range.start, '(') != NULL)
    scalar = true;
  const char *open = scalar ? strchr(source.data + projection[0].range.start, '(') : NULL;
  if (!sequel_summary_append_text(text, length, capacity, " "))
    return 0;
  const char *select_distinct = sequel_summary_find_text(source.data, "SELECT DISTINCT ON");
  size_t distinct = select_distinct ? (size_t)(select_distinct - source.data) + 7 : source.length;
  if (select_distinct && sequel_summary_word(source.data, source.length, distinct, "DISTINCT")) {
    size_t on = sequel_summary_skip_space(source.data, source.length, distinct + 8);
    if (sequel_summary_word(source.data, source.length, on, "ON"))
      return sequel_summary_append_text(text, length, capacity, "DISTINCT ON (…) ") &&
             sequel_summary_append_bytes(text, length, capacity, ellipsis, sizeof(ellipsis) - 1);
  }
  if (scalar) {
    if (!sequel_summary_append_bytes(text, length, capacity, source.data + projection[0].range.start,
                                     (size_t)(open - (source.data + projection[0].range.start) + 1)))
      return 0;
    if (open[1] == ')') {
      if (!sequel_summary_append_text(text, length, capacity, ")"))
        return 0;
    } else if (!sequel_summary_append_bytes(text, length, capacity, ellipsis, sizeof(ellipsis) - 1) ||
               !sequel_summary_append_text(text, length, capacity, ")"))
      return 0;
  } else if (literal && !multiple && sequel_query_relation_count(query) == 0
                 ? !sequel_summary_append_text(text, length, capacity, "?")
                 : !sequel_summary_append_bytes(text, length, capacity, ellipsis, sizeof(ellipsis) - 1))
    return 0;
  const char *alias = sequel_summary_find_text(source.data + projection[0].range.start, " AS ");
  const char *from = alias ? sequel_summary_find_text(alias + 4, " FROM ") : NULL;
  if (alias && (!from || alias < from) && (first == '\'' || scalar)) {
    const char *name = alias + 4;
    const char *end = name;
    while (end < source.data + source.length && !isspace((unsigned char)*end))
      ++end;
    if (!sequel_summary_append_text(text, length, capacity, " AS ") ||
        !sequel_summary_append_bytes(text, length, capacity, name, (size_t)(end - name)))
      return 0;
  }
  return 1;
}
