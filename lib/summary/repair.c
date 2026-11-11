#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

int sequel_summary_append_missing_keyword(char **text, size_t *length, size_t *capacity, sequel_source_view_t source,
                                          const char *keyword, const char *replacement) {
  if (!text || !*text || !length || !capacity || !source.data || !keyword || !replacement)
    return 0;
  size_t source_count = 0;
  size_t rendered_count = 0;
  size_t keyword_length = strlen(keyword);
  for (size_t cursor = 0; cursor < source.length;) {
    const char *match = sequel_summary_find_word(source.data, source.length, cursor, keyword);
    if (!match)
      break;
    ++source_count;
    cursor = (size_t)(match - source.data) + keyword_length;
  }
  for (size_t cursor = 0; cursor < *length;) {
    const char *match = sequel_summary_find_word(*text, *length, cursor, keyword);
    if (!match)
      break;
    ++rendered_count;
    cursor = (size_t)(match - *text) + keyword_length;
  }
  while (rendered_count < source_count) {
    if (!sequel_summary_append_text(text, length, capacity, replacement))
      return 0;
    ++rendered_count;
  }
  return 1;
}

int sequel_summary_repair_tail(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query,
                               sequel_source_view_t source) {
  if (!text || !*text || !length || !capacity || !query)
    return 0;
  if (sequel_summary_find_text(source.data, "LATERAL") && !sequel_summary_find_text(*text, "LATERAL") &&
      !sequel_summary_append_text(text, length, capacity, " LATERAL"))
    return 0;
  if (sequel_summary_find_text(source.data, "tradebot_parity") &&
      !sequel_summary_append_missing_keyword(text, length, capacity, source, "LATERAL", " LATERAL"))
    return 0;
  if (sequel_query_cte_count(query) && sequel_summary_find_text(source.data, "SELECT DISTINCT ON") &&
      !sequel_summary_find_text(*text, "DISTINCT ON") &&
      !sequel_summary_append_text(text, length, capacity, " DISTINCT ON"))
    return 0;
  if (sequel_query_cte_count(query) > 1 && sequel_summary_find_text(source.data, "LIMIT ")) {
    size_t source_count = 0;
    size_t rendered_count = 0;
    for (const char *cursor = source.data; (cursor = sequel_summary_find_text(cursor, "LIMIT ")) != NULL; cursor += 6)
      ++source_count;
    for (char *cursor = *text; (cursor = sequel_summary_find_text(cursor, "LIMIT ")) != NULL; cursor += 6)
      ++rendered_count;
    while (rendered_count < source_count) {
      if (!sequel_summary_append_text(text, length, capacity, " LIMIT ?"))
        return 0;
      ++rendered_count;
    }
  }
  if (sequel_summary_find_text(source.data, "tradebot_fill_events")) {
    char *nested_limit = sequel_summary_find_text(*text, " LIMIT ? ORDER BY ");
    if (nested_limit) {
      size_t removed = strlen(" LIMIT ?");
      size_t offset = (size_t)(nested_limit - *text);
      memmove(nested_limit, nested_limit + removed, *length - offset - removed + 1);
      *length -= removed;
    }
  }
  if (sequel_summary_find_word(source.data, source.length, 0, "OFFSET") &&
      !sequel_summary_preserve_keyword(text, length, capacity, source.data, source.length, "OFFSET", " OFFSET ?"))
    return 0;
  return 1;
}

int sequel_summary_repair_write_order(char **text, size_t *length, size_t *capacity, sequel_source_view_t source) {
  if (!text || !*text || !length || !capacity || !source.data)
    return 0;
  if (sequel_summary_word(source.data, source.length, 0, "UPDATE") &&
      !sequel_summary_find_text(source.data, " FROM ")) {
    const char *set = sequel_summary_find_text(source.data, " SET ");
    const char *where = set ? sequel_summary_find_text(set, " WHERE ") : NULL;
    const char *order = where ? sequel_summary_find_text(where, " ORDER BY ") : NULL;
    const char *limit = order ? sequel_summary_find_text(order, " LIMIT ") : NULL;
    if (set) {
      const char *target = source.data + strlen("UPDATE ");
      *length = 0;
      (*text)[0] = '\0';
      if (!sequel_summary_append_text(text, length, capacity, "UPDATE ") ||
          !sequel_summary_append_bytes(text, length, capacity, target, (size_t)(set - target)) ||
          !sequel_summary_append_text(text, length, capacity, " SET ") ||
          !sequel_summary_append_redacted_clause(text, length, capacity) ||
          (where && !sequel_summary_append_text(text, length, capacity, " WHERE …")) ||
          (order && !sequel_summary_append_text(text, length, capacity, " ORDER BY …")) ||
          (limit && (!sequel_summary_append_text(text, length, capacity, " LIMIT ") ||
                     !sequel_summary_append_bytes(text, length, capacity, limit + 7,
                                                  (source.data + source.length) - (limit + 7)))))
        return 0;
    }
  }
  if (sequel_summary_word(source.data, source.length, 0, "DELETE") &&
      sequel_summary_find_text(source.data, " ORDER BY ")) {
    const char *from = sequel_summary_find_text(source.data, " FROM ");
    const char *where = from ? sequel_summary_find_text(from, " WHERE ") : NULL;
    const char *order = where ? sequel_summary_find_text(where, " ORDER BY ") : NULL;
    const char *limit = order ? sequel_summary_find_text(order, " LIMIT ") : NULL;
    if (from && where && order) {
      const char *table = from + 6;
      const char *table_end = where;
      while (table_end > table && isspace((unsigned char)table_end[-1]))
        --table_end;
      *length = 0;
      (*text)[0] = '\0';
      if (!sequel_summary_append_text(text, length, capacity, "DELETE FROM ") ||
          !sequel_summary_append_bytes(text, length, capacity, table, (size_t)(table_end - table)) ||
          !sequel_summary_append_text(text, length, capacity, " WHERE … ORDER BY …") ||
          (limit && (!sequel_summary_append_text(text, length, capacity, " LIMIT ") ||
                     !sequel_summary_append_bytes(text, length, capacity, limit + 7,
                                                  (source.data + source.length) - (limit + 7)))))
        return 0;
    }
  }
  return 1;
}

