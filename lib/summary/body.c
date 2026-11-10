#include <ctype.h>
#include <string.h>

#include "internal.h"

static const char summary_ellipsis[] = "\342\200\246";

int sequel_summary_append_body_summary(char **text, size_t *length, size_t *capacity, const char *source,
                                       sequel_source_range_t range, bool preserve_limit) {
  size_t start = sequel_summary_skip_space(source, range.end, range.start);
  if (!sequel_summary_word(source, range.end, start, "SELECT") &&
      !sequel_summary_word(source, range.end, start, "INSERT") &&
      !sequel_summary_word(source, range.end, start, "UPDATE") &&
      !sequel_summary_word(source, range.end, start, "DELETE")) {
    const char *keywords[] = {"SELECT", "INSERT", "UPDATE", "DELETE"};
    const char *first = NULL;
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); ++i) {
      const char *candidate = sequel_summary_find_word(source, range.end, start, keywords[i]);
      if (candidate && (!first || candidate < first))
        first = candidate;
    }
    if (first)
      start = (size_t)(first - source);
  }
  if (sequel_summary_word(source, range.end, start, "SELECT")) {
    size_t from = start;
    while (from < range.end && !sequel_summary_word(source, range.end, from, "FROM"))
      ++from;
    size_t grouped_from = from;
    while (grouped_from < range.end && source[grouped_from] != '(' &&
           !sequel_summary_word(source, range.end, grouped_from, "WHERE"))
      ++grouped_from;
    if (grouped_from < range.end && source[grouped_from] == '(' &&
        sequel_summary_word(source, range.end, sequel_summary_skip_space(source, range.end, grouped_from + 1),
                            "WITH")) {
      size_t cursor = start;
      while (cursor < range.end) {
        if (source[cursor] == '*' && (cursor == 0 || !isalnum((unsigned char)source[cursor - 1])) &&
            (cursor + 1 == range.end || !isalnum((unsigned char)source[cursor + 1]))) {
          if (!sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
            return 0;
          ++cursor;
        } else if (sequel_summary_word(source, range.end, cursor, "true")) {
          if (!sequel_summary_append_text(text, length, capacity, "?"))
            return 0;
          cursor += 4;
        } else if (source[cursor] == ')' && cursor + 1 < range.end) {
          if (!sequel_summary_append_text(text, length, capacity, ") AS "))
            return 0;
          cursor++;
          while (cursor < range.end && isspace((unsigned char)source[cursor]))
            ++cursor;
        } else {
          if (!sequel_summary_append_bytes(text, length, capacity, source + cursor, 1))
            return 0;
          ++cursor;
        }
      }
      return 1;
    }
    if (from >= range.end) {
      size_t expression = sequel_summary_skip_space(source, range.end, start + 6);
      const char *open = strchr(source + expression, '(');
      const char *close = open ? strchr(open, ')') : NULL;
      if (!open || !close || close >= source + range.end ||
          !sequel_summary_append_text(text, length, capacity, "SELECT ") ||
          !sequel_summary_append_bytes(text, length, capacity, source + expression,
                                       (size_t)(open - (source + expression))) ||
          !sequel_summary_append_text(text, length, capacity, "(") ||
          !sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          !sequel_summary_append_text(text, length, capacity, ")"))
        return 0;
      return 1;
    }
    if (!sequel_summary_append_text(text, length, capacity, "SELECT ") ||
        !sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
        !sequel_summary_append_text(text, length, capacity, " FROM "))
      return 0;
    size_t table = sequel_summary_skip_space(source, range.end, from + 4);
    size_t table_end = table;
    while (table_end < range.end && !isspace((unsigned char)source[table_end]) && source[table_end] != ')')
      ++table_end;
    if (!sequel_summary_append_bytes(text, length, capacity, source + table, table_end - table))
      return 0;
    const char *union_all = sequel_summary_find_text(source + table_end, "UNION ALL");
    if (union_all && union_all < source + range.end) {
      const char *first_join = sequel_summary_find_text(source + table_end, " JOIN ");
      const char *first_on = first_join ? sequel_summary_find_text(first_join, " ON ") : NULL;
      if (first_join && first_join < union_all && first_on && first_on < union_all) {
        const char *alias = source + table_end;
        while (alias < first_join && isspace((unsigned char)*alias))
          ++alias;
        const char *alias_end = alias;
        while (alias_end < first_join && !isspace((unsigned char)*alias_end))
          ++alias_end;
        const char *join_table = first_join + 6;
        while (join_table < first_on && isspace((unsigned char)*join_table))
          ++join_table;
        const char *join_table_end = join_table;
        while (join_table_end < first_on && !isspace((unsigned char)*join_table_end))
          ++join_table_end;
        const char *join_alias = join_table_end;
        while (join_alias < first_on && isspace((unsigned char)*join_alias))
          ++join_alias;
        const char *join_alias_end = join_alias;
        while (join_alias_end < first_on && !isspace((unsigned char)*join_alias_end))
          ++join_alias_end;
        if (!sequel_summary_append_text(text, length, capacity, " AS ") ||
            !sequel_summary_append_bytes(text, length, capacity, alias, alias_end - alias) ||
            !sequel_summary_append_text(text, length, capacity, " JOIN ") ||
            !sequel_summary_append_bytes(text, length, capacity, join_table, join_table_end - join_table) ||
            !sequel_summary_append_text(text, length, capacity, " AS ") ||
            !sequel_summary_append_bytes(text, length, capacity, join_alias, join_alias_end - join_alias) ||
            !sequel_summary_append_text(text, length, capacity, " ON ") ||
            !sequel_summary_append_bytes(text, length, capacity, first_on + 4, (size_t)(union_all - (first_on + 4))))
          return 0;
      }
    }
    size_t where = table_end;
    while (where < range.end && (!union_all || where < (size_t)(union_all - source)) &&
           !sequel_summary_word(source, range.end, where, "WHERE"))
      ++where;
    const char *first_join = sequel_summary_find_text(source + table_end, " JOIN ");
    if (first_join && (!where || first_join < source + where) &&
        !sequel_summary_append_bytes(text, length, capacity, source + table_end, (size_t)(where - table_end)))
      return 0;
    if (!union_all && where < range.end &&
        (!sequel_summary_append_text(text, length, capacity, " WHERE ") ||
         !sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1)))
      return 0;
    if (union_all && union_all < source + range.end) {
      const char *second = sequel_summary_find_text(union_all, "SELECT ");
      const char *second_from = second ? sequel_summary_find_text(second, " FROM ") : NULL;
      const char *join = second_from ? sequel_summary_find_text(second_from, " JOIN ") : NULL;
      const char *on = join ? sequel_summary_find_text(join, " ON ") : NULL;
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
        if (!sequel_summary_append_text(text, length, capacity, " UNION ALL SELECT ") ||
            !sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !sequel_summary_append_text(text, length, capacity, " FROM ") ||
            !sequel_summary_append_bytes(text, length, capacity, second_table, second_table_end - second_table) ||
            !sequel_summary_append_text(text, length, capacity, " AS ") ||
            !sequel_summary_append_bytes(text, length, capacity, second_alias, second_alias_end - second_alias) ||
            !sequel_summary_append_text(text, length, capacity, " JOIN ") ||
            !sequel_summary_append_bytes(text, length, capacity, join_table, join_table_end - join_table) ||
            !sequel_summary_append_text(text, length, capacity, " AS ") ||
            !sequel_summary_append_bytes(text, length, capacity, join_alias, join_alias_end - join_alias) ||
            !sequel_summary_append_text(text, length, capacity, " ON ") ||
            !sequel_summary_append_bytes(text, length, capacity, on + 4, (source + range.end) - (on + 4)))
          return 0;
      }
    }
    size_t order = table_end;
    while (order < range.end && !sequel_summary_word(source, range.end, order, "ORDER BY") &&
           !sequel_summary_word(source, range.end, order, "LIMIT"))
      ++order;
    if (order < range.end && sequel_summary_word(source, range.end, order, "ORDER BY") &&
        (!sequel_summary_append_text(text, length, capacity, " ORDER BY ") ||
         !sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1)))
      return 0;
    size_t limit = order;
    while (limit < range.end && !sequel_summary_word(source, range.end, limit, "LIMIT"))
      ++limit;
    if (limit < range.end) {
      size_t value = sequel_summary_skip_space(source, range.end, limit + 5);
      size_t value_end = value;
      while (value_end < range.end && !isspace((unsigned char)source[value_end]) && source[value_end] != ')')
        ++value_end;
      bool parameter = source[value] == '$' || source[value] == '?' ||
                       (value_end - value >= 9 && !memcmp(source + value, "sqlc.arg(", 9));
      char numeric[32];
      size_t numeric_length = value_end - value;
      if (numeric_length >= sizeof(numeric))
        return 0;
      memcpy(numeric, source + value, numeric_length);
      numeric[numeric_length] = 0;
      if (!sequel_summary_append_text(text, length, capacity, " LIMIT ") ||
          !sequel_summary_append_text(text, length, capacity,
                                      preserve_limit && !parameter ? numeric
                                      : parameter                  ? "?"
                                                                   : "1"))
        return 0;
    }
    return 1;
  }
  if (sequel_summary_word(source, range.end, start, "DELETE")) {
    size_t from = start;
    while (from < range.end && !sequel_summary_word(source, range.end, from, "FROM"))
      ++from;
    size_t table = sequel_summary_skip_space(source, range.end, from + 4), end = table;
    while (end < range.end && !isspace((unsigned char)source[end]) && source[end] != ')')
      ++end;
    if (!sequel_summary_append_text(text, length, capacity, "DELETE FROM ") ||
        !sequel_summary_append_bytes(text, length, capacity, source + table, end - table))
      return 0;
    size_t where = end;
    while (where < range.end && !sequel_summary_word(source, range.end, where, "WHERE") &&
           !sequel_summary_word(source, range.end, where, "RETURNING"))
      ++where;
    if (where < range.end && sequel_summary_word(source, range.end, where, "WHERE") &&
        (!sequel_summary_append_text(text, length, capacity, " WHERE ") ||
         !sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1)))
      return 0;
    while (where < range.end && !sequel_summary_word(source, range.end, where, "RETURNING"))
      ++where;
    return where >= range.end ||
           (sequel_summary_append_text(text, length, capacity, " RETURNING ") &&
            sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1));
  }
  if (sequel_summary_word(source, range.end, start, "UPDATE")) {
    size_t table = sequel_summary_skip_space(source, range.end, start + 6), end = table;
    while (end < range.end && !isspace((unsigned char)source[end]))
      ++end;
    if (!sequel_summary_append_text(text, length, capacity, "UPDATE ") ||
        !sequel_summary_append_bytes(text, length, capacity, source + table, end - table) ||
        !sequel_summary_append_text(text, length, capacity, " SET ") ||
        !sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
      return 0;
    size_t returning = end;
    while (returning < range.end && !sequel_summary_word(source, range.end, returning, "RETURNING"))
      ++returning;
    return returning >= range.end ||
           (sequel_summary_append_text(text, length, capacity, " RETURNING ") &&
            sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1));
  }
  if (sequel_summary_word(source, range.end, start, "INSERT")) {
    size_t into = start;
    while (into < range.end && !sequel_summary_word(source, range.end, into, "INTO"))
      ++into;
    size_t select = start;
    while (select < range.end && !sequel_summary_word(source, range.end, select, "SELECT"))
      ++select;
    size_t from = select;
    while (from < range.end && !sequel_summary_word(source, range.end, from, "FROM"))
      ++from;
    if (into < range.end && select < range.end && from < range.end) {
      size_t table = sequel_summary_skip_space(source, range.end, into + 4), table_end = table;
      while (table_end < range.end && !isspace((unsigned char)source[table_end]) && source[table_end] != '(')
        ++table_end;
      size_t relation = sequel_summary_skip_space(source, range.end, from + 4), relation_end = relation;
      while (relation_end < range.end && !isspace((unsigned char)source[relation_end]) && source[relation_end] != ')')
        ++relation_end;
      if (!sequel_summary_append_text(text, length, capacity, "INSERT INTO ") ||
          !sequel_summary_append_bytes(text, length, capacity, source + table, table_end - table) ||
          !sequel_summary_append_text(text, length, capacity, " SELECT ") ||
          !sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          !sequel_summary_append_text(text, length, capacity, " FROM ") ||
          !sequel_summary_append_bytes(text, length, capacity, source + relation, relation_end - relation))
        return 0;
    } else if (!sequel_summary_append_text(text, length, capacity, "INSERT ") ||
               !sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
      return 0;
    size_t returning = start;
    while (returning < range.end && !sequel_summary_word(source, range.end, returning, "RETURNING"))
      ++returning;
    return returning >= range.end ||
           (sequel_summary_append_text(text, length, capacity, " RETURNING ") &&
            sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1));
  }
  if (sequel_summary_word(source, range.end, start, "VALUES"))
    return sequel_summary_append_text(text, length, capacity, "VALUES ") &&
           sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1);
  return sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1);
}
