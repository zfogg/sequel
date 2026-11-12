#include <ctype.h>
#include <string.h>

#include "internal.h"

static const char *sequel_summary_next_join_marker(const char *source, const char *end) {
  static const char *markers[] = {" JOIN ",       " LEFT JOIN ",  " RIGHT JOIN ",      " FULL JOIN ",
                                  " CROSS JOIN ", " INNER JOIN ", " NATURAL JOIN ",    " SEMI JOIN ",
                                  " ANTI JOIN ",  " ASOF JOIN ",  " POSITIONAL JOIN ", NULL};
  const char *best = NULL;
  for (size_t i = 0; markers[i]; ++i) {
    const char *candidate = sequel_summary_find_text(source, markers[i]);
    if (candidate && candidate < end && (!best || candidate < best))
      best = candidate;
  }
  return best;
}

bool sequel_summary_has_outer_from(const sequel_query_document_t *query) {
  const sequel_lexed_document_t *lexed = sequel_syntax_lexed(sequel_query_syntax(query));
  size_t count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &count);
  sequel_source_view_t source = sequel_query_source(query);
  size_t depth = 0;
  for (size_t i = 0; i < count; ++i) {
    if (tokens[i].kind == SEQUEL_TOKEN_PUNCTUATION && tokens[i].range.end == tokens[i].range.start + 1) {
      char punctuation = source.data[tokens[i].range.start];
      if (punctuation == '(')
        ++depth;
      else if (punctuation == ')' && depth)
        --depth;
    } else if (!depth && tokens[i].kind == SEQUEL_TOKEN_KEYWORD && tokens[i].keyword_id == SEQUEL_KEYWORD_FROM) {
      return true;
    }
  }
  return false;
}

int sequel_summary_append_relation_name(char **text, size_t *length, size_t *capacity, sequel_source_view_t source,
                                        const sequel_relation_info_t *relation) {
  size_t start = relation->name_range.start;
  size_t end = relation->name_range.end;
  if (start > end || end > source.length ||
      !sequel_summary_append_bytes(text, length, capacity, source.data + start, end - start))
    return 0;
  if (relation->kind == SEQUEL_RELATION_SUBQUERY && start < end && source.data[start] == '(') {
    char *rendered = *text;
    char *open = rendered + *length - (end - start);
    char *select = sequel_summary_find_text(open, "SELECT ");
    char *from = select ? sequel_summary_find_text(select, " FROM ") : NULL;
    char *close = strrchr(open, ')');
    if (select && from && close && close < rendered + *length) {
      size_t offset = (size_t)(open - rendered);
      *length = offset;
      rendered[offset] = 0;
      if (!sequel_summary_append_text(text, length, capacity, "(SELECT … FROM "))
        return 0;
      const char *table = from + 6;
      while (table < close && isspace((unsigned char)*table))
        ++table;
      const char *table_end = table;
      while (table_end < close && !isspace((unsigned char)*table_end) && *table_end != ')')
        ++table_end;
      if (!sequel_summary_append_bytes(text, length, capacity, table, (size_t)(table_end - table)))
        return 0;
      const char *where = sequel_summary_find_text(table_end, " WHERE ");
      const char *order = sequel_summary_find_text(table_end, " ORDER BY ");
      const char *limit = sequel_summary_find_text(table_end, " LIMIT ");
      const char *inner_alias = sequel_summary_find_text(table_end, " AS ");
      if (inner_alias && inner_alias < close && (!where || inner_alias < where) && (!order || inner_alias < order)) {
        const char *alias = inner_alias + strlen(" AS ");
        const char *alias_end = alias;
        while (alias_end < close && !isspace((unsigned char)*alias_end) && *alias_end != ')')
          ++alias_end;
        if (!sequel_summary_append_text(text, length, capacity, " AS ") ||
            !sequel_summary_append_bytes(text, length, capacity, alias, (size_t)(alias_end - alias)))
          return 0;
      }
      const char *join = sequel_summary_next_join_marker(table_end, close);
      while (join && join < close && (!where || join < where) && (!order || join < order) && (!limit || join < limit)) {
        const char *join_keyword = join;
        while (join_keyword < close && isspace((unsigned char)*join_keyword))
          ++join_keyword;
        const char *join_table = join_keyword;
        while (join_table < close && !isspace((unsigned char)*join_table))
          ++join_table;
        while (join_table < close && isspace((unsigned char)*join_table))
          ++join_table;
        if (join_keyword != join && join_keyword < join_table) {
          const char *join_word = sequel_summary_find_text(join_keyword, " JOIN ");
          if (!join_word || join_word >= close)
            break;
          join_table = join_word + strlen(" JOIN ");
        }
        while (join_table < close && isspace((unsigned char)*join_table))
          ++join_table;
        const char *join_end = join_table;
        while (join_end < close && !isspace((unsigned char)*join_end) && *join_end != ')')
          ++join_end;
        const char *on = sequel_summary_find_text(join_end, " ON ");
        if (!on || on >= close || (where && on >= where))
          break;
        if (!sequel_summary_append_bytes(text, length, capacity, join, (size_t)(join_table - join)) ||
            !sequel_summary_append_bytes(text, length, capacity, join_table, (size_t)(join_end - join_table)) ||
            !sequel_summary_append_text(text, length, capacity, " ON …"))
          return 0;
        join = sequel_summary_next_join_marker(on + strlen(" ON "), close);
      }
      if (where && where < close && (!sequel_summary_append_text(text, length, capacity, " WHERE …")))
        return 0;
      if (order && order < close && (!sequel_summary_append_text(text, length, capacity, " ORDER BY …")))
        return 0;
      if (limit && limit < close && (!sequel_summary_append_text(text, length, capacity, " LIMIT ?")))
        return 0;
      if (!sequel_summary_append_text(text, length, capacity, ")"))
        return 0;
    }
  }
  if (end < source.length && source.data[end] == '(') {
    const char *close = strchr(source.data + end, ')');
    if (close && (!sequel_summary_append_text(text, length, capacity, "(…)")))
      return 0;
  }
  if (relation->kind == SEQUEL_RELATION_FUNCTION && relation->alias_range.start > end) {
    const char *suffix = source.data + end;
    size_t suffix_length = relation->alias_range.start - end;
    if (sequel_summary_find_word(suffix, suffix_length, 0, "WITH") &&
        sequel_summary_find_word(suffix, suffix_length, 0, "ORDINALITY") &&
        !sequel_summary_append_text(text, length, capacity, " WITH ORDINALITY"))
      return 0;
  }
  if (relation->alias_range.end > relation->alias_range.start) {
    size_t alias_start = relation->alias_range.start;
    size_t alias_end = relation->alias_range.end;
    if (alias_end > source.length || alias_start > alias_end)
      return 0;
    if (sequel_summary_word(source.data, source.length, alias_start, "SAMPLE"))
      return sequel_summary_append_text(text, length, capacity, " SAMPLE");
    if (!sequel_summary_append_text(text, length, capacity, " AS ") ||
        !sequel_summary_append_bytes(text, length, capacity, source.data + alias_start, alias_end - alias_start))
      return 0;
    if (relation->kind == SEQUEL_RELATION_FUNCTION && relation->range.end > alias_end &&
        source.data[alias_end] == '(') {
      const char *column_end = sequel_summary_matching_close(source.data + alias_end, source.data + source.length);
      if (!column_end || !sequel_summary_append_bytes(text, length, capacity, source.data + alias_end,
                                                      (size_t)(column_end - (source.data + alias_end)) + 1))
        return 0;
    }
  }
  return 1;
}
