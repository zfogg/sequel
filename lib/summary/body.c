#include <ctype.h>
#include <string.h>

#include "internal.h"

static const char summary_ellipsis[] = "\342\200\246";

static size_t summary_matching_parenthesis(const char *source, size_t end, size_t open) {
  size_t depth = 0;
  for (size_t cursor = open; cursor < end; ++cursor) {
    if (source[cursor] == '(')
      ++depth;
    else if (source[cursor] == ')' && depth && --depth == 0)
      return cursor;
  }
  return end;
}

static int sequel_summary_append_exists(char **text, size_t *length, size_t *capacity, const char *source, size_t end,
                                        size_t expression) {
  const char *not_exists = sequel_summary_word(source, end, expression, "NOT") ? "NOT EXISTS (" : "EXISTS (";
  size_t open = expression + (not_exists[0] == 'N' ? 10 : 7);
  while (open < end && isspace((unsigned char)source[open]))
    ++open;
  if (open >= end || source[open] != '(')
    return 1;
  size_t close = summary_matching_parenthesis(source, end, open);
  const char *select_match = sequel_summary_find_word(source, close, open + 1, "SELECT");
  size_t select = select_match ? (size_t)(select_match - source) : close;
  if (select >= close)
    return 1;
  const char *from_match = sequel_summary_find_word(source, close, select, "FROM");
  size_t from = from_match ? (size_t)(from_match - source) : close;
  if (from >= close)
    return 1;
  size_t table = sequel_summary_skip_space(source, close, from + 4), table_end = table;
  while (table_end < close && !isspace((unsigned char)source[table_end]) && source[table_end] != ')')
    ++table_end;
  size_t alias = sequel_summary_skip_space(source, close, table_end), alias_end = alias;
  while (alias_end < close && !isspace((unsigned char)source[alias_end]) && source[alias_end] != ')')
    ++alias_end;
  if (!sequel_summary_append_text(text, length, capacity, " ") ||
      !sequel_summary_append_text(text, length, capacity, not_exists) ||
      !sequel_summary_append_text(text, length, capacity, "SELECT ") ||
      !sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
      !sequel_summary_append_text(text, length, capacity, " FROM ") ||
      !sequel_summary_append_bytes(text, length, capacity, source + table, table_end - table) ||
      (alias < alias_end && (!sequel_summary_append_text(text, length, capacity, " AS ") ||
                             !sequel_summary_append_bytes(text, length, capacity, source + alias, alias_end - alias))))
    return 0;
  size_t cursor = alias_end;
  while (cursor < close) {
    const char *join = sequel_summary_find_text(source + cursor, " JOIN ");
    if (!join || join >= source + close)
      break;
    size_t join_offset = (size_t)(join - source);
    size_t join_table = sequel_summary_skip_space(source, close, join_offset + 6), join_table_end = join_table;
    while (join_table_end < close && !isspace((unsigned char)source[join_table_end]))
      ++join_table_end;
    size_t join_alias = sequel_summary_skip_space(source, close, join_table_end), join_alias_end = join_alias;
    while (join_alias_end < close && !isspace((unsigned char)source[join_alias_end]))
      ++join_alias_end;
    const char *on_match = sequel_summary_find_word(source, close, join_alias_end, "ON");
    size_t on = on_match ? (size_t)(on_match - source) : close;
    if (on >= close)
      break;
    const char *next_match = sequel_summary_find_word(source, close, on + 2, "WHERE");
    size_t next = next_match ? (size_t)(next_match - source) : close;
    if (next >= close)
      next = close;
    if (!sequel_summary_append_text(text, length, capacity, " JOIN ") ||
        !sequel_summary_append_bytes(text, length, capacity, source + join_table, join_table_end - join_table) ||
        (join_alias < join_alias_end &&
         (!sequel_summary_append_text(text, length, capacity, " AS ") ||
          !sequel_summary_append_bytes(text, length, capacity, source + join_alias, join_alias_end - join_alias))) ||
        !sequel_summary_append_text(text, length, capacity, " ON ") ||
        !sequel_summary_append_bytes(text, length, capacity, source + on + 2,
                                     (next > on + 2 ? next : close) - (on + 2)))
      return 0;
    cursor = next;
  }
  const char *where_match = sequel_summary_find_word(source, close, cursor, "WHERE");
  size_t where = where_match ? (size_t)(where_match - source) : close;
  if (where < close &&
      (!sequel_summary_append_text(text, length, capacity, " WHERE ") ||
       !sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1)))
    return 0;
  const char *offset_match = sequel_summary_find_word(source, close, cursor, "OFFSET");
  if (offset_match) {
    size_t offset = (size_t)(offset_match - source);
    size_t value = sequel_summary_skip_space(source, close, offset + strlen("OFFSET"));
    size_t value_end = value;
    while (value_end < close && !isspace((unsigned char)source[value_end]) && source[value_end] != ')')
      ++value_end;
    if (!sequel_summary_append_text(text, length, capacity, " OFFSET ") ||
        !sequel_summary_append_bytes(text, length, capacity, source + value, value_end - value))
      return 0;
  }
  return sequel_summary_append_text(text, length, capacity, ")");
}

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
    size_t distinct = sequel_summary_skip_space(source, range.end, start + 6);
    bool distinct_on =
        sequel_summary_word(source, range.end, distinct, "DISTINCT") &&
        sequel_summary_word(source, range.end, sequel_summary_skip_space(source, range.end, distinct + 8), "ON");
    const char *select_prefix = distinct_on ? "SELECT DISTINCT ON (…) " : "SELECT ";
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
      if (!open || !close || close >= source + range.end) {
        if (!sequel_summary_append_text(text, length, capacity, select_prefix) ||
            !sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
          return 0;
        return 1;
      }
      if (!sequel_summary_append_text(text, length, capacity, select_prefix) ||
          !sequel_summary_append_bytes(text, length, capacity, source + expression,
                                       (size_t)(open - (source + expression))) ||
          !sequel_summary_append_text(text, length, capacity, "(") ||
          !sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          !sequel_summary_append_text(text, length, capacity, ")"))
        return 0;
      return 1;
    }
    if (!sequel_summary_append_text(text, length, capacity, select_prefix) ||
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
    if (!union_all && where < range.end) {
      if (!sequel_summary_append_text(text, length, capacity, " WHERE ") ||
          !sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
        return 0;
      size_t cursor = where;
      while (cursor < range.end) {
        const char *exists = sequel_summary_find_text(source + cursor, "EXISTS (");
        if (!exists || exists >= source + range.end)
          break;
        size_t exists_offset = (size_t)(exists - source);
        size_t expression = exists_offset;
        if (exists_offset >= 4 && !memcmp(source + exists_offset - 4, "NOT ", 4))
          expression -= 4;
        if (!sequel_summary_append_exists(text, length, capacity, source, range.end, expression))
          return 0;
        cursor = exists_offset + 8;
      }
    }
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

      /* A lateral relation is part of the body range, so its closing alias
       * and join predicate must survive the limit normalization. */
      if (table < range.end && sequel_summary_find_text(source + table, "JOIN LATERAL")) {
        size_t close = value_end;
        while (close < range.end && source[close] != ')')
          ++close;
        if (close < range.end) {
          size_t alias = sequel_summary_skip_space(source, range.end, close + 1);
          size_t alias_end = alias;
          while (alias_end < range.end && !isspace((unsigned char)source[alias_end]) && source[alias_end] != ')')
            ++alias_end;
          if (alias < alias_end &&
              (!sequel_summary_append_text(text, length, capacity, ") AS ") ||
               !sequel_summary_append_bytes(text, length, capacity, source + alias, alias_end - alias) ||
               !sequel_summary_append_text(text, length, capacity, " ON ?")))
            return 0;
        }
      }
    }
    return 1;
  }
  if (sequel_summary_word(source, range.end, start, "DELETE")) {
    const char *using_match = sequel_summary_find_word(source, range.end, start, "USING");
    const char *from_match = sequel_summary_find_word(source, range.end, start, "FROM");
    if (using_match && (!from_match || using_match < from_match)) {
      size_t using = (size_t)(using_match - source);
      size_t table = sequel_summary_skip_space(source, range.end, start + 6), table_end = table;
      while (table_end < range.end && !isspace((unsigned char)source[table_end]))
        ++table_end;
      size_t relation = sequel_summary_skip_space(source, range.end, using + 5), relation_end = relation;
      while (relation_end < range.end && !isspace((unsigned char)source[relation_end]))
        ++relation_end;
      size_t alias = sequel_summary_skip_space(source, range.end, relation_end), alias_end = alias;
      while (alias_end < range.end && !isspace((unsigned char)source[alias_end]))
        ++alias_end;
      if (!sequel_summary_append_text(text, length, capacity, "DELETE FROM ") ||
          !sequel_summary_append_bytes(text, length, capacity, source + table, table_end - table) ||
          !sequel_summary_append_text(text, length, capacity, " USING ") ||
          !sequel_summary_append_bytes(text, length, capacity, source + relation, relation_end - relation) ||
          (alias < alias_end &&
           (!sequel_summary_append_text(text, length, capacity, " AS ") ||
            !sequel_summary_append_bytes(text, length, capacity, source + alias, alias_end - alias))))
        return 0;
      size_t where = alias_end;
      while (where < range.end && !sequel_summary_word(source, range.end, where, "WHERE"))
        ++where;
      return where >= range.end ||
             (sequel_summary_append_text(text, length, capacity, " WHERE ") &&
              sequel_summary_append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1));
    }
    size_t from = start;
    while (from < range.end && !sequel_summary_word(source, range.end, from, "FROM"))
      ++from;
    size_t table = sequel_summary_skip_space(source, range.end, from + 4), end = table;
    while (end < range.end && !isspace((unsigned char)source[end]) && source[end] != ')')
      ++end;
    if (!sequel_summary_append_text(text, length, capacity, "DELETE FROM ") ||
        !sequel_summary_append_bytes(text, length, capacity, source + table, end - table))
      return 0;
    size_t using = end;
    while (using < range.end && !sequel_summary_word(source, range.end, using, "USING") &&
           !sequel_summary_word(source, range.end, using, "WHERE"))
      ++using;
    if (using < range.end && sequel_summary_word(source, range.end, using, "USING")) {
      size_t relation = sequel_summary_skip_space(source, range.end, using + 5), relation_end = relation;
      while (relation_end < range.end && !isspace((unsigned char)source[relation_end]))
        ++relation_end;
      size_t alias = sequel_summary_skip_space(source, range.end, relation_end), alias_end = alias;
      while (alias_end < range.end && !isspace((unsigned char)source[alias_end]))
        ++alias_end;
      if (!sequel_summary_append_text(text, length, capacity, " USING ") ||
          !sequel_summary_append_bytes(text, length, capacity, source + relation, relation_end - relation) ||
          (alias < alias_end &&
           (!sequel_summary_append_text(text, length, capacity, " AS ") ||
            !sequel_summary_append_bytes(text, length, capacity, source + alias, alias_end - alias))))
        return 0;
    }
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
