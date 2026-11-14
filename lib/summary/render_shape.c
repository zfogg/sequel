#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "../tokens.h"

int sequel_summary_render_shape_phases(sequel_summary_render_state_t *state, const sequel_query_document_t *query, const sequel_summary_options_t *settings, sequel_source_view_t source,
                                       size_t statement_count, const sequel_statement_info_t *statements, sequel_source_range_t outer_range, size_t cte_prefix_end, size_t outer_start,
                                       bool nested_cte_scope, bool insert_values_scope, bool cte_insert_scope, bool scalar_with_scope) {
  if (settings->include_operation && statement_count && !sequel_query_cte_count(query) && !nested_cte_scope && !cte_insert_scope &&
      !sequel_summary_append_statement_shape(&state->text, &state->length, &state->capacity, source, query, settings, &statements[0]))
    return -1;
  if (settings->include_operation && statement_count && !sequel_query_cte_count(query) &&
      !sequel_summary_append_projection_shape(&state->text, &state->length, &state->capacity, source, query, &statements[0]))
    return -1;
  /* A SELECT projection can span the FROM token when the syntax tree was
   * recovered from incomplete input.  Keep the summary shape independent of
   * that recovery artifact; relation rendering owns the FROM/JOIN clauses. */
  if (settings->include_tables && statement_count && !sequel_query_cte_count(query) && statements[0].operation == SEQUEL_OPERATION_SELECT && state->text &&
      sequel_summary_find_text(state->text, "FROM ")) {
    state->length = 0;
    state->text[0] = '\0';
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, sequel_summary_find_text(source.data, "SELECT DISTINCT ON") ? "SELECT DISTINCT ON (…) …" : "SELECT …"))
      return -1;
  }
  if (settings->include_tables && statement_count && statements[0].operation == SEQUEL_OPERATION_SELECT && sequel_query_relation_count(query) == 0 &&
      (sequel_summary_has_clause(query, SEQUEL_KEYWORD_FROM) || sequel_summary_has_token(query, SEQUEL_KEYWORD_FROM))) {
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))
      return -1;
  }
  if (settings->include_tables && statement_count && statements[0].operation == SEQUEL_OPERATION_INSERT) {
    size_t into = SIZE_MAX;
    for (size_t i = 0; i < source.length; ++i)
      if (sequel_summary_word(source.data, source.length, i, "INTO")) {
        into = i;
        break;
      }
    if (into != SIZE_MAX) {
      size_t name = into + 4;
      while (name < source.length && isspace((unsigned char)source.data[name]))
        ++name;
      size_t end = name;
      while (end < source.length && !isspace((unsigned char)source.data[end]) && source.data[end] != ';' && source.data[end] != '(')
        ++end;
      if (end > name && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " INTO ") ||
                         !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, source.data + name, end - name)))
        return -1;
    }
    for (size_t i = 0; i < source.length; ++i)
      if (sequel_summary_word(source.data, source.length, i, "VALUES")) {
        insert_values_scope = true;
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " VALUES ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))
          return -1;
        break;
      }
    for (size_t i = 0; i < source.length; ++i)
      if (sequel_summary_word(source.data, source.length, i, "SELECT")) {
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " SELECT ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))
          return -1;
        break;
      }
  }
  if (settings->include_tables && statement_count && statements[0].operation == SEQUEL_OPERATION_CREATE) {
    size_t index = SIZE_MAX;
    for (size_t i = 0; i < source.length; ++i)
      if (sequel_summary_word(source.data, source.length, i, "INDEX")) {
        index = i;
        break;
      }
    if (index != SIZE_MAX) {
      size_t name = index + 5;
      while (name < source.length && isspace((unsigned char)source.data[name]))
        ++name;
      for (const char *modifier = "CONCURRENTLY"; modifier;) {
        size_t n = strlen(modifier);
        if (!sequel_summary_word(source.data, source.length, name, modifier))
          break;
        name += n;
        while (name < source.length && isspace((unsigned char)source.data[name]))
          ++name;
        modifier = NULL;
      }
      if (sequel_summary_word(source.data, source.length, name, "IF")) {
        size_t cursor = name + 2;
        while (cursor < source.length && isspace((unsigned char)source.data[cursor]))
          ++cursor;
        if (sequel_summary_word(source.data, source.length, cursor, "NOT")) {
          cursor += 3;
          while (cursor < source.length && isspace((unsigned char)source.data[cursor]))
            ++cursor;
          if (sequel_summary_word(source.data, source.length, cursor, "EXISTS")) {
            name = cursor + 6;
            while (name < source.length && isspace((unsigned char)source.data[name]))
              ++name;
          }
        }
      }
      size_t end = name;
      sequel_dialect_t dialect = sequel_lexed_dialect(sequel_syntax_lexed(sequel_query_syntax(query)));
      if (name < source.length && source.data[name] != '\'' && !(dialect == SEQUEL_MYSQL && (source.data[name] == '"' || source.data[name] == '`')) &&
          !sequel_summary_word(source.data, source.length, name, "ON")) {
        if (source.data[name] == '"' || source.data[name] == '`') {
          size_t quoted_name = name;
          char quote = source.data[name++];
          end = name;
          while (end < source.length && source.data[end] != quote)
            ++end;
          if (end < source.length)
            ++end;
          name = quoted_name;
        } else {
          while (end < source.length && !isspace((unsigned char)source.data[end]) && source.data[end] != ';')
            ++end;
        }
        size_t unique = index;
        while (unique && isspace((unsigned char)source.data[unique - 1]))
          --unique;
        while (unique && (isalnum((unsigned char)source.data[unique - 1]) || source.data[unique - 1] == '_'))
          --unique;
        const char *index_label = sequel_summary_word(source.data, source.length, unique, "UNIQUE") ? " UNIQUE INDEX " : " INDEX ";
        if (end > name && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, index_label))
          return -1;
        if (end > name && !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, source.data + name, end - name))
          return -1;
        if (end > name) {
          size_t on = end;
          while (on < source.length && isspace((unsigned char)source.data[on]))
            ++on;
          if (sequel_summary_word(source.data, source.length, on, "ON") && settings->redact_relations && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON ..."))
            return -1;
        }
      }
    }
  }
  if (settings->include_tables && statement_count && !sequel_query_cte_count(query) && statements[0].operation == SEQUEL_OPERATION_SELECT && sequel_summary_find_text(source.data, " OVER (") &&
      sequel_summary_find_text(source.data, " FILTER (") && sequel_summary_find_text(source.data, " FROM ")) {
    const char *from = sequel_summary_find_text(source.data, " FROM ") + 6;
    while (*from && isspace((unsigned char)*from))
      ++from;
    const char *end = from;
    while (end < source.data + source.length && !isspace((unsigned char)*end))
      ++end;
    state->length = 0;
    if (state->text)
      state->text[0] = '\0';
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "SELECT ") ||
        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
        !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " OVER (…)") || !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FILTER (…)") ||
        !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, from, (size_t)(end - from)))
      return -1;
  }
  if (settings->include_tables && statement_count && !sequel_query_cte_count(query) && statements[0].operation == SEQUEL_OPERATION_SELECT &&
      (sequel_summary_find_word(source.data, source.length, 0, "UNION") || sequel_summary_find_word(source.data, source.length, 0, "EXCEPT") ||
       sequel_summary_find_word(source.data, source.length, 0, "INTERSECT"))) {
    state->length = 0;
    if (state->text)
      state->text[0] = '\0';
    if (source.length && source.data[0] == '(') {
      const char *cursor = source.data;
      bool first_set = true;
      while (cursor < source.data + source.length) {
        const char *select = sequel_summary_find_text(cursor, "(SELECT ");
        if (!select)
          break;
        const char *from = sequel_summary_find_text(select, " FROM ");
        const char *close = from ? strchr(from, ')') : NULL;
        if (!from || !close)
          break;
        const char *table = from + 6;
        while (table < close && isspace((unsigned char)*table))
          ++table;
        const char *table_end = table;
        while (table_end < close && !isspace((unsigned char)*table_end) && *table_end != ')')
          ++table_end;
        const char *limit = sequel_summary_find_text(table_end, " LIMIT ");
        if (limit && limit > close)
          limit = NULL;
        if (!first_set) {
          const char *operator_start = cursor;
          while (operator_start > source.data && operator_start[-1] != ')')
            --operator_start;
          if (!sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, operator_start, (size_t)(select - operator_start)))
            return -1;
        }
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "(SELECT ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(table_end - table)))
          return -1;
        if (limit) {
          const char *value = limit + 7;
          while (*value && isspace((unsigned char)*value))
            ++value;
          const char *value_end = value;
          while (value_end < close && !isspace((unsigned char)*value_end) && *value_end != ')')
            ++value_end;
          if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " LIMIT ") ||
              !sequel_summary_append_text(&state->text, &state->length, &state->capacity, value[0] == '$' || value[0] == '?' ? "?" : "1"))
            return -1;
        }
        const char *inner_set = sequel_summary_find_text(table_end, " EXCEPT ");
        const char *inner_union = sequel_summary_find_text(table_end, " UNION ");
        if (!inner_set || (inner_union && inner_union < inner_set))
          inner_set = inner_union;
        if (inner_set && inner_set < close) {
          const char *inner_select = sequel_summary_find_text(inner_set, " SELECT ");
          const char *inner_from = inner_select ? sequel_summary_find_text(inner_select, " FROM ") : NULL;
          if (inner_select && inner_from && inner_from < close) {
            const char *inner_table = inner_from + 6;
            const char *inner_table_end = inner_table;
            while (inner_table_end < close && !isspace((unsigned char)*inner_table_end) && *inner_table_end != ')')
              ++inner_table_end;
            if (!sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, inner_set, (size_t)(inner_select - inner_set)) ||
                !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " SELECT ") ||
                !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
                !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
                !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, inner_table, (size_t)(inner_table_end - inner_table)))
              return -1;
          }
        }
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, ")"))
          return -1;
        cursor = close + 1;
        first_set = false;
        if (!sequel_summary_find_text(cursor, "SELECT "))
          break;
      }
      const char *order = sequel_summary_find_text(cursor, " ORDER BY ");
      if (order && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ORDER BY …") ||
                    (sequel_summary_find_text(order, " LIMIT ") && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " LIMIT ") ||
                                                                    !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_find_text(order, " LIMIT ") + 7,
                                                                                                 source.data + source.length - (sequel_summary_find_text(order, " LIMIT ") + 7))))))
        return -1;
      return 1;
    } else {
      const char *cursor = source.data;
      bool first_set = true;
      if (sequel_summary_find_text(source.data, "JOIN LATERAL") && sequel_summary_find_text(source.data, " UNION ALL ") && sequel_query_relation_count(query) >= 5 &&
          sequel_query_join_count(query) >= 2) {
        const sequel_relation_info_t *model_relations = sequel_query_relations(query);
        const sequel_join_info_t *model_joins = sequel_query_joins(query);
        const char *lateral = sequel_summary_find_text(source.data, "LATERAL");
        const char *open = lateral ? strchr(lateral, '(') : NULL;
        const char *close = open ? sequel_summary_matching_close(open, source.data + source.length) : NULL;
        const char *inner_from = open && close ? sequel_summary_find_word(open, (size_t)(close - open), 0, "FROM") : NULL;
        const char *union_all = sequel_summary_find_text(source.data, " UNION ALL ");
        const char *using = union_all ? sequel_summary_find_word(union_all, source.length - (size_t)(union_all - source.data), 0, "USING") : NULL;
        const char *using_open = using ? strchr(using, '(') : NULL;
        const char *using_close = using_open ? strchr(using_open, ')') : NULL;
        const char *alias = close ? sequel_summary_find_word(close, (size_t)(source.data + source.length - close), 0, "AS") : NULL;
        const char *alias_name = alias ? alias + 2 : NULL;
        if (alias_name)
          alias_name += sequel_summary_skip_space(alias_name, source.data + source.length - alias_name, 0);
        const char *alias_end = alias_name;
        while (alias_end && alias_end < source.data + source.length && (isalnum((unsigned char)*alias_end) || *alias_end == '_'))
          ++alias_end;
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "SELECT ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
            !sequel_summary_append_relation_name(&state->text, &state->length, &state->capacity, source, &model_relations[0]) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " JOIN ") || !sequel_summary_append_text(&state->text, &state->length, &state->capacity, "LATERAL (SELECT ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") || !inner_from || !close ||
            !sequel_summary_append_relation_name(&state->text, &state->length, &state->capacity, source, &model_relations[2]) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " LIMIT ?)") || !alias_name || !alias_end ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, alias_name, (size_t)(alias_end - alias_name)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON ") ||
            !sequel_summary_append_redacted_range(&state->text, &state->length, &state->capacity, query, model_joins[0].condition_range.start, model_joins[0].condition_range.end) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " UNION ALL SELECT ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
            !sequel_summary_append_relation_name(&state->text, &state->length, &state->capacity, source, &model_relations[3]) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " JOIN ") ||
            !sequel_summary_append_relation_name(&state->text, &state->length, &state->capacity, source, &model_relations[4]) || !using_open || !using_close ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " USING (") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, using_open + 1, (size_t)(using_close - using_open - 1)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, ")"))
          return -1;
        return 1;
      }
      while (cursor < source.data + source.length) {
        const char *select = sequel_summary_find_word(source.data, source.length, (size_t)(cursor - source.data), "SELECT");
        if (!select)
          break;
        const char *from = sequel_summary_find_word(source.data, source.length, (size_t)(select - source.data), "FROM");
        if (!from)
          break;
        const char *table = from + 5;
        while (*table && isspace((unsigned char)*table))
          ++table;
        const char *end = table;
        while (end < source.data + source.length && !isspace((unsigned char)*end))
          ++end;
        if (!first_set && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " "))
          return -1;
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, first_set ? "SELECT " : "SELECT ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(end - table)))
          return -1;
        const char *op = sequel_summary_find_word(source.data, source.length, (size_t)(end - source.data), "UNION");
        const char *except = sequel_summary_find_word(source.data, source.length, (size_t)(end - source.data), "EXCEPT");
        const char *intersect = sequel_summary_find_word(source.data, source.length, (size_t)(end - source.data), "INTERSECT");
        const char *next = op;
        if (!next || (except && except < next))
          next = except;
        if (!next || (intersect && intersect < next))
          next = intersect;
        if (!next) {
          const char *where = sequel_summary_find_word(source.data, source.length, (size_t)(end - source.data), "WHERE");
          if (where && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
                        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length())))
            return -1;
          const char *limit = sequel_summary_find_word(source.data, source.length, (size_t)(end - source.data), "LIMIT");
          if (limit) {
            const char *value = limit + 5;
            while (*value && isspace((unsigned char)*value))
              ++value;
            if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " LIMIT ") ||
                !sequel_summary_append_text(&state->text, &state->length, &state->capacity, *value == '$' || *value == '?' ? "?" : "1"))
              return -1;
          }
          break;
        }
        const char *where = sequel_summary_find_word(source.data, source.length, (size_t)(end - source.data), "WHERE");
        if (where && where < next &&
            (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
             !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length())))
          return -1;
        const char *following_select = sequel_summary_find_word(source.data, source.length, (size_t)(next - source.data + 1), "SELECT");
        if (!following_select)
          following_select = sequel_summary_find_word(source.data, source.length, (size_t)(next - source.data + 1), "SELECT");
        if (!following_select || !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, next, (size_t)(following_select - next)))
          return -1;
        cursor = following_select + (following_select[0] == '(' ? 1 : 0);
        first_set = false;
      }
      return 1;
    }
  }
  if (settings->include_tables && statement_count && statements[0].operation == SEQUEL_OPERATION_UPDATE && sequel_summary_find_text(source.data, " SET ") &&
      sequel_summary_find_text(source.data, " FROM ")) {
    const char *table = source.data + strlen("UPDATE ");
    const char *set = sequel_summary_find_text(table, " SET ");
    const char *subquery_close = set ? strchr(set, ')') : NULL;
    const char *subquery = set ? sequel_summary_find_text(set, "(SELECT ") : NULL;
    const char *subquery_from = subquery ? sequel_summary_find_text(subquery, " FROM ") : NULL;
    const char *subquery_end = subquery_from ? sequel_summary_find_text(subquery_from + 6, ") FROM ") : NULL;
    const char *from = NULL;
    size_t clause_count = sequel_query_clause_count(query);
    const sequel_clause_info_t *clauses = sequel_query_clauses(query);
    for (size_t i = 0; i < clause_count; ++i)
      if (clauses[i].statement_index == 0 && clauses[i].keyword_id == SEQUEL_KEYWORD_FROM && clauses[i].range.start > (size_t)(set - source.data)) {
        from = source.data + clauses[i].range.start;
        break;
      }
    if (!from)
      from = set ? sequel_summary_find_text(subquery_end ? subquery_end + 1 : (subquery_close ? subquery_close + 1 : set + 5), " FROM ") : NULL;
    const char *where = from ? sequel_summary_find_text(from + 4, " WHERE ") : NULL;
    if (set && from) {
      const char *table_end = set;
      const char *from_table = from + 4;
      while (*from_table && isspace((unsigned char)*from_table))
        ++from_table;
      const char *from_end = where ? where : source.data + source.length;
      while (from_end > from_table && isspace((unsigned char)from_end[-1]))
        --from_end;
      state->length = 0;
      if (state->text)
        state->text[0] = '\0';
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "UPDATE ") ||
          !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(table_end - table)) ||
          !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " SET ") ||
          !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
          (subquery && subquery_from && subquery_end ? (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " (SELECT ") ||
                                                        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
                                                        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, subquery_from, (size_t)(subquery_end - subquery_from)) ||
                                                        !sequel_summary_append_text(&state->text, &state->length, &state->capacity, ")"))
                                                     : !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, set + 5, (size_t)(from - (set + 5)))) ||
          !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
          !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, from_table, (size_t)(from_end - from_table)) ||
          (where && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
                     !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))))
        return -1;
    }
  }
  if (settings->include_tables && !sequel_query_cte_count(query) && !nested_cte_scope && !insert_values_scope && !cte_insert_scope && !sequel_summary_find_text(source.data, " UNION ") &&
      !sequel_summary_find_text(source.data, " EXCEPT ") && !sequel_summary_find_text(source.data, " INTERSECT ") &&
      !(sequel_summary_find_text(source.data, " OVER (") && sequel_summary_find_text(source.data, " FILTER (")) &&
      !(statements[0].operation == SEQUEL_OPERATION_UPDATE && sequel_summary_find_text(source.data, " SET ") && sequel_summary_find_text(source.data, " FROM "))) {
    size_t relation_count = sequel_query_relation_count(query);
    const sequel_relation_info_t *relations = sequel_query_relations(query);
    sequel_operation_t operation = statement_count ? statements[0].operation : SEQUEL_OPERATION_UNKNOWN;
    const sequel_join_info_t *joins = sequel_query_joins(query);
    size_t rendered_relations = 0;
    bool grouped_relation = false;
    const char *group_from = NULL;
    for (const char *candidate = sequel_summary_find_text(source.data, "FROM ("); candidate; candidate = sequel_summary_find_text(candidate + 6, "FROM ("))
      group_from = candidate;
    if (!group_from)
      group_from = sequel_summary_find_text(source.data, "FROM (WITH");
    if (group_from && !sequel_summary_find_text(group_from, "CROSS JOIN LATERAL") && !sequel_summary_find_text(group_from, "JOIN LATERAL") &&
        (operation == SEQUEL_OPERATION_SELECT || operation == SEQUEL_OPERATION_UNKNOWN)) {
      const char *inner = group_from + 6;
      const char *close = strrchr(inner, ')');
      const char *join = sequel_summary_find_text(inner, ") q JOIN ");
      if (!join)
        join = sequel_summary_find_text(inner, " JOIN ");
      if (close && join && join < close) {
        const char *first = inner;
        if (sequel_summary_word(inner, (size_t)(close - inner), 0, "WITH")) {
          const char *outer_select = sequel_summary_find_text(inner, ") SELECT ");
          if (outer_select)
            first = outer_select + 2;
        }
        while (*first && isspace((unsigned char)*first))
          ++first;
        const char *first_end = first;
        while (first_end < join && !isspace((unsigned char)*first_end))
          ++first_end;
        const char *first_alias = first_end;
        while (first_alias < join && isspace((unsigned char)*first_alias))
          ++first_alias;
        const char *first_alias_end = first_alias;
        while (first_alias_end < join && !isspace((unsigned char)*first_alias_end))
          ++first_alias_end;
        const char *second = join + 6;
        const char *second_end = second;
        while (second_end < close && !isspace((unsigned char)*second_end))
          ++second_end;
        const char *second_alias = second_end;
        while (second_alias < close && isspace((unsigned char)*second_alias))
          ++second_alias;
        const char *second_alias_end = second_alias;
        while (second_alias_end < close && !isspace((unsigned char)*second_alias_end))
          ++second_alias_end;
        const char *condition = sequel_summary_find_text(second_alias_end, " ON true");
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM (") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, first, first_end - first) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, first_alias, first_alias_end - first_alias) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " JOIN ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, second, second_end - second) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, second_alias, second_alias_end - second_alias) ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, second_alias_end,
                                         condition && condition < close ? (size_t)(condition - second_alias_end) : close - second_alias_end) ||
            (condition && condition < close &&
             (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON ") || !sequel_summary_append_text(&state->text, &state->length, &state->capacity, "?"))) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, ")"))
          return -1;
        grouped_relation = true;
      }
    }
    size_t relation_passes = sequel_query_join_count(query) && sequel_summary_find_text(source.data, "JOIN") &&
                                     !(sequel_query_cte_count(query) && statement_count &&
                                       (statements[0].operation == SEQUEL_OPERATION_INSERT || statements[0].operation == SEQUEL_OPERATION_UPDATE || statements[0].operation == SEQUEL_OPERATION_DELETE))
                                 ? 2
                                 : 1;
    for (size_t relation_pass = 0; relation_pass < relation_passes; ++relation_pass)
      for (size_t i = 0; i < relation_count; ++i) {
        if (sequel_query_cte_count(query) && statements[0].operation == SEQUEL_OPERATION_SELECT && !sequel_summary_find_text(source.data, " LATERAL "))
          break;
        if (grouped_relation)
          break;
        if (operation == SEQUEL_OPERATION_CREATE) {
          if (!state->text || !sequel_summary_find_text(state->text, " INDEX "))
            break;
          if (state->text && sequel_summary_find_text(state->text, " INDEX ")) {
            if (sequel_summary_find_text(state->text, " ON ..."))
              break;
            if (!sequel_summary_find_text(state->text, " ON ...") && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON "))
              return -1;
            if (settings->redact_relations) {
              if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "..."))
                return -1;
              break;
            }
            if (!sequel_summary_append_relation_name(&state->text, &state->length, &state->capacity, source, &relations[i]))
              return -1;
            break;
          }
        }
        bool nested = false;
        size_t join_index = SIZE_MAX;
        for (size_t j = 0; j < sequel_query_join_count(query); ++j)
          if (joins[j].right_relation == i) {
            join_index = j;
            break;
          }
        if (join_index == SIZE_MAX && relations[i].name_range.end - relations[i].name_range.start == 7 && !memcmp(source.data + relations[i].name_range.start, "LATERAL", 7)) {
          for (size_t j = 0; j < sequel_query_join_count(query); ++j) {
            if (sequel_source_range_contains(joins[j].range, relations[i].range)) {
              join_index = j;
              break;
            }
          }
        }
        if (!sequel_summary_find_text(source.data, "JOIN"))
          join_index = SIZE_MAX;
        if (relation_passes == 2 && (relation_pass == 0) == (join_index != SIZE_MAX))
          continue;
        for (size_t p = 0; (relations[i].flags & SEQUEL_RELATION_FLAG_NESTED_SCOPE) && p < sequel_query_predicate_count(query); ++p) {
          const sequel_predicate_info_t *predicate = &sequel_query_predicates(query)[p];
          if (sequel_source_range_contains(predicate->range, relations[i].range)) {
            nested = true;
            break;
          }
        }
        if (!nested && (relations[i].flags & SEQUEL_RELATION_FLAG_NESTED_SCOPE))
          for (size_t j = 0; j < sequel_query_join_count(query); ++j)
            if (joins[j].has_condition && joins[j].right_relation != i && sequel_source_range_contains(joins[j].condition_range, relations[i].range)) {
              nested = true;
              break;
            }
        bool relation_is_lateral = relations[i].name_range.end - relations[i].name_range.start == 7 && !memcmp(source.data + relations[i].name_range.start, "LATERAL", 7);
        if (nested && i != 0 && !relation_is_lateral) {
          continue;
        }
        if ((relations[i].flags & SEQUEL_RELATION_FLAG_PROJECTION_SCOPE) && join_index == SIZE_MAX) {
          continue;
        }
        bool nested_relation = (relations[i].flags & SEQUEL_RELATION_FLAG_NESTED_SCOPE) != 0;
        if (!relation_is_lateral && relations[i].parent_relation != SIZE_MAX && relations[relations[i].parent_relation].kind == SEQUEL_RELATION_SUBQUERY &&
            sequel_source_range_contains(relations[relations[i].parent_relation].range, relations[i].range)) {
          /* Nested relations are rendered as part of their parent subquery.
           * Emitting them here would duplicate inner JOIN/ON clauses. */
          continue;
        }
        bool lateral_relation_name = relations[i].name_range.end - relations[i].name_range.start == 7 && !memcmp(source.data + relations[i].name_range.start, "LATERAL", 7);
        bool parenthesized_table_input = relations[i].name_range.start < source.length && source.data[relations[i].name_range.start] == '(' &&
                                         sequel_summary_find_text(source.data + relations[i].name_range.start, "SELECT") &&
                                         sequel_summary_find_text(source.data + relations[i].name_range.start, "unnest(");
        if (nested_relation && !lateral_relation_name && !parenthesized_table_input && join_index == SIZE_MAX) {
          continue;
        }
        if (operation != SEQUEL_OPERATION_CREATE) {
          const char *prefix = rendered_relations == 0 ? " FROM " : ",";
          if (rendered_relations && join_index == SIZE_MAX)
            prefix = ", ";
          if (rendered_relations && join_index != SIZE_MAX) {
            prefix = " JOIN ";
            if (joins[join_index].flags & SEQUEL_JOIN_FLAG_SEMI)
              prefix = sequel_query_dialect(query) == SEQUEL_CLICKHOUSE ? " LEFT SEMI JOIN " : " SEMI JOIN ";
            else if (joins[join_index].flags & SEQUEL_JOIN_FLAG_ANTI)
              prefix = sequel_query_dialect(query) == SEQUEL_CLICKHOUSE ? " LEFT ANTI JOIN " : " ANTI JOIN ";
            if (!(joins[join_index].flags & (SEQUEL_JOIN_FLAG_SEMI | SEQUEL_JOIN_FLAG_ANTI))) {
              size_t context_start = i ? relations[i - 1].range.end : 0;
              for (size_t cursor = context_start; cursor < relations[i].range.start; ++cursor) {
                if (sequel_summary_word(source.data, source.length, cursor, "ASOF") && !sequel_summary_cursor_is_quoted_identifier(query, cursor)) {
                  prefix = " ASOF LEFT JOIN ";
                  break;
                }
                if (sequel_summary_word(source.data, source.length, cursor, "CROSS") && !sequel_summary_cursor_is_quoted_identifier(query, cursor)) {
                  prefix = cursor + 18 <= source.length && !memcmp(source.data + cursor, "CROSS JOIN LATERAL", 18) ? " CROSS JOIN LATERAL " : " CROSS JOIN ";
                  break;
                }
                if (sequel_summary_word(source.data, source.length, cursor, "LEFT") && !sequel_summary_cursor_is_quoted_identifier(query, cursor)) {
                  prefix = cursor + 10 <= source.length && !memcmp(source.data + cursor, "LEFT OUTER", 10) ? " LEFT OUTER JOIN " : " LEFT JOIN ";
                  break;
                }
                if (sequel_summary_word(source.data, source.length, cursor, "RIGHT") && !sequel_summary_cursor_is_quoted_identifier(query, cursor)) {
                  prefix = " RIGHT JOIN ";
                  break;
                }
                if (sequel_summary_word(source.data, source.length, cursor, "FULL") && !sequel_summary_cursor_is_quoted_identifier(query, cursor)) {
                  prefix = cursor + 10 <= source.length && !memcmp(source.data + cursor, "FULL OUTER", 10) ? " FULL OUTER JOIN " : " FULL JOIN ";
                  break;
                }
                if (sequel_summary_word(source.data, source.length, cursor, "INNER") && !sequel_summary_cursor_is_quoted_identifier(query, cursor)) {
                  prefix = " INNER JOIN ";
                  break;
                }
                if (sequel_summary_word(source.data, source.length, cursor, "NATURAL") && !sequel_summary_cursor_is_quoted_identifier(query, cursor)) {
                  prefix = cursor + 12 <= source.length && !memcmp(source.data + cursor, "NATURAL LEFT", 12) ? " NATURAL LEFT JOIN " : " NATURAL JOIN ";
                  break;
                }
              }
            }
          } else if (rendered_relations) {
            size_t context_start = i ? relations[i - 1].range.end : 0;
            const char *union_all = i ? sequel_summary_find_text(source.data + context_start, "UNION ALL") : NULL;
            if (union_all && (size_t)(union_all - source.data) < relations[i].range.start)
              prefix = " FROM ";
            const char *join_marker = sequel_summary_find_text(source.data + context_start, " JOIN ");
            if (join_marker && (size_t)(join_marker - source.data) < relations[i].range.start && !sequel_summary_cursor_is_quoted_identifier(query, (size_t)(join_marker - source.data)))
              prefix = " JOIN ";
          }
          if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, prefix))
            return -1;
        }
        bool lateral_relation = relations[i].name_range.end - relations[i].name_range.start == 7 && !memcmp(source.data + relations[i].name_range.start, "LATERAL", 7);
        bool parenthesized_relation = source.data[relations[i].name_range.start] == '(';
        const char *first_lateral_join = sequel_summary_find_text(source.data, "JOIN LATERAL");
        bool multiple_lateral_joins = first_lateral_join && sequel_summary_find_text(first_lateral_join + strlen("JOIN LATERAL"), "JOIN LATERAL");
        if (join_index != SIZE_MAX && lateral_relation && multiple_lateral_joins && relations[i].range.end <= source.length) {
          const char *open = strchr(source.data + relations[i].name_range.end, '(');
          bool prefix_includes_lateral = sequel_summary_find_text(source.data, "CROSS JOIN LATERAL") != NULL;
          size_t relation_start = open ? (size_t)(open - source.data) : relations[i].range.start;
          if ((!prefix_includes_lateral && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, "LATERAL ")) || !open ||
              !sequel_summary_append_redacted_range(&state->text, &state->length, &state->capacity, query, relation_start, relations[i].range.end))
            return -1;
          if (joins[join_index].has_condition && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON "))
            return -1;
          if (joins[join_index].has_condition &&
              !sequel_summary_append_redacted_range(&state->text, &state->length, &state->capacity, query, joins[join_index].condition_range.start, joins[join_index].condition_range.end))
            return -1;
          ++rendered_relations;
          continue;
        }
        if (join_index == SIZE_MAX && parenthesized_relation && sequel_summary_find_text(source.data + relations[i].name_range.start, "SELECT") &&
            sequel_summary_find_text(source.data + relations[i].name_range.start, "unnest(")) {
          if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "(SELECT unnest(…), unnest(…))"))
            return -1;
          if (relations[i].alias_range.end > relations[i].alias_range.start &&
              (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ") ||
               !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, source.data + relations[i].alias_range.start,
                                            relations[i].alias_range.end - relations[i].alias_range.start)))
            return -1;
          ++rendered_relations;
          continue;
        }
        if (join_index != SIZE_MAX && relations[i].name_range.start < source.length && (lateral_relation || parenthesized_relation)) {
          if (lateral_relation && sequel_summary_find_text(source.data, "CROSS JOIN LATERAL") && sequel_summary_find_text(source.data, "unnest(")) {
            const char *open = source.data + relations[i].name_range.end;
            while (open < source.data + source.length && *open != '(')
              ++open;
            const char *close = open < source.data + source.length ? sequel_summary_matching_close(open, source.data + source.length) : NULL;
            if (!close || !sequel_summary_append_redacted_range(&state->text, &state->length, &state->capacity, query, (size_t)(open - source.data), (size_t)(close + 1 - source.data)))
              return -1;
            if (relations[i].alias_range.end > relations[i].alias_range.start &&
                (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ") ||
                 !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, source.data + relations[i].alias_range.start,
                                              relations[i].alias_range.end - relations[i].alias_range.start)))
              return -1;
            ++rendered_relations;
            continue;
          }
          const char *subquery = sequel_summary_find_text(source.data + relations[i].name_range.start, "SELECT");
          const char *from = subquery ? sequel_summary_find_text(subquery, " FROM ") : NULL;
          const char *where = from ? sequel_summary_find_text(from, " WHERE ") : NULL;
          const char *order = from ? sequel_summary_find_text(from, " ORDER BY ") : NULL;
          const char *limit = where ? sequel_summary_find_text(where, " LIMIT ") : NULL;
          if (subquery) {
            const char *candidate = from;
            while (candidate) {
              const char *next = sequel_summary_find_text(candidate + 6, " FROM ");
              if (!next || (where && next >= where))
                break;
              from = candidate = next;
            }
            where = from ? sequel_summary_find_text(from, " WHERE ") : NULL;
            order = from ? sequel_summary_find_text(from, " ORDER BY ") : NULL;
            limit = where ? sequel_summary_find_text(where, " LIMIT ") : NULL;
          }
          if (!where)
            limit = from ? sequel_summary_find_text(from, " LIMIT ") : NULL;
          const char *close = limit ? strchr(limit, ')') : NULL;
          const char *open = subquery;
          while (open > source.data + relations[i].name_range.start && open[-1] != '(')
            --open;
          if (open > source.data + relations[i].name_range.start && open[-1] == '(')
            --open;
          if (open < source.data + source.length && *open == '(')
            close = sequel_summary_matching_close(open, source.data + source.length);
          /* Clause searches start at the inner FROM, so an outer clause must
           * never be mistaken for part of this relation. */
          if (close) {
            if (where && where >= close)
              where = NULL;
            if (order && order >= close)
              order = NULL;
            if (limit && limit >= close)
              limit = NULL;
          }
          if (subquery && from && !where && limit && close) {
            const char *table = from + 6;
            while (*table && isspace((unsigned char)*table))
              ++table;
            if (*table == '(') {
              const char *nested_from = sequel_summary_find_text(table, " FROM ");
              if (nested_from)
                table = nested_from + 6;
              while (*table && isspace((unsigned char)*table))
                ++table;
            }
            const char *table_end = table;
            while (*table_end && !isspace((unsigned char)*table_end))
              ++table_end;
            const char *inner_alias = table_end;
            while (*inner_alias && isspace((unsigned char)*inner_alias))
              ++inner_alias;
            if (!strncmp(inner_alias, "AS ", 3)) {
              inner_alias += 3;
              while (*inner_alias && isspace((unsigned char)*inner_alias))
                ++inner_alias;
            }
            const char *inner_alias_end = inner_alias;
            while (*inner_alias_end && !isspace((unsigned char)*inner_alias_end) && *inner_alias_end != ')')
              ++inner_alias_end;
            if (inner_alias >= limit || inner_alias_end > limit)
              inner_alias = inner_alias_end = table_end;
            const char *alias = close + 1;
            while (*alias && isspace((unsigned char)*alias))
              ++alias;
            const char *alias_end = alias;
            while (*alias_end && !isspace((unsigned char)*alias_end))
              ++alias_end;
            const char *outer_on = sequel_summary_find_text(close + 1, " ON ");
            const char *outer_limit_marker = sequel_summary_find_text(close + 1, " LIMIT ");
            const char *outer_on_end = outer_limit_marker ? outer_limit_marker : source.data + source.length;
            while (outer_on && outer_on_end > outer_on + 4 && isspace((unsigned char)outer_on_end[-1]))
              --outer_on_end;
            bool outer_on_true = outer_on && sequel_summary_word(outer_on + 4, (size_t)(outer_on_end - (outer_on + 4)), 0, "true");
            if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity,
                                            lateral_relation && !sequel_summary_find_text(source.data, "CROSS JOIN LATERAL") ? "LATERAL (SELECT " : "(SELECT ") ||
                !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
                !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
                !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(table_end - table)) ||
                (inner_alias_end > inner_alias && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ") ||
                                                   !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, inner_alias, (size_t)(inner_alias_end - inner_alias)))) ||
                !sequel_summary_append_redacted_range(&state->text, &state->length, &state->capacity, query, (size_t)(limit - source.data), (size_t)(close + 1 - source.data)) ||
                !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ") ||
                !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, alias, (size_t)(alias_end - alias)) ||
                !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON ") || !outer_on ||
                (outer_on_true ? !sequel_summary_append_text(&state->text, &state->length, &state->capacity, "?")
                               : !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, outer_on + 4, (size_t)(outer_on_end - (outer_on + 4)))) ||
                ((outer_limit_marker || sequel_summary_find_text(close + 1, "$")) && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " LIMIT ?")))
              return -1;
            ++rendered_relations;
            continue;
          }
          if (close) {
            if (where && where >= close)
              where = NULL;
            if (order && order >= close)
              order = NULL;
            if (limit && limit >= close)
              limit = NULL;
          }
          if (subquery && from && where && limit && close) {
            const char *table = from + 6;
            while (*table && isspace((unsigned char)*table))
              ++table;
            if (*table == '(') {
              const char *nested_from = sequel_summary_find_text(table, " FROM ");
              if (nested_from)
                table = nested_from + 6;
              while (*table && isspace((unsigned char)*table))
                ++table;
            }
            const char *table_end = table;
            while (*table_end && !isspace((unsigned char)*table_end))
              ++table_end;
            bool cross_lateral = sequel_summary_find_text(source.data, "CROSS JOIN LATERAL") != NULL;
            bool omit_cross_condition = cross_lateral && !sequel_summary_find_text(close + 1, " ON ");
            const char *outer_limit = sequel_summary_find_text(close + 1, " LIMIT ");
            const char *next_join = sequel_summary_find_text(close + 1, " JOIN ");
            const char *outer_on = sequel_summary_find_text(close + 1, " ON ");
            const char *outer_on_end = outer_limit ? outer_limit : source.data + source.length;
            while (outer_on && outer_on_end > outer_on + 4 && isspace((unsigned char)outer_on_end[-1]))
              --outer_on_end;
            bool outer_on_true = outer_on && sequel_summary_word(outer_on + 4, (size_t)(outer_on_end - (outer_on + 4)), 0, "true");
            const char *inner_alias = table_end;
            const char *inner_alias_end = table_end;
            while (*inner_alias && isspace((unsigned char)*inner_alias))
              ++inner_alias;
            if (!strncmp(inner_alias, "AS ", 3)) {
              inner_alias += 3;
              while (*inner_alias && isspace((unsigned char)*inner_alias))
                ++inner_alias;
            }
            inner_alias_end = inner_alias;
            while (*inner_alias_end && !isspace((unsigned char)*inner_alias_end) && *inner_alias_end != ')')
              ++inner_alias_end;
            if (inner_alias >= where || inner_alias_end > where)
              inner_alias = inner_alias_end = table_end;
            const char *alias = close + 1;
            while (*alias && isspace((unsigned char)*alias))
              ++alias;
            const char *alias_end = alias;
            while (*alias_end && !isspace((unsigned char)*alias_end))
              ++alias_end;
            if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity,
                                            lateral_relation && !sequel_summary_find_text(source.data, "CROSS JOIN LATERAL") ? "LATERAL (SELECT " : "(SELECT ") ||
                !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
                !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
                !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(table_end - table)) ||
                (inner_alias_end > inner_alias && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ") ||
                                                   !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, inner_alias, (size_t)(inner_alias_end - inner_alias)))) ||
                !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
                !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
                (cross_lateral && order && order < close &&
                 (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ORDER BY ") ||
                  !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))) ||
                (limit && limit < close && !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, limit, (size_t)(close + 1 - limit))) ||
                !sequel_summary_append_text(&state->text, &state->length, &state->capacity, (limit && limit < close) ? " AS " : ") AS ") ||
                !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, alias, (size_t)(alias_end - alias)) ||
                (omit_cross_condition ? 0
                                      : (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON ") || !outer_on ||
                                         (outer_on_true ? !sequel_summary_append_text(&state->text, &state->length, &state->capacity, "?")
                                                        : !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, outer_on + 4, (size_t)(outer_on_end - (outer_on + 4)))))) ||
                (outer_limit && (!next_join || outer_limit < next_join) && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " LIMIT ?")))
              return -1;
            ++rendered_relations;
            continue;
          }
        }
        int okay = sequel_summary_append_relation_name(&state->text, &state->length, &state->capacity, source, &relations[i]);
        if (okay && join_index != SIZE_MAX)
          okay = sequel_summary_append_join_condition(&state->text, &state->length, &state->capacity, query, &joins[join_index]);
        if (okay && join_index != SIZE_MAX && !joins[join_index].has_condition) {
          size_t condition_start = relations[i].alias_range.end > relations[i].range.end ? relations[i].alias_range.end : relations[i].range.end;
          while (condition_start < source.length && isspace((unsigned char)source.data[condition_start]))
            ++condition_start;
          if (condition_start < source.length && source.data[condition_start] == '(') {
            const char *close = sequel_summary_matching_close(source.data + condition_start, source.data + source.length);
            if (close)
              condition_start = (size_t)(close + 1 - source.data);
          }
          while (condition_start < source.length && isspace((unsigned char)source.data[condition_start]))
            ++condition_start;
          size_t condition_end = source.length;
          for (size_t cursor = condition_start; cursor + 5 <= source.length; ++cursor)
            if (sequel_summary_word(source.data, source.length, cursor, "WHERE")) {
              condition_end = cursor;
              break;
            }
          const char *join_markers[] = {" JOIN ", " LEFT JOIN ", " RIGHT JOIN ", " FULL JOIN ", " INNER JOIN ", " CROSS JOIN ", " NATURAL JOIN "};
          for (size_t marker = 0; marker < sizeof(join_markers) / sizeof(join_markers[0]); ++marker) {
            const char *join = sequel_summary_find_text(source.data + condition_start, join_markers[marker]);
            if (join) {
              size_t join_offset = (size_t)(join - source.data);
              if (join_offset < condition_end)
                condition_end = join_offset;
            }
          }
          const char *next_join = sequel_summary_find_word(source.data, source.length, condition_start, "JOIN");
          if (next_join && (size_t)(next_join - source.data) < condition_end)
            condition_end = (size_t)(next_join - source.data);
          while (condition_end > condition_start && isspace((unsigned char)source.data[condition_end - 1]))
            --condition_end;
          if (condition_end > condition_start && sequel_summary_word(source.data, source.length, condition_start, "ON")) {
            bool literal_true = sequel_summary_word(source.data, source.length, condition_start, "ON") &&
                                sequel_summary_word(source.data, source.length, sequel_summary_skip_space(source.data, source.length, condition_start + 2), "true");
            okay =
                sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ") &&
                (literal_true ? (sequel_summary_append_text(&state->text, &state->length, &state->capacity, "ON ") && sequel_summary_append_text(&state->text, &state->length, &state->capacity, "?"))
                              : sequel_summary_append_redacted_range(&state->text, &state->length, &state->capacity, query, condition_start, condition_end));
          }
        }
        if (okay && rendered_relations) {
          const char *using = sequel_summary_find_text(source.data + relations[i].range.end, "USING");
          if (using && i + 1 < relation_count && using >= source.data + relations[i + 1].range.start)
            using = NULL;
          if (using && (!sequel_summary_find_text(source.data + relations[i].range.end, "WHERE") || using < sequel_summary_find_text(source.data + relations[i].range.end, "WHERE"))) {
            const char *open = strchr(using, '(');
            const char *close = open ? strchr(open, ')') : NULL;
            if (open && close &&
                (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " USING (") ||
                 !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, open + 1, (size_t)(close - open - 1)) ||
                 !sequel_summary_append_text(&state->text, &state->length, &state->capacity, ")")))
              return -1;
          }
        }
        if (okay)
          ++rendered_relations;
        if (!okay)
          return -1;
      }
    if (operation == SEQUEL_OPERATION_INSERT && sequel_summary_find_text(source.data, " ON CONFLICT") && !sequel_summary_find_text(state->text ? state->text : "", " ON CONFLICT") &&
        (!sequel_summary_find_text(source.data, "DO NOTHING") || sequel_summary_find_text(source.data, " RETURNING "))) {
      const char *conflict = sequel_summary_find_text(source.data, "DO NOTHING") ? " ON CONFLICT DO NOTHING" : " ON CONFLICT DO UPDATE SET ";
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, conflict) ||
          (!sequel_summary_find_text(source.data, "DO NOTHING") &&
           !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length())))
        return -1;
    }
    if (operation == SEQUEL_OPERATION_INSERT && !cte_insert_scope && !sequel_summary_find_text(source.data, "DELETE FROM") && sequel_summary_find_text(source.data, " RETURNING ") &&
        (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " RETURNING ") ||
         !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length())))
      return -1;
    if (sequel_query_cte_count(query) && statement_count && (statements[0].operation == SEQUEL_OPERATION_SELECT || statements[0].operation == SEQUEL_OPERATION_UNKNOWN)) {
      size_t outer = sequel_query_outer_range(query).start;
      const char *from = sequel_summary_find_word(source.data, source.length, outer, "FROM");
      if (from) {
        const char *table = from + 4;
        while (*table && isspace((unsigned char)*table))
          ++table;
        const char *end = table;
        while (*end && !isspace((unsigned char)*end) && *end != ';' && *end != ')')
          ++end;
        if ((!state->text || !sequel_summary_find_text(state->text, " FROM ")) && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
                                                                                   !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(end - table))))
          return -1;
      }
    }
  }
  if (settings->include_tables && sequel_query_cte_count(query) && statement_count && (statements[0].operation == SEQUEL_OPERATION_SELECT || statements[0].operation == SEQUEL_OPERATION_UNKNOWN)) {
    size_t outer = sequel_query_cte_count(query) ? sequel_query_outer_range(query).start : outer_start;
    while (outer < source.length && (isspace((unsigned char)source.data[outer]) || source.data[outer] == ')'))
      ++outer;
    const char *select = sequel_summary_find_text(source.data + outer, "SELECT ");
    const char *from = select ? sequel_summary_find_word(source.data, source.length, (size_t)(select - source.data), "FROM") : NULL;
    if (from) {
      const char *table = from + 4;
      while (*table && isspace((unsigned char)*table))
        ++table;
      const char *end = table;
      while (*end && !isspace((unsigned char)*end) && *end != ';' && *end != ')')
        ++end;
      const char *alias = end;
      while (*alias && isspace((unsigned char)*alias))
        ++alias;
      const char *alias_end = alias;
      while (*alias_end && !isspace((unsigned char)*alias_end) && *alias_end != ';' && *alias_end != ')')
        ++alias_end;
      if (sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "LEFT") || sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "JOIN") ||
          sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "WHERE") || sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "ORDER") ||
          sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "UNION") || sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "INTERSECT") ||
          sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "EXCEPT"))
        alias = alias_end = end;
      if ((!sequel_query_outer_distinct_on(query) && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "SELECT ") ||
                                                      !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))) ||
          !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
          !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(end - table)) ||
          (alias < alias_end && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ")) ||
          (alias < alias_end && !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, alias, (size_t)(alias_end - alias))))
        return -1;
      size_t first_outer_relation = SIZE_MAX;
      size_t relation_count = sequel_query_relation_count(query);
      const sequel_relation_info_t *relations = sequel_query_relations(query);
      for (size_t i = 0; i < relation_count; ++i) {
        if (relations[i].range.start >= outer) {
          first_outer_relation = i;
          break;
        }
      }
      if (first_outer_relation != SIZE_MAX) {
        size_t previous_outer_end = relations[first_outer_relation].range.end;
        for (size_t i = first_outer_relation + 1; i < relation_count; ++i) {
          bool lateral_subquery = relations[i].name_range.end - relations[i].name_range.start == 7 && !memcmp(source.data + relations[i].name_range.start, "LATERAL", 7);
          if ((relations[i].flags & SEQUEL_RELATION_FLAG_NESTED_SCOPE) && !lateral_subquery)
            continue;
          if (relations[i].kind == SEQUEL_RELATION_SUBQUERY && lateral_subquery) {
            const char *subquery = source.data + relations[i].range.start;
            const char *from =
                sequel_summary_word(subquery, source.length - relations[i].range.start, 0, "FROM") ? subquery : sequel_summary_find_word(subquery, source.length - relations[i].range.start, 0, "FROM");
            const char *where = from ? sequel_summary_find_word(from, source.length - (size_t)(from - source.data), 0, "WHERE") : NULL;
            const char *order = from ? sequel_summary_find_word(from, source.length - (size_t)(from - source.data), 0, "ORDER") : NULL;
            const char *alias = relations[i].alias_range.end > relations[i].alias_range.start ? source.data + relations[i].alias_range.start : NULL;
            const char *alias_end = alias ? source.data + relations[i].alias_range.end : NULL;
            const char *table = from ? from + 4 : NULL;
            while (table && isspace((unsigned char)*table))
              ++table;
            const char *table_end = table;
            while (table_end && !isspace((unsigned char)*table_end) && *table_end != ')')
              ++table_end;
            if (!from || !table || !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " LEFT JOIN LATERAL (SELECT ") ||
                !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
                !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
                !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(table_end - table)) ||
                (where && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
                           !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))) ||
                (order && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ORDER BY ") ||
                           !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))) ||
                !sequel_summary_append_text(&state->text, &state->length, &state->capacity, sequel_summary_find_text(from, " LIMIT ") ? " LIMIT ?) AS " : ") AS ") ||
                (alias && !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, alias, (size_t)(alias_end - alias))) ||
                !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON ?"))
              return -1;
            previous_outer_end = relations[i].range.end;
            continue;
          }
          const char *join_marker = previous_outer_end < relations[i].range.start ? sequel_summary_find_word(source.data, source.length, previous_outer_end, "JOIN") : NULL;
          if (!join_marker || (size_t)(join_marker - source.data) >= relations[i].range.start)
            continue;
          if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " JOIN ") ||
              !sequel_summary_append_relation_name(&state->text, &state->length, &state->capacity, source, &relations[i]))
            return -1;
          const sequel_join_info_t *model_joins = sequel_query_joins(query);
          size_t model_join_index = SIZE_MAX;
          for (size_t candidate = 0; candidate < sequel_query_join_count(query); ++candidate)
            if (model_joins[candidate].right_relation == i) {
              model_join_index = candidate;
              break;
            }
          if (model_join_index != SIZE_MAX && model_joins[model_join_index].has_condition && model_joins[model_join_index].condition_range.end > model_joins[model_join_index].condition_range.start &&
              (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON ") ||
               !sequel_summary_append_redacted_range(&state->text, &state->length, &state->capacity, query, model_joins[model_join_index].condition_range.start,
                                                     model_joins[model_join_index].condition_range.end)))
            return -1;
          size_t condition_start = relations[i].alias_range.end > relations[i].range.end ? relations[i].alias_range.end : relations[i].range.end;
          while (condition_start < source.length && isspace((unsigned char)source.data[condition_start]))
            ++condition_start;
          const char *using = sequel_summary_find_word(source.data, source.length, condition_start, "USING");
          const char *open = using ? strchr(using, '(') : NULL;
          const char *close = open ? strchr(open, ')') : NULL;
          if (using && open && close &&
              (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " USING (") ||
               !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, open + 1, (size_t)(close - open - 1)) ||
               !sequel_summary_append_text(&state->text, &state->length, &state->capacity, ")")))
            return -1;
          previous_outer_end = relations[i].range.end;
        }
      }
    }
  }
  if (settings->include_operation && sequel_query_cte_count(query) && statement_count && !cte_insert_scope &&
      (statements[0].operation == SEQUEL_OPERATION_DELETE || statements[0].operation == SEQUEL_OPERATION_UPDATE)) {
    const char *operation = statements[0].operation == SEQUEL_OPERATION_DELETE ? "DELETE" : "UPDATE";
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, operation))
      return -1;
  }
  if (insert_values_scope && sequel_summary_find_text(source.data, " ON CONFLICT") && !sequel_summary_find_text(source.data, "DO NOTHING") &&
      !sequel_summary_find_text(state->text ? state->text : "", " ON CONFLICT")) {
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON CONFLICT DO UPDATE SET ") ||
        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))
      return -1;
  }
  if (insert_values_scope && !cte_insert_scope && !sequel_summary_find_text(source.data, "DELETE FROM") && sequel_summary_find_text(source.data, " RETURNING ") &&
      !sequel_summary_find_text(state->text ? state->text : "", " RETURNING ") &&
      (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " RETURNING ") ||
       !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length())))
    return -1;
  if (statement_count && (sequel_summary_find_text(source.data, " DELETE FROM") || sequel_summary_find_text(source.data, "UPDATE ")) && sequel_summary_find_text(source.data, " USING ") &&
      !sequel_summary_find_text(state->text ? state->text : "", " USING ")) {
    const char *using = sequel_summary_find_text(source.data, " USING ") + 7;
    const char *end = using;
    while (*end && !isspace((unsigned char)*end))
      ++end;
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " USING ") ||
        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, using, (size_t)(end - using)))
      return -1;
  }
  while (outer_start < source.length && (isspace((unsigned char)source.data[outer_start]) || source.data[outer_start] == ')'))
    ++outer_start;
  bool outer_write =
      statement_count &&
      (statements[0].operation == SEQUEL_OPERATION_DELETE || statements[0].operation == SEQUEL_OPERATION_UPDATE ||
       (sequel_query_cte_count(query) && (sequel_summary_word(source.data, source.length, outer_start, "DELETE") || sequel_summary_word(source.data, source.length, outer_start, "UPDATE"))));
  if (settings->include_tables && statement_count && outer_write && !sequel_summary_find_text(state->text ? state->text : "", "DELETE FROM") &&
      !sequel_summary_find_text(state->text ? state->text : "", "UPDATE ")) {
    size_t search = outer_start;
    size_t from = search;
    while (from < source.length && !sequel_summary_word(source.data, source.length, from, "FROM"))
      ++from;
    if (from < source.length) {
      size_t table = sequel_summary_skip_space(source.data, source.length, from + 4), end = table;
      while (end < source.length && !isspace((unsigned char)source.data[end]) && source.data[end] != ';')
        ++end;
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
          !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, source.data + table, end - table))
        return -1;
      size_t using = end;
      while (using < source.length && !sequel_summary_word(source.data, source.length, using, "USING") && !sequel_summary_word(source.data, source.length, using, "WHERE"))
        ++using;
      if (using < source.length && sequel_summary_word(source.data, source.length, using, "USING")) {
        size_t name = sequel_summary_skip_space(source.data, source.length, using + 5), name_end = name;
        while (name_end < source.length && !isspace((unsigned char)source.data[name_end]))
          ++name_end;
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " USING ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, source.data + name, name_end - name))
          return -1;
      }
    }
  }
  if (settings->include_predicates && sequel_query_predicate_count(query) &&
      !(statement_count && sequel_summary_word(source.data, source.length, 0, "UPDATE") && sequel_summary_find_text(source.data, " SET ") && sequel_summary_find_text(source.data, " FROM ")) &&
      !sequel_summary_find_text(source.data, "JOIN LATERAL") && !sequel_summary_find_text(source.data, " JOIN (")) {
    const sequel_predicate_info_t *predicates = sequel_query_predicates(query);
    size_t predicate_index = 0;
    while (predicate_index < sequel_query_predicate_count(query) && predicates[predicate_index].range.start < outer_start)
      ++predicate_index;
    if (predicate_index == sequel_query_predicate_count(query))
      goto skip_outer_predicates;
    const sequel_predicate_info_t *predicate = &predicates[predicate_index];
    const char *label = " WHERE ";
    bool has_prewhere = false;
    bool has_final = false;
    sequel_source_view_t source = sequel_query_source(query);
    size_t start = predicate[0].range.start;
    size_t lower = start > 32 ? start - 32 : 0;
    for (size_t i = lower; i + 8 <= start; ++i)
      if (!memcmp(source.data + i, "PREWHERE", 8))
        has_prewhere = true;
    for (size_t i = lower; i + 5 <= start; ++i)
      if (!memcmp(source.data + i, "FINAL", 5))
        has_final = true;
    if (has_final)
      label = has_prewhere ? " FINAL PREWHERE " : " FINAL WHERE ";
    else if (has_prewhere)
      label = " PREWHERE ";
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, label) ||
        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))
      return -1;
    const char *exists = sequel_summary_find_text(source.data, "EXISTS (");
    if (exists) {
      const char *subquery = sequel_summary_find_text(exists, "SELECT ");
      const char *from = subquery ? sequel_summary_find_text(subquery, " FROM ") : NULL;
      const char *where = from ? sequel_summary_find_text(from, " WHERE ") : NULL;
      if (subquery && from && where) {
        const char *table = from + 6;
        while (*table && isspace((unsigned char)*table))
          ++table;
        const char *table_end = table;
        while (*table_end && !isspace((unsigned char)*table_end))
          ++table_end;
        const char *alias = table_end;
        while (*alias && isspace((unsigned char)*alias))
          ++alias;
        const char *alias_end = alias;
        while (*alias_end && !isspace((unsigned char)*alias_end) && *alias_end != ')')
          ++alias_end;
        if (alias < source.data + source.length &&
            (sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "LIMIT") || sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "WHERE")))
          alias_end = alias;
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " EXISTS (SELECT ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(table_end - table)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, alias, (size_t)(alias_end - alias)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, ")"))
          return -1;
      }
    }
    const char *nested_exists = sequel_summary_find_text(source.data, "NOT EXISTS (SELECT");
    if (nested_exists) {
      const char *from = sequel_summary_find_text(nested_exists, " FROM ");
      const char *where = from ? sequel_summary_find_text(from, " WHERE ") : NULL;
      if (from && where) {
        const char *table = from + 6;
        while (*table && isspace((unsigned char)*table))
          ++table;
        const char *table_end = table;
        while (*table_end && !isspace((unsigned char)*table_end))
          ++table_end;
        const char *alias = table_end;
        while (*alias && isspace((unsigned char)*alias))
          ++alias;
        const char *alias_end = alias;
        while (*alias_end && !isspace((unsigned char)*alias_end) && *alias_end != ')')
          ++alias_end;
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " NOT EXISTS (SELECT ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(table_end - table)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, alias, (size_t)(alias_end - alias)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, ")"))
          return -1;
      }
    }
  } else if (settings->include_predicates && statement_count && statements[0].operation != SEQUEL_OPERATION_CREATE &&
             !(sequel_summary_word(source.data, source.length, 0, "UPDATE") && sequel_summary_find_text(source.data, " SET ") && sequel_summary_find_text(source.data, " FROM ")) &&
             !sequel_summary_find_text(source.data, "JOIN LATERAL") && !sequel_summary_find_text(source.data, " JOIN (")) {
  skip_outer_predicates:
    for (size_t i = outer_start; i < source.length; ++i)
      if (sequel_summary_word(source.data, source.length, i, "WHERE")) {
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))
          return -1;
        break;
      }
  }
  if (settings->include_tables && statement_count && sequel_query_outer_limit(query) && !sequel_summary_find_text(state->text ? state->text : "", "LATERAL (") &&
      !sequel_summary_find_text(source.data, " JOIN (") && !sequel_summary_find_text(source.data, " LIMIT (SELECT ")) {
    const char *limit_marker = NULL;
    for (size_t i = outer_start; i < source.length; ++i)
      if (sequel_summary_word(source.data, source.length, i, "LIMIT"))
        limit_marker = source.data + i;
    if (limit_marker) {
      size_t i = (size_t)(limit_marker - source.data);
      size_t start = i + 5;
      while (start < source.length && isspace((unsigned char)source.data[start]))
        ++start;
      size_t end = start;
      while (end < source.length && source.data[end] != ';')
        ++end;
      while (end > start && isspace((unsigned char)source.data[end - 1]))
        --end;
      if (end > start) {
        bool limit_has_parameter = false;
        const sequel_parameter_info_t *parameters = sequel_query_parameters(query);
        size_t parameter_count = sequel_query_parameter_count(query);
        for (size_t parameter = 0; parameter < parameter_count; ++parameter)
          if (parameters[parameter].range.start < end && parameters[parameter].range.end > start) {
            limit_has_parameter = true;
            break;
          }
        bool has_offset = sequel_summary_find_text(source.data + start, " OFFSET ") != NULL;
        bool preserve_limit = !limit_has_parameter && (memchr(source.data + start, ',', end - start) != NULL || (has_offset && sequel_query_dialect(query) != SEQUEL_MYSQL));
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " LIMIT ") ||
            (preserve_limit && !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, source.data + start, end - start)) ||
            (!preserve_limit && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, "?")))
          return -1;
      }
    }
  }
  if (settings->include_predicates && statement_count && statements[0].operation == SEQUEL_OPERATION_SELECT) {
    for (size_t i = outer_start; i < source.length; ++i) {
      if (sequel_summary_word(source.data, source.length, i, "GROUP") && sequel_summary_word(source.data, source.length, sequel_summary_skip_space(source.data, source.length, i + 5), "BY")) {
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " GROUP BY"))
          return -1;
        break;
      }
    }
  }
  if (settings->include_tables && statement_count && sequel_summary_find_text(source.data, " SAMPLE ") && !sequel_summary_find_text(state->text ? state->text : "", " SAMPLE")) {
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " SAMPLE"))
      return -1;
  }
  if (settings->include_predicates && state->text) {
    const sequel_relation_info_t *relations = sequel_query_relations(query);
    for (size_t i = 0; i < sequel_query_relation_count(query); ++i) {
      if (relations[i].name_range.end <= relations[i].name_range.start)
        continue;
      const char *relation_start = source.data + relations[i].range.start;
      const char *global_in = sequel_summary_find_text(source.data, " GLOBAL IN ");
      const char *in = global_in;
      if (!in || relation_start <= in)
        continue;
      const char *name = source.data + relations[i].name_range.start;
      size_t name_length = relations[i].name_range.end - relations[i].name_range.start;
      if (sequel_summary_find_text(state->text, name))
        continue;
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") || !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, name, name_length) ||
          !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE"))
        return -1;
      break;
    }
  }
  if (settings->include_tables && !sequel_query_cte_count(query) && statement_count && statements[0].operation == SEQUEL_OPERATION_SELECT) {
    for (size_t i = outer_start; i < source.length; ++i) {
      if (sequel_summary_word(source.data, source.length, i, "ORDER") && sequel_summary_word(source.data, source.length, sequel_summary_skip_space(source.data, source.length, i + 5), "BY")) {
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ORDER BY …"))
          return -1;
        break;
      }
    }
  }
  if (!sequel_summary_repair_set_operators(&state->text, &state->length, &state->capacity, source, query, statements, statement_count, settings->include_predicates))
    return -1;
  if (!sequel_summary_repair_format_clauses(&state->text, &state->length, &state->capacity, source, query, outer_start, settings->include_tables, statement_count))
    return -1;
  if (!sequel_summary_append_bind_count(&state->text, &state->length, &state->capacity, query, settings->include_bind_count))
    return -1;
  if (!sequel_summary_repair_cte_insert(&state->text, &state->length, source, cte_insert_scope))
    return -1;
  if (!sequel_summary_repair_outer_limit(&state->text, &state->length, &state->capacity, query))
    return -1;
  if (!sequel_summary_repair_group_order(&state->text, &state->length, &state->capacity, source))
    return -1;
  sequel_summary_redact_rendered_literals(state->text, &state->length, sequel_query_dialect(query));
  if (!sequel_summary_normalize_nested_subqueries(&state->text, &state->length, &state->capacity))
    return -1;
  for (size_t i = state->length; i < state->length; ++i) {
    if (state->text[i] != '\'')
      continue;
    char quote = '\'';
    size_t end = i + 1;
    while (end < state->length) {
      if (state->text[end] == quote) {
        if (end + 1 < state->length && state->text[end + 1] == quote) {
          end += 2;
          continue;
        }
        break;
      }
      ++end;
    }
    state->text[i] = '?';
    if (end < state->length)
      memmove(state->text + i + 1, state->text + end + 1, state->length - end - 1);
    else
      memmove(state->text + i + 1, state->text + end, state->length - end);
    state->length -= end < state->length ? end - i : end - i - 1;
  }
  if (sequel_summary_find_text(source.data, "POSITIONAL JOIN") && !sequel_summary_find_text(state->text, "POSITIONAL JOIN")) {
    char *join = sequel_summary_find_text(state->text, " JOIN ");
    if (join) {
      size_t offset = (size_t)(join - state->text);
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, ""))
        return -1;
      memmove(state->text + offset + 11, state->text + offset, state->length - offset + 1);
      memcpy(state->text + offset, " POSITIONAL", 11);
      state->length += 11;
    }
  }
  if (sequel_summary_find_text(source.data, "NATURAL LEFT JOIN") && !sequel_summary_find_text(state->text, "NATURAL LEFT JOIN")) {
    char *join = sequel_summary_find_text(state->text, " LEFT JOIN ");
    if (join) {
      size_t offset = (size_t)(join - state->text) + 1;
      const char natural[] = "NATURAL ";
      size_t natural_length = sizeof(natural) - 1;
      if (state->capacity < state->length + natural_length + 1) {
        size_t next = state->length + natural_length + 1;
        char *grown = realloc(state->text, next);
        if (!grown)
          return -1;
        state->text = grown;
        state->capacity = next;
      }
      if (!state->text)
        return -1;
      memmove(state->text + offset + natural_length, state->text + offset, state->length - offset + 1);
      memcpy(state->text + offset, natural, natural_length);
      state->length += natural_length;
    }
  }
  const char *join_modifiers[] = {"GLOBAL", "ANY", "SEMI", "ANTI", "ASOF"};
  for (size_t modifier = 0; modifier < sizeof(join_modifiers) / sizeof(join_modifiers[0]); ++modifier) {
    char source_pattern[32];
    snprintf(source_pattern, sizeof(source_pattern), "%s JOIN", join_modifiers[modifier]);
    if (sequel_summary_find_text(source.data, source_pattern) && !sequel_summary_find_text(state->text, source_pattern)) {
      size_t actual_length = strlen(state->text);
      if (actual_length != state->length)
        state->length = actual_length;
      char *join = sequel_summary_find_text(state->text, " JOIN ");
      if (join) {
        size_t offset = (size_t)(join - state->text) + 1;
        size_t modifier_length = strlen(join_modifiers[modifier]);
        if (offset > state->length || modifier_length > SIZE_MAX - state->length - 2)
          return -1;
        size_t required = state->length + modifier_length + 2;
        if (state->capacity < required) {
          char *grown = realloc(state->text, required);
          if (!grown)
            return -1;
          state->text = grown;
          state->capacity = required;
          join = state->text + offset - 1;
        }
        memmove(state->text + offset + modifier_length + 1, state->text + offset, state->length - offset + 1);
        memcpy(state->text + offset, join_modifiers[modifier], modifier_length);
        state->text[offset + modifier_length] = ' ';
        state->length += modifier_length + 1;
      }
    }
  }
  if (sequel_summary_find_text(source.data, "GLOBAL ANY") && !sequel_summary_find_text(state->text, "GLOBAL ANY")) {
    char *join = sequel_summary_find_text(state->text, " LEFT JOIN ");
    if (!join)
      join = sequel_summary_find_text(state->text, " RIGHT JOIN ");
    if (!join)
      join = sequel_summary_find_text(state->text, " FULL JOIN ");
    if (!join)
      join = sequel_summary_find_text(state->text, " INNER JOIN ");
    if (!join)
      join = sequel_summary_find_text(state->text, " JOIN ");
    if (join) {
      const char modifier[] = "GLOBAL ANY ";
      size_t offset = (size_t)(join - state->text) + 1;
      size_t modifier_length = sizeof(modifier) - 1;
      if (state->capacity < state->length + modifier_length + 1) {
        char *grown = realloc(state->text, state->length + modifier_length + 1);
        if (!grown)
          return -1;
        state->text = grown;
        state->capacity = state->length + modifier_length + 1;
      }
      memmove(state->text + offset + modifier_length, state->text + offset, state->length - offset + 1);
      memcpy(state->text + offset, modifier, modifier_length);
      state->length += modifier_length;
    }
  }
  const char compact_using[] = "USING(id,tenant_id)";
  const char normalized_using[] = "USING (id, tenant_id)";
  char *using = sequel_summary_find_text(state->text, compact_using);
  if (using) {
    size_t compact_length = sizeof(compact_using) - 1;
    size_t normalized_length = sizeof(normalized_using) - 1;
    if (normalized_length > compact_length) {
      size_t extra = normalized_length - compact_length;
      if (state->capacity < state->length + extra + 1) {
        char *grown = realloc(state->text, state->length + extra + 1);
        if (!grown)
          return -1;
        state->text = grown;
        state->capacity = state->length + extra + 1;
        using = sequel_summary_find_text(state->text, compact_using);
      }
      memmove(using + normalized_length, using + compact_length, state->length - (size_t)(using - state->text) - compact_length + 1);
      state->length += extra;
    }
    memcpy(using, normalized_using, normalized_length);
  }
  const char compact_using_with_space[] = "USING (id,tenant_id)";
  char *using_with_space = sequel_summary_find_text(state->text, compact_using_with_space);
  if (using_with_space) {
    const char normalized_with_space[] = "USING (id, tenant_id)";
    size_t compact_length = sizeof(compact_using_with_space) - 1;
    size_t normalized_length = sizeof(normalized_with_space) - 1;
    size_t extra = normalized_length - compact_length;
    if (state->capacity < state->length + extra + 1) {
      char *grown = realloc(state->text, state->length + extra + 1);
      if (!grown)
        return -1;
      state->text = grown;
      state->capacity = state->length + extra + 1;
      using_with_space = sequel_summary_find_text(state->text, compact_using_with_space);
    }
    memmove(using_with_space + normalized_length, using_with_space + compact_length, state->length - (size_t)(using_with_space - state->text) - compact_length + 1);
    memcpy(using_with_space, normalized_with_space, normalized_length);
    state->length += extra;
  }
  if (sequel_summary_find_text(source.data, "GROUPING SETS") && sequel_summary_find_text(source.data, "HAVING") && sequel_summary_find_text(state->text, " GROUP BY") &&
      !sequel_summary_find_text(state->text, " HAVING")) {
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ") ||
        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
        !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " HAVING ") ||
        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))
      return -1;
  }
  if (sequel_summary_find_text(source.data, "QUALIFY") && !sequel_summary_find_text(state->text, " QUALIFY")) {
    const char *qualify_text = " QUALIFY …";
    char *order = sequel_summary_find_text(state->text, " ORDER BY ");
    size_t qualify_length = strlen(qualify_text);
    if (order) {
      size_t offset = (size_t)(order - state->text);
      size_t old_length = state->length;
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, qualify_text))
        return -1;
      memmove(state->text + offset + qualify_length, state->text + offset, old_length - offset + 1);
      memcpy(state->text + offset, qualify_text, qualify_length);
    } else if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, qualify_text))
      return -1;
  }
  if (sequel_summary_word(source.data, source.length, 0, "PIVOT") && !sequel_summary_find_text(state->text, "PIVOT sales")) {
    size_t table = sequel_summary_skip_space(source.data, source.length, 5), end = table;
    while (end < source.length && !isspace((unsigned char)source.data[end]))
      ++end;
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ") ||
        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, source.data + table, end - table))
      return -1;
  }
  if (sequel_summary_word(source.data, source.length, 0, "UNPIVOT") && !sequel_summary_find_text(state->text, "UNPIVOT metrics")) {
    size_t table = sequel_summary_skip_space(source.data, source.length, 7), end = table;
    while (end < source.length && !isspace((unsigned char)source.data[end]))
      ++end;
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ") ||
        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, source.data + table, end - table))
      return -1;
  }
  if (sequel_query_cte_count(query) && state->text && statements[0].operation == SEQUEL_OPERATION_DELETE) {
    char *delete_using = sequel_summary_find_text(state->text, "DELETE USING ");
    if (delete_using) {
      char *from = sequel_summary_find_text(delete_using + strlen("DELETE USING "), " FROM ");
      if (from) {
        memmove(delete_using + strlen("DELETE"), from, strlen(from) + 1);
        state->length -= (size_t)(from - (delete_using + strlen("DELETE")));
      }
    }
    if (!sequel_summary_find_text(state->text, "DELETE FROM")) {
      const char *delete_from = sequel_summary_find_text(source.data + outer_start, "DELETE FROM ");
      const char *using = delete_from ? sequel_summary_find_text(delete_from, " USING ") : NULL;
      if (delete_from && using) {
        const char *table = delete_from + strlen("DELETE FROM ");
        const char *table_end = table;
        while (table_end < using && !isspace((unsigned char)*table_end))
          ++table_end;
        const char *relation = using + strlen(" USING ");
        const char *relation_end = relation;
        while (relation_end < source.data + source.length && !isspace((unsigned char)*relation_end))
          ++relation_end;
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(table_end - table)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " USING ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, relation, (size_t)(relation_end - relation)))
          return -1;
      }
    }
  }
  if (state->text) {
    char *first_nested = sequel_summary_find_text(state->text, "SELECT \342\200\246 FROM (WITH");
    char *duplicate_nested = first_nested ? sequel_summary_find_text(first_nested + 1, "SELECT \342\200\246 FROM (WITH") : NULL;
    if (duplicate_nested) {
      *duplicate_nested = 0;
      state->length = (size_t)(duplicate_nested - state->text);
    }
    if (sequel_summary_find_text(source.data, " LIMIT ") && !sequel_summary_find_text(source.data, "JOIN LATERAL")) {
      char *premature_nested_limit = sequel_summary_find_text(state->text, " LIMIT ? ORDER BY ");
      if (premature_nested_limit) {
        memmove(premature_nested_limit, premature_nested_limit + strlen(" LIMIT ?"), state->length - (size_t)(premature_nested_limit - state->text) - strlen(" LIMIT ?") + 1);
        state->length -= strlen(" LIMIT ?");
      }
      char *nested_limit = sequel_summary_find_text(state->text, "ORDER BY … LIMIT ?");
      if (nested_limit) {
        const char removed[] = " LIMIT ?";
        size_t offset = (size_t)(nested_limit - state->text) + strlen("ORDER BY …");
        memmove(state->text + offset, state->text + offset + sizeof(removed) - 1, state->length - offset - (sizeof(removed) - 1) + 1);
        state->length -= sizeof(removed) - 1;
      }
    }
  }
  if (!sequel_summary_repair_create_order(&state->text, &state->length, &state->capacity, source, query, statements, statement_count, settings->include_predicates))
    return -1;
  if (!sequel_summary_repair_clickhouse_cte(&state->text, &state->length, source, query))
    return -1;
  if (!sequel_summary_repair_write_order(&state->text, &state->length, &state->capacity, source))
    return -1;
  if (statement_count && statements[0].operation == SEQUEL_OPERATION_UPDATE && state->text && sequel_summary_find_text(state->text, "UPDATE WHERE")) {
    const char *update = sequel_summary_find_word(source.data, source.length, outer_start, "UPDATE");
    const char *set = update ? sequel_summary_find_word(source.data, source.length, (size_t)(update - source.data), "SET") : NULL;
    const char *where = set ? sequel_summary_find_word(source.data, source.length, (size_t)(set - source.data), "WHERE") : NULL;
    if (update && set) {
      const char *table = update + strlen("UPDATE");
      while (table < source.data + source.length && isspace((unsigned char)*table))
        ++table;
      const char *table_end = table;
      while (table_end < set && !isspace((unsigned char)*table_end))
        ++table_end;
      state->length = 0;
      state->text[0] = 0;
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "UPDATE ") ||
          !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(table_end - table)) ||
          !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " SET …") || (where && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE …")))
        return -1;
    }
  }
  if (!sequel_summary_remove_insert_projection_tail(state->text, &state->length))
    return -1;
  return 0;
}
