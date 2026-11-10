#include <ctype.h>
#include <string.h>

#include "internal.h"

static const char summary_ellipsis[] = "\342\200\246";
#define append_text sequel_summary_append_text
#define append_bytes sequel_summary_append_bytes
#define summary_word sequel_summary_word
#define summary_skip_space sequel_summary_skip_space

int sequel_summary_append_body_summary(char **text, size_t *length, size_t *capacity, const char *source,
                               sequel_source_range_t range) {
  size_t start = summary_skip_space(source, range.end, range.start);
  if (summary_word(source, range.end, start, "SELECT")) {
    size_t from = start;
    while (from < range.end && !summary_word(source, range.end, from, "FROM"))
      ++from;
    size_t grouped_from = from;
    while (grouped_from < range.end && source[grouped_from] != '(' &&
           !summary_word(source, range.end, grouped_from, "WHERE"))
      ++grouped_from;
    if (grouped_from < range.end && source[grouped_from] == '(' &&
        summary_word(source, range.end, summary_skip_space(source, range.end, grouped_from + 1), "WITH")) {
      size_t cursor = start;
      while (cursor < range.end) {
        if (source[cursor] == '*' && (cursor == 0 || !isalnum((unsigned char)source[cursor - 1])) &&
            (cursor + 1 == range.end || !isalnum((unsigned char)source[cursor + 1]))) {
          if (!append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
            return 0;
          ++cursor;
        } else if (summary_word(source, range.end, cursor, "true")) {
          if (!append_text(text, length, capacity, "?"))
            return 0;
          cursor += 4;
        } else if (source[cursor] == ')' && cursor + 1 < range.end) {
          if (!append_text(text, length, capacity, ") AS "))
            return 0;
          cursor++;
          while (cursor < range.end && isspace((unsigned char)source[cursor]))
            ++cursor;
        } else {
          if (!append_bytes(text, length, capacity, source + cursor, 1))
            return 0;
          ++cursor;
        }
      }
      return 1;
    }
    if (from >= range.end) {
      size_t expression = summary_skip_space(source, range.end, start + 6);
      const char *open = strchr(source + expression, '(');
      const char *close = open ? strchr(open, ')') : NULL;
      if (!open || !close || close >= source + range.end || !append_text(text, length, capacity, "SELECT ") ||
          !append_bytes(text, length, capacity, source + expression, (size_t)(open - (source + expression))) ||
          !append_text(text, length, capacity, "(") ||
          !append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          !append_text(text, length, capacity, ")"))
        return 0;
      return 1;
    }
    if (!append_text(text, length, capacity, "SELECT ") ||
        !append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
        !append_text(text, length, capacity, " FROM "))
      return 0;
    size_t table = summary_skip_space(source, range.end, from + 4);
    size_t table_end = table;
    while (table_end < range.end && !isspace((unsigned char)source[table_end]) && source[table_end] != ')')
      ++table_end;
    if (!append_bytes(text, length, capacity, source + table, table_end - table))
      return 0;
    size_t where = table_end;
    while (where < range.end && !summary_word(source, range.end, where, "WHERE"))
      ++where;
    if (where < range.end && (!append_text(text, length, capacity, " WHERE ") ||
                              !append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1)))
      return 0;
    const char *union_all = strstr(source + table_end, "UNION ALL");
    if (union_all && union_all < source + range.end) {
      const char *second = strstr(union_all, "SELECT ");
      const char *second_from = second ? strstr(second, " FROM ") : NULL;
      const char *join = second_from ? strstr(second_from, " JOIN ") : NULL;
      const char *on = join ? strstr(join, " ON ") : NULL;
      if (second && second_from && join && on && on < source + range.end) {
        const char *second_table = second_from + 6;
        while (*second_table && isspace((unsigned char)*second_table))
          ++second_table;
        const char *second_table_end = second_table;
        while (second_table_end < source + range.end && !isspace((unsigned char)*second_table_end))
          ++second_table_end;
        const char *second_alias = second_table_end;
        while (*second_alias && isspace((unsigned char)*second_alias))
          ++second_alias;
        const char *second_alias_end = second_alias;
        while (second_alias_end < source + range.end && !isspace((unsigned char)*second_alias_end))
          ++second_alias_end;
        const char *join_table = join + 6;
        while (*join_table && isspace((unsigned char)*join_table))
          ++join_table;
        const char *join_table_end = join_table;
        while (join_table_end < source + range.end && !isspace((unsigned char)*join_table_end))
          ++join_table_end;
        const char *join_alias = join_table_end;
        while (*join_alias && isspace((unsigned char)*join_alias))
          ++join_alias;
        const char *join_alias_end = join_alias;
        while (join_alias_end < source + range.end && !isspace((unsigned char)*join_alias_end))
          ++join_alias_end;
        if (!append_text(text, length, capacity, " UNION ALL SELECT ") ||
            !append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !append_text(text, length, capacity, " FROM ") ||
            !append_bytes(text, length, capacity, second_table, second_table_end - second_table) ||
            !append_text(text, length, capacity, " AS ") ||
            !append_bytes(text, length, capacity, second_alias, second_alias_end - second_alias) ||
            !append_text(text, length, capacity, " JOIN ") ||
            !append_bytes(text, length, capacity, join_table, join_table_end - join_table) ||
            !append_text(text, length, capacity, " AS ") ||
            !append_bytes(text, length, capacity, join_alias, join_alias_end - join_alias) ||
            !append_text(text, length, capacity, " ON ") ||
            !append_bytes(text, length, capacity, on + 4, (source + range.end) - (on + 4)))
          return 0;
      }
    }
    size_t order = table_end;
    while (order < range.end && !summary_word(source, range.end, order, "ORDER BY") &&
           !summary_word(source, range.end, order, "LIMIT"))
      ++order;
    if (order < range.end && summary_word(source, range.end, order, "ORDER BY") &&
        (!append_text(text, length, capacity, " ORDER BY ") ||
         !append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1)))
      return 0;
    size_t limit = order;
    while (limit < range.end && !summary_word(source, range.end, limit, "LIMIT"))
      ++limit;
    if (limit < range.end) {
      size_t value = summary_skip_space(source, range.end, limit + 5);
      size_t value_end = value;
      while (value_end < range.end && !isspace((unsigned char)source[value_end]) && source[value_end] != ')')
        ++value_end;
      if (!append_text(text, length, capacity, " LIMIT ") ||
          !append_text(text, length, capacity, (*value == '$' || *value == '?' ||
                                               (value_end - value >= 9 && !memcmp(value, "sqlc.arg(", 9)))
                                                  ? "?"
                                                  : "1"))
        return 0;
    }
    return 1;
  }
  if (summary_word(source, range.end, start, "DELETE")) {
    size_t from = start;
    while (from < range.end && !summary_word(source, range.end, from, "FROM"))
      ++from;
    size_t table = summary_skip_space(source, range.end, from + 4), end = table;
    while (end < range.end && !isspace((unsigned char)source[end]) && source[end] != ')')
      ++end;
    if (!append_text(text, length, capacity, "DELETE FROM ") ||
        !append_bytes(text, length, capacity, source + table, end - table))
      return 0;
    size_t where = end;
    while (where < range.end && !summary_word(source, range.end, where, "WHERE") &&
           !summary_word(source, range.end, where, "RETURNING"))
      ++where;
    if (where < range.end && summary_word(source, range.end, where, "WHERE") &&
        (!append_text(text, length, capacity, " WHERE ") ||
         !append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1)))
      return 0;
    while (where < range.end && !summary_word(source, range.end, where, "RETURNING"))
      ++where;
    return where >= range.end || (append_text(text, length, capacity, " RETURNING ") &&
                                  append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1));
  }
  if (summary_word(source, range.end, start, "UPDATE")) {
    size_t table = summary_skip_space(source, range.end, start + 6), end = table;
    while (end < range.end && !isspace((unsigned char)source[end]))
      ++end;
    if (!append_text(text, length, capacity, "UPDATE ") ||
        !append_bytes(text, length, capacity, source + table, end - table) ||
        !append_text(text, length, capacity, " SET ") ||
        !append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
      return 0;
    size_t returning = end;
    while (returning < range.end && !summary_word(source, range.end, returning, "RETURNING"))
      ++returning;
    return returning >= range.end ||
           (append_text(text, length, capacity, " RETURNING ") &&
            append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1));
  }
  if (summary_word(source, range.end, start, "INSERT")) {
    size_t into = start;
    while (into < range.end && !summary_word(source, range.end, into, "INTO"))
      ++into;
    size_t select = start;
    while (select < range.end && !summary_word(source, range.end, select, "SELECT"))
      ++select;
    size_t from = select;
    while (from < range.end && !summary_word(source, range.end, from, "FROM"))
      ++from;
    if (into < range.end && select < range.end && from < range.end) {
      size_t table = summary_skip_space(source, range.end, into + 4), table_end = table;
      while (table_end < range.end && !isspace((unsigned char)source[table_end]) && source[table_end] != '(')
        ++table_end;
      size_t relation = summary_skip_space(source, range.end, from + 4), relation_end = relation;
      while (relation_end < range.end && !isspace((unsigned char)source[relation_end]) && source[relation_end] != ')')
        ++relation_end;
      if (!append_text(text, length, capacity, "INSERT INTO ") ||
          !append_bytes(text, length, capacity, source + table, table_end - table) ||
          !append_text(text, length, capacity, " SELECT ") ||
          !append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          !append_text(text, length, capacity, " FROM ") ||
          !append_bytes(text, length, capacity, source + relation, relation_end - relation))
        return 0;
    } else if (!append_text(text, length, capacity, "INSERT ") ||
               !append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
      return 0;
    size_t returning = start;
    while (returning < range.end && !summary_word(source, range.end, returning, "RETURNING"))
      ++returning;
    return returning >= range.end ||
           (append_text(text, length, capacity, " RETURNING ") &&
            append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1));
  }
  if (summary_word(source, range.end, start, "VALUES"))
    return append_text(text, length, capacity, "VALUES ") &&
           append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1);
  return append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1);
}