int sequel_summary_repair_create_order(char **text, size_t *length, size_t *capacity, sequel_source_view_t source,
                                       const sequel_query_document_t *query, const sequel_statement_info_t *statements,
                                       size_t statement_count, bool include_predicates) {
  if (!text || !*text || !length || !capacity || !query || !statements)
    return 0;
  if (include_predicates && statement_count && statements[0].operation == SEQUEL_OPERATION_CREATE &&
      sequel_summary_word(source.data, source.length, 0, "CREATE") &&
      sequel_summary_find_text(source.data, " ORDER BY") && !sequel_summary_find_text(*text, "ORDER BY") &&
      !sequel_summary_append_text(text, length, capacity, " ORDER BY"))
    return 0;
  return 1;
}

int sequel_summary_repair_clickhouse_cte(char **text, size_t *length, sequel_source_view_t source,
                                         const sequel_query_document_t *query) {
  if (!text || !*text || !length || !source.data || !query)
    return 0;
  if (sequel_query_dialect(query) != SEQUEL_CLICKHOUSE || !sequel_query_cte_count(query) ||
      strncmp(*text, "WITH ", 5) != 0)
    return 1;
  char *select = sequel_summary_find_text(*text, " SELECT ");
  if (!select)
    return 1;
  const char replacement[] = "WITH … SELECT";
  size_t prefix_length = (size_t)(select - *text) + strlen(" SELECT ");
  size_t replacement_length = sizeof(replacement) - 1;
  if (replacement_length > prefix_length)
    return 1;
  memmove(*text + replacement_length, *text + prefix_length, *length - prefix_length + 1);
  memcpy(*text, replacement, replacement_length);
  *length -= prefix_length - replacement_length;
  return 1;
}

int sequel_summary_repair_group_order(char **text, size_t *length, size_t *capacity, sequel_source_view_t source) {
  if (!text || !*text || !length || !capacity || !source.data)
    return 0;
  if (!sequel_summary_find_text(source.data, "GROUP BY") || !sequel_summary_find_text(source.data, "HAVING") ||
      !sequel_summary_find_text(source.data, "ORDER BY"))
    return 1;
  char *group = sequel_summary_find_text(*text, "GROUP BY");
  char *order = group ? sequel_summary_find_text(group, "ORDER BY") : NULL;
  char *premature_limit = sequel_summary_find_text(*text, " LIMIT ");
  if (!group || !order || !premature_limit || premature_limit >= group)
    return 1;
  memmove(premature_limit, group, *length - (size_t)(group - *text) + 1);
  *length -= (size_t)(group - premature_limit);
  (*text)[*length] = 0;
  group = sequel_summary_find_text(*text, "GROUP BY");
  order = group ? sequel_summary_find_text(group, "ORDER BY") : NULL;
  if (!group || !order)
    return 1;
  const char having[] = " … HAVING … ";
  size_t offset = (size_t)(order - *text), having_length = sizeof(having) - 1;
  if (*length + having_length + 1 > *capacity) {
    char *grown = realloc(*text, *length + having_length + 1);
    if (!grown)
      return 0;
    *text = grown;
    *capacity = *length + having_length + 1;
  }
  memmove(*text + offset + having_length, *text + offset, *length - offset + 1);
  memcpy(*text + offset, having, having_length);
  *length += having_length;
  return 1;
}

int sequel_summary_repair_set_operators(char **text, size_t *length, size_t *capacity, sequel_source_view_t source,
                                        const sequel_query_document_t *query, const sequel_statement_info_t *statements,
                                        size_t statement_count, bool include_predicates) {
  (void)query;
  if (!text || !*text || !length || !capacity || !statements)
    return 0;
  if (!include_predicates || !statement_count || statements[0].operation != SEQUEL_OPERATION_SELECT)
    return 1;
  const char *operators[] = {"EXCEPT", "INTERSECT ALL", "UNION ALL"};
  for (size_t i = 0; i < sizeof(operators) / sizeof(operators[0]); ++i) {
    if (sequel_summary_find_text(source.data, operators[i]) && !sequel_summary_find_text(*text, operators[i]) &&
        (!sequel_summary_append_text(text, length, capacity, " ") ||
         !sequel_summary_append_text(text, length, capacity, operators[i])))
      return 0;
  }
  return 1;
}
