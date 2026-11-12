#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#include <sequel/summary.h>
#include <sequel/sanitizer.h>

#include "internal.h"
#include "../tokens.h"

static const char summary_ellipsis[] = "\342\200\246";

sequel_status_t sequel_summary_render_document_impl(const sequel_query_document_t *query,
                                                    const sequel_summary_options_t *options,
                                                    sequel_text_result_t *out) {
  if (!query || !out)
    return SEQUEL_ERR_NULL;
  sequel_summary_options_t defaults;
  sequel_summary_options_init(&defaults);
  const sequel_summary_options_t *settings = options ? options : &defaults;
  char *text = NULL;
  size_t length = 0, capacity = 0;
  size_t statement_count = sequel_query_statement_count(query);
  const sequel_statement_info_t *statements = sequel_query_statements(query);
  sequel_source_view_t source = sequel_query_source(query);
  int special_route = 0;
  if (!sequel_summary_begin_render(&text, &length, &capacity, query, settings, source, statement_count, statements,
                                   &special_route))
    goto no_memory;
  if (special_route == 1) {
    *out = (sequel_text_result_t){
        .data = text, .length = length, .capacity = capacity, .mode = SEQUEL_FORMAT_SYNTAX_AWARE};
    return SEQUEL_OK;
  }
  if (special_route == 2)
    goto summary_postprocess;
  size_t cte_prefix_end = 0;
  size_t outer_start = 0;
  bool nested_cte_scope = false;
  bool insert_values_scope = false;
  bool cte_insert_scope = false;
  bool scalar_with_scope =
      sequel_summary_word(source.data, source.length, 0, "WITH") && sequel_query_cte_count(query) == 0;
  int table_prefix =
      sequel_summary_append_table_prefix(&text, &length, &capacity, query, settings, source, statement_count);
  if (!table_prefix)
    goto no_memory;
  if (table_prefix == 2)
    goto summary_postprocess;
  if (settings->include_operation && sequel_query_cte_count(query)) {
    const sequel_cte_info_t *ctes = sequel_query_ctes(query);
    bool recursive = source.length >= 13 && sequel_summary_find_text(source.data, "WITH RECURSIVE") == source.data;
    if (!sequel_summary_append_cte_definitions(&text, &length, &capacity, source, ctes, sequel_query_cte_count(query),
                                               recursive, &cte_prefix_end))
      goto no_memory;
    if (sequel_query_outer_distinct_on(query) &&
        (!sequel_summary_append_text(&text, &length, &capacity, "SELECT DISTINCT ON (…) …")))
      goto no_memory;
    if (!sequel_summary_append_nested_cte_projection(&text, &length, &capacity, source, &nested_cte_scope))
      goto no_memory;
    if (!sequel_summary_append_cte_insert_shape(&text, &length, &capacity, source, ctes, sequel_query_cte_count(query),
                                                &cte_insert_scope))
      goto no_memory;
    outer_start = sequel_query_outer_range(query).start;
  }
  if (settings->include_operation && scalar_with_scope &&
      !sequel_summary_append_text(&text, &length, &capacity, "WITH "))
    goto no_memory;
  if (statement_count > 1 && settings->redact_literals) {
    sequel_text_result_t sanitized = {0};
    if (sequel_sanitize_tokens(sequel_syntax_lexed(sequel_query_syntax(query)),
                               SEQUEL_SANITIZE_REMOVE_COMMENTS | SEQUEL_SANITIZE_REPLACE_LITERALS,
                               &sanitized) != SEQUEL_OK)
      goto no_memory;
    char *multi = NULL;
    size_t multi_length = 0, multi_capacity = 0;
    bool after_first = false;
    size_t statement_index = 0;
    bool compact_tail = false;
    bool compact_initial_set = false;
    for (size_t i = 0; i < sanitized.length; ++i) {
      if (!compact_initial_set && i == 0 && sequel_summary_word(sanitized.data, sanitized.length, 0, "SET")) {
        if (!sequel_summary_append_text(&multi, &multi_length, &multi_capacity, "SET")) {
          free(multi);
          sequel_text_result_free(&sanitized);
          goto no_memory;
        }
        while (i < sanitized.length && sanitized.data[i] != ';')
          ++i;
        compact_initial_set = true;
      }
      if (sanitized.data[i] == ';') {
        after_first = true, ++statement_index;
        compact_tail = false;
      }
      bool compact_literals = after_first && statement_index < statement_count &&
                              statements[statement_index].operation != SEQUEL_OPERATION_SELECT;
      if (compact_literals && !compact_tail &&
          (sequel_summary_word(sanitized.data, sanitized.length, i, "WHERE") ||
           sequel_summary_word(sanitized.data, sanitized.length, i, "SET"))) {
        const char *keyword = sequel_summary_word(sanitized.data, sanitized.length, i, "WHERE") ? " WHERE " : " SET ";
        if (!sequel_summary_append_text(&multi, &multi_length, &multi_capacity, keyword) ||
            !sequel_summary_append_bytes(&multi, &multi_length, &multi_capacity, summary_ellipsis,
                                         sizeof(summary_ellipsis) - 1)) {
          free(multi);
          sequel_text_result_free(&sanitized);
          goto no_memory;
        }
        while (i < sanitized.length && sanitized.data[i] != ';')
          ++i;
        --i;
        compact_tail = true;
        continue;
      }
      size_t tail_prefix = 0;
      if (compact_literals && !compact_tail && i + 5 <= sanitized.length && !memcmp(sanitized.data + i, " SET ", 5))
        tail_prefix = 5;
      else if (compact_literals && !compact_tail && i + 7 <= sanitized.length &&
               !memcmp(sanitized.data + i, " WHERE ", 7))
        tail_prefix = 7;
      if (tail_prefix) {
        if (!sequel_summary_append_bytes(&multi, &multi_length, &multi_capacity, sanitized.data + i, tail_prefix) ||
            !sequel_summary_append_bytes(&multi, &multi_length, &multi_capacity, summary_ellipsis,
                                         sizeof(summary_ellipsis) - 1)) {
          free(multi);
          sequel_text_result_free(&sanitized);
          goto no_memory;
        }
        i += tail_prefix;
        while (i < sanitized.length && sanitized.data[i] != ';')
          ++i;
        --i;
        compact_tail = true;
      } else if (compact_literals && sanitized.data[i] == '?') {
        if (!sequel_summary_append_bytes(&multi, &multi_length, &multi_capacity, summary_ellipsis,
                                         sizeof(summary_ellipsis) - 1)) {
          free(multi);
          sequel_text_result_free(&sanitized);
          goto no_memory;
        }
      } else if (!sequel_summary_append_bytes(&multi, &multi_length, &multi_capacity, sanitized.data + i, 1)) {
        free(multi);
        sequel_text_result_free(&sanitized);
        goto no_memory;
      }
    }
    sequel_text_result_free(&sanitized);
    for (char *empty = sequel_summary_find_text(multi, ";;"); empty; empty = sequel_summary_find_text(multi, ";;")) {
      memmove(empty + 1, empty + 2, strlen(empty + 2) + 1);
      --multi_length;
    }
    for (char *empty = sequel_summary_find_text(multi, "; ;"); empty; empty = sequel_summary_find_text(multi, "; ;")) {
      memmove(empty + 1, empty + 3, strlen(empty + 3) + 1);
      multi_length -= 2;
    }
    while (multi_length && multi[multi_length - 1] == ';')
      multi[--multi_length] = 0;
    size_t normalized = 0;
    for (size_t read = 0; read < multi_length; ++read) {
      if (multi[read] == ';') {
        while (normalized && isspace((unsigned char)multi[normalized - 1]))
          --normalized;
        multi[normalized++] = ';';
        while (read + 1 < multi_length && isspace((unsigned char)multi[read + 1]))
          ++read;
        if (read + 1 < multi_length)
          multi[normalized++] = ' ';
      } else {
        multi[normalized++] = multi[read];
      }
    }
    while (normalized && isspace((unsigned char)multi[normalized - 1]))
      --normalized;
    multi[normalized] = 0;
    size_t leading = 0;
    while (leading < normalized && isspace((unsigned char)multi[leading]))
      ++leading;
    if (leading) {
      memmove(multi, multi + leading, normalized - leading + 1);
      normalized -= leading;
    }
    multi_length = normalized;
    sequel_text_result_free(out);
    *out = (sequel_text_result_t){
        .data = multi, .length = multi_length, .capacity = multi_capacity, .mode = SEQUEL_FORMAT_SYNTAX_AWARE};
    return SEQUEL_OK;
  }
  if (settings->include_operation && statement_count && !sequel_query_cte_count(query) && !nested_cte_scope &&
      !cte_insert_scope &&
      !sequel_summary_append_statement_shape(&text, &length, &capacity, source, query, settings, &statements[0]))
    goto no_memory;
  if (settings->include_operation && statement_count && !sequel_query_cte_count(query) &&
      !sequel_summary_append_projection_shape(&text, &length, &capacity, source, query, &statements[0]))
    goto no_memory;
  /* A SELECT projection can span the FROM token when the syntax tree was
   * recovered from incomplete input.  Keep the summary shape independent of
   * that recovery artifact; relation rendering owns the FROM/JOIN clauses. */
  if (settings->include_tables && statement_count && !sequel_query_cte_count(query) &&
      statements[0].operation == SEQUEL_OPERATION_SELECT && text && sequel_summary_find_text(text, "FROM ")) {
    length = 0;
    text[0] = '\0';
    if (!sequel_summary_append_text(
            &text, &length, &capacity,
            sequel_summary_find_text(source.data, "SELECT DISTINCT ON") ? "SELECT DISTINCT ON (…) …" : "SELECT …"))
      goto no_memory;
  }
  if (settings->include_tables && statement_count && statements[0].operation == SEQUEL_OPERATION_SELECT &&
      sequel_query_relation_count(query) == 0 &&
      (sequel_summary_has_clause(query, SEQUEL_KEYWORD_FROM) || sequel_summary_has_token(query, SEQUEL_KEYWORD_FROM))) {
    if (!sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
        !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
      goto no_memory;
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
      while (end < source.length && !isspace((unsigned char)source.data[end]) && source.data[end] != ';' &&
             source.data[end] != '(')
        ++end;
      if (end > name && (!sequel_summary_append_text(&text, &length, &capacity, " INTO ") ||
                         !sequel_summary_append_bytes(&text, &length, &capacity, source.data + name, end - name)))
        goto no_memory;
    }
    for (size_t i = 0; i < source.length; ++i)
      if (sequel_summary_word(source.data, source.length, i, "VALUES")) {
        insert_values_scope = true;
        if (!sequel_summary_append_text(&text, &length, &capacity, " VALUES ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
          goto no_memory;
        break;
      }
    for (size_t i = 0; i < source.length; ++i)
      if (sequel_summary_word(source.data, source.length, i, "SELECT")) {
        if (!sequel_summary_append_text(&text, &length, &capacity, " SELECT ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
          goto no_memory;
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
      if (name < source.length && source.data[name] != '\'' &&
          !(dialect == SEQUEL_MYSQL && (source.data[name] == '"' || source.data[name] == '`')) &&
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
        const char *index_label =
            sequel_summary_word(source.data, source.length, unique, "UNIQUE") ? " UNIQUE INDEX " : " INDEX ";
        if (end > name && !sequel_summary_append_text(&text, &length, &capacity, index_label))
          goto no_memory;
        if (end > name && !sequel_summary_append_bytes(&text, &length, &capacity, source.data + name, end - name))
          goto no_memory;
        if (end > name) {
          size_t on = end;
          while (on < source.length && isspace((unsigned char)source.data[on]))
            ++on;
          if (settings->redact_relations && sequel_summary_word(source.data, source.length, on, "ON") &&
              !sequel_summary_append_text(&text, &length, &capacity, " ON ..."))
            goto no_memory;
        }
      }
    }
  }
  if (settings->include_tables && statement_count && !sequel_query_cte_count(query) &&
      statements[0].operation == SEQUEL_OPERATION_SELECT && sequel_summary_find_text(source.data, " OVER (") &&
      sequel_summary_find_text(source.data, " FILTER (") && sequel_summary_find_text(source.data, " FROM ")) {
    const char *from = sequel_summary_find_text(source.data, " FROM ") + 6;
    while (*from && isspace((unsigned char)*from))
      ++from;
    const char *end = from;
    while (end < source.data + source.length && !isspace((unsigned char)*end))
      ++end;
    length = 0;
    if (text)
      text[0] = '\0';
    if (!sequel_summary_append_text(&text, &length, &capacity, "SELECT ") ||
        !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
        !sequel_summary_append_text(&text, &length, &capacity, " OVER (…)") ||
        !sequel_summary_append_text(&text, &length, &capacity, " FILTER (…)") ||
        !sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
        !sequel_summary_append_bytes(&text, &length, &capacity, from, (size_t)(end - from)))
      goto no_memory;
  }
  if (settings->include_tables && statement_count && !sequel_query_cte_count(query) &&
      statements[0].operation == SEQUEL_OPERATION_SELECT &&
      (sequel_summary_find_word(source.data, source.length, 0, "UNION") ||
       sequel_summary_find_word(source.data, source.length, 0, "EXCEPT") ||
       sequel_summary_find_word(source.data, source.length, 0, "INTERSECT"))) {
    length = 0;
    if (text)
      text[0] = '\0';
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
          if (!sequel_summary_append_bytes(&text, &length, &capacity, operator_start,
                                           (size_t)(select - operator_start)))
            goto no_memory;
        }
        if (!sequel_summary_append_text(&text, &length, &capacity, "(SELECT ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, table, (size_t)(table_end - table)))
          goto no_memory;
        if (limit) {
          const char *value = limit + 7;
          while (*value && isspace((unsigned char)*value))
            ++value;
          const char *value_end = value;
          while (value_end < close && !isspace((unsigned char)*value_end) && *value_end != ')')
            ++value_end;
          if (!sequel_summary_append_text(&text, &length, &capacity, " LIMIT ") ||
              !sequel_summary_append_text(&text, &length, &capacity, value[0] == '$' || value[0] == '?' ? "?" : "1"))
            goto no_memory;
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
            if (!sequel_summary_append_bytes(&text, &length, &capacity, inner_set,
                                             (size_t)(inner_select - inner_set)) ||
                !sequel_summary_append_text(&text, &length, &capacity, " SELECT ") ||
                !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis,
                                             sizeof(summary_ellipsis) - 1) ||
                !sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
                !sequel_summary_append_bytes(&text, &length, &capacity, inner_table,
                                             (size_t)(inner_table_end - inner_table)))
              goto no_memory;
          }
        }
        if (!sequel_summary_append_text(&text, &length, &capacity, ")"))
          goto no_memory;
        cursor = close + 1;
        first_set = false;
        if (!sequel_summary_find_text(cursor, "SELECT "))
          break;
      }
      const char *order = sequel_summary_find_text(cursor, " ORDER BY ");
      if (order && (!sequel_summary_append_text(&text, &length, &capacity, " ORDER BY …") ||
                    (sequel_summary_find_text(order, " LIMIT ") &&
                     (!sequel_summary_append_text(&text, &length, &capacity, " LIMIT ") ||
                      !sequel_summary_append_bytes(
                          &text, &length, &capacity, sequel_summary_find_text(order, " LIMIT ") + 7,
                          source.data + source.length - (sequel_summary_find_text(order, " LIMIT ") + 7))))))
        goto no_memory;
      goto summary_postprocess;
    } else {
      const char *cursor = source.data;
      bool first_set = true;
      if (sequel_summary_find_text(source.data, "JOIN LATERAL") &&
          sequel_summary_find_text(source.data, " UNION ALL ") && sequel_query_relation_count(query) >= 5 &&
          sequel_query_join_count(query) >= 2) {
        const sequel_relation_info_t *model_relations = sequel_query_relations(query);
        const sequel_join_info_t *model_joins = sequel_query_joins(query);
        const char *lateral = sequel_summary_find_text(source.data, "LATERAL");
        const char *open = lateral ? strchr(lateral, '(') : NULL;
        const char *close = open ? sequel_summary_matching_close(open, source.data + source.length) : NULL;
        const char *inner_from =
            open && close ? sequel_summary_find_word(open, (size_t)(close - open), 0, "FROM") : NULL;
        const char *union_all = sequel_summary_find_text(source.data, " UNION ALL ");
        const char *using =
            union_all
                ? sequel_summary_find_word(union_all, source.length - (size_t)(union_all - source.data), 0, "USING")
                : NULL;
        const char *using_open = using ? strchr(using, '(') : NULL;
        const char *using_close = using_open ? strchr(using_open, ')') : NULL;
        const char *alias =
            close ? sequel_summary_find_word(close, (size_t)(source.data + source.length - close), 0, "AS") : NULL;
        const char *alias_name = alias ? alias + 2 : NULL;
        if (alias_name)
          alias_name += sequel_summary_skip_space(alias_name, source.data + source.length - alias_name, 0);
        const char *alias_end = alias_name;
        while (alias_end && alias_end < source.data + source.length &&
               (isalnum((unsigned char)*alias_end) || *alias_end == '_'))
          ++alias_end;
        if (!sequel_summary_append_text(&text, &length, &capacity, "SELECT ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
            !sequel_summary_append_relation_name(&text, &length, &capacity, source, &model_relations[0]) ||
            !sequel_summary_append_text(&text, &length, &capacity, " JOIN ") ||
            !sequel_summary_append_text(&text, &length, &capacity, "LATERAL (SELECT ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !sequel_summary_append_text(&text, &length, &capacity, " FROM ") || !inner_from || !close ||
            !sequel_summary_append_relation_name(&text, &length, &capacity, source, &model_relations[2]) ||
            !sequel_summary_append_text(&text, &length, &capacity, " LIMIT ?)") || !alias_name || !alias_end ||
            !sequel_summary_append_text(&text, &length, &capacity, " AS ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, alias_name, (size_t)(alias_end - alias_name)) ||
            !sequel_summary_append_text(&text, &length, &capacity, " ON ") ||
            !sequel_summary_append_redacted_range(&text, &length, &capacity, query,
                                                  model_joins[0].condition_range.start,
                                                  model_joins[0].condition_range.end) ||
            !sequel_summary_append_text(&text, &length, &capacity, " UNION ALL SELECT ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
            !sequel_summary_append_relation_name(&text, &length, &capacity, source, &model_relations[3]) ||
            !sequel_summary_append_text(&text, &length, &capacity, " JOIN ") ||
            !sequel_summary_append_relation_name(&text, &length, &capacity, source, &model_relations[4]) ||
            !using_open || !using_close || !sequel_summary_append_text(&text, &length, &capacity, " USING (") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, using_open + 1,
                                         (size_t)(using_close - using_open - 1)) ||
            !sequel_summary_append_text(&text, &length, &capacity, ")"))
          goto no_memory;
        goto summary_postprocess;
      }
      while (cursor < source.data + source.length) {
        const char *select =
            sequel_summary_find_word(source.data, source.length, (size_t)(cursor - source.data), "SELECT");
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
        if (!first_set && !sequel_summary_append_text(&text, &length, &capacity, " "))
          goto no_memory;
        if (!sequel_summary_append_text(&text, &length, &capacity, first_set ? "SELECT " : "SELECT ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, table, (size_t)(end - table)))
          goto no_memory;
        const char *op = sequel_summary_find_word(source.data, source.length, (size_t)(end - source.data), "UNION");
        const char *except =
            sequel_summary_find_word(source.data, source.length, (size_t)(end - source.data), "EXCEPT");
        const char *intersect =
            sequel_summary_find_word(source.data, source.length, (size_t)(end - source.data), "INTERSECT");
        const char *next = op;
        if (!next || (except && except < next))
          next = except;
        if (!next || (intersect && intersect < next))
          next = intersect;
        if (!next) {
          const char *where =
              sequel_summary_find_word(source.data, source.length, (size_t)(end - source.data), "WHERE");
          if (where &&
              (!sequel_summary_append_text(&text, &length, &capacity, " WHERE ") ||
               !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1)))
            goto no_memory;
          const char *limit =
              sequel_summary_find_word(source.data, source.length, (size_t)(end - source.data), "LIMIT");
          if (limit) {
            const char *value = limit + 5;
            while (*value && isspace((unsigned char)*value))
              ++value;
            if (!sequel_summary_append_text(&text, &length, &capacity, " LIMIT ") ||
                !sequel_summary_append_text(&text, &length, &capacity, *value == '$' || *value == '?' ? "?" : "1"))
              goto no_memory;
          }
          break;
        }
        const char *where = sequel_summary_find_word(source.data, source.length, (size_t)(end - source.data), "WHERE");
        if (where && where < next &&
            (!sequel_summary_append_text(&text, &length, &capacity, " WHERE ") ||
             !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1)))
          goto no_memory;
        const char *following_select =
            sequel_summary_find_word(source.data, source.length, (size_t)(next - source.data + 1), "SELECT");
        if (!following_select)
          following_select =
              sequel_summary_find_word(source.data, source.length, (size_t)(next - source.data + 1), "SELECT");
        if (!following_select || !sequel_summary_append_text(&text, &length, &capacity, " ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, next, (size_t)(following_select - next)))
          goto no_memory;
        cursor = following_select + (following_select[0] == '(' ? 1 : 0);
        first_set = false;
      }
      goto summary_postprocess;
    }
  }
  if (settings->include_tables && statement_count && statements[0].operation == SEQUEL_OPERATION_UPDATE &&
      sequel_summary_find_text(source.data, " SET ") && sequel_summary_find_text(source.data, " FROM ")) {
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
      if (clauses[i].statement_index == 0 && clauses[i].keyword_id == SEQUEL_KEYWORD_FROM &&
          clauses[i].range.start > (size_t)(set - source.data)) {
        from = source.data + clauses[i].range.start;
        break;
      }
    if (!from)
      from = set ? sequel_summary_find_text(
                       subquery_end ? subquery_end + 1 : (subquery_close ? subquery_close + 1 : set + 5), " FROM ")
                 : NULL;
    const char *where = from ? sequel_summary_find_text(from + 4, " WHERE ") : NULL;
    if (set && from) {
      const char *table_end = set;
      const char *from_table = from + 4;
      while (*from_table && isspace((unsigned char)*from_table))
        ++from_table;
      const char *from_end = where ? where : source.data + source.length;
      while (from_end > from_table && isspace((unsigned char)from_end[-1]))
        --from_end;
      length = 0;
      if (text)
        text[0] = '\0';
      if (!sequel_summary_append_text(&text, &length, &capacity, "UPDATE ") ||
          !sequel_summary_append_bytes(&text, &length, &capacity, table, (size_t)(table_end - table)) ||
          !sequel_summary_append_text(&text, &length, &capacity, " SET ") ||
          !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          (subquery && subquery_from && subquery_end
               ? (!sequel_summary_append_text(&text, &length, &capacity, " (SELECT ") ||
                  !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis,
                                               sizeof(summary_ellipsis) - 1) ||
                  !sequel_summary_append_bytes(&text, &length, &capacity, subquery_from,
                                               (size_t)(subquery_end - subquery_from)) ||
                  !sequel_summary_append_text(&text, &length, &capacity, ")"))
               : !sequel_summary_append_bytes(&text, &length, &capacity, set + 5, (size_t)(from - (set + 5)))) ||
          !sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
          !sequel_summary_append_bytes(&text, &length, &capacity, from_table, (size_t)(from_end - from_table)) ||
          (where &&
           (!sequel_summary_append_text(&text, &length, &capacity, " WHERE ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))))
        goto no_memory;
    }
  }
  if (settings->include_tables && !sequel_query_cte_count(query) && !nested_cte_scope && !insert_values_scope &&
      !cte_insert_scope && !sequel_summary_find_text(source.data, " UNION ") &&
      !sequel_summary_find_text(source.data, " EXCEPT ") && !sequel_summary_find_text(source.data, " INTERSECT ") &&
      !(sequel_summary_find_text(source.data, " OVER (") && sequel_summary_find_text(source.data, " FILTER (")) &&
      !(statements[0].operation == SEQUEL_OPERATION_UPDATE && sequel_summary_find_text(source.data, " SET ") &&
        sequel_summary_find_text(source.data, " FROM "))) {
    size_t relation_count = sequel_query_relation_count(query);
    const sequel_relation_info_t *relations = sequel_query_relations(query);
    sequel_operation_t operation = statement_count ? statements[0].operation : SEQUEL_OPERATION_UNKNOWN;
    const sequel_join_info_t *joins = sequel_query_joins(query);
    size_t rendered_relations = 0;
    bool grouped_relation = false;
    const char *group_from = NULL;
    for (const char *candidate = sequel_summary_find_text(source.data, "FROM ("); candidate;
         candidate = sequel_summary_find_text(candidate + 6, "FROM ("))
      group_from = candidate;
    if (!group_from)
      group_from = sequel_summary_find_text(source.data, "FROM (WITH");
    if (group_from && !sequel_summary_find_text(group_from, "CROSS JOIN LATERAL") &&
        !sequel_summary_find_text(group_from, "JOIN LATERAL") &&
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
        if (!sequel_summary_append_text(&text, &length, &capacity, " FROM (") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, first, first_end - first) ||
            !sequel_summary_append_text(&text, &length, &capacity, " AS ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, first_alias, first_alias_end - first_alias) ||
            !sequel_summary_append_text(&text, &length, &capacity, " JOIN ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, second, second_end - second) ||
            !sequel_summary_append_text(&text, &length, &capacity, " AS ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, second_alias, second_alias_end - second_alias) ||
            !sequel_summary_append_bytes(&text, &length, &capacity, second_alias_end,
                                         condition && condition < close ? (size_t)(condition - second_alias_end)
                                                                        : close - second_alias_end) ||
            (condition && condition < close &&
             (!sequel_summary_append_text(&text, &length, &capacity, " ON ") ||
              !sequel_summary_append_text(&text, &length, &capacity, "?"))) ||
            !sequel_summary_append_text(&text, &length, &capacity, ")"))
          goto no_memory;
        grouped_relation = true;
      }
    }
    size_t relation_passes = sequel_query_join_count(query) && sequel_summary_find_text(source.data, "JOIN") &&
                                     !(sequel_query_cte_count(query) && statement_count &&
                                       (statements[0].operation == SEQUEL_OPERATION_INSERT ||
                                        statements[0].operation == SEQUEL_OPERATION_UPDATE ||
                                        statements[0].operation == SEQUEL_OPERATION_DELETE))
                                 ? 2
                                 : 1;
    for (size_t relation_pass = 0; relation_pass < relation_passes; ++relation_pass)
      for (size_t i = 0; i < relation_count; ++i) {
        if (sequel_query_cte_count(query) && statements[0].operation == SEQUEL_OPERATION_SELECT &&
            !sequel_summary_find_text(source.data, " LATERAL "))
          break;
        if (grouped_relation)
          break;
        if (operation == SEQUEL_OPERATION_CREATE) {
          if (!text || !sequel_summary_find_text(text, " INDEX "))
            break;
          if (text && sequel_summary_find_text(text, " INDEX ")) {
            if (sequel_summary_find_text(text, " ON ..."))
              break;
            if (!sequel_summary_find_text(text, " ON ...") &&
                !sequel_summary_append_text(&text, &length, &capacity, " ON "))
              goto no_memory;
            if (settings->redact_relations) {
              if (!sequel_summary_append_text(&text, &length, &capacity, "..."))
                goto no_memory;
              break;
            }
          }
        }
        bool nested = false;
        size_t join_index = SIZE_MAX;
        for (size_t j = 0; j < sequel_query_join_count(query); ++j)
          if (joins[j].right_relation == i) {
            join_index = j;
            break;
          }
        if (!sequel_summary_find_text(source.data, "JOIN"))
          join_index = SIZE_MAX;
        if (relation_passes == 2 && (relation_pass == 0) == (join_index != SIZE_MAX))
          continue;
        for (size_t p = 0;
             (relations[i].flags & SEQUEL_RELATION_FLAG_NESTED_SCOPE) && p < sequel_query_predicate_count(query); ++p) {
          const sequel_predicate_info_t *predicate = &sequel_query_predicates(query)[p];
          if (sequel_source_range_contains(predicate->range, relations[i].range)) {
            nested = true;
            break;
          }
        }
        if (!nested && (relations[i].flags & SEQUEL_RELATION_FLAG_NESTED_SCOPE))
          for (size_t j = 0; j < sequel_query_join_count(query); ++j)
            if (joins[j].has_condition && joins[j].right_relation != i &&
                sequel_source_range_contains(joins[j].condition_range, relations[i].range)) {
              nested = true;
              break;
            }
        if (nested && i != 0) {
          continue;
        }
        if ((relations[i].flags & SEQUEL_RELATION_FLAG_PROJECTION_SCOPE) && join_index == SIZE_MAX) {
          continue;
        }
        bool nested_relation = (relations[i].flags & SEQUEL_RELATION_FLAG_NESTED_SCOPE) != 0;
        bool lateral_relation_name = relations[i].name_range.end - relations[i].name_range.start == 7 &&
                                     !memcmp(source.data + relations[i].name_range.start, "LATERAL", 7);
        bool parenthesized_table_input =
            relations[i].name_range.start < source.length && source.data[relations[i].name_range.start] == '(' &&
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
                if (sequel_summary_word(source.data, source.length, cursor, "ASOF") &&
                    !sequel_summary_cursor_is_quoted_identifier(query, cursor)) {
                  prefix = " ASOF LEFT JOIN ";
                  break;
                }
                if (sequel_summary_word(source.data, source.length, cursor, "CROSS") &&
                    !sequel_summary_cursor_is_quoted_identifier(query, cursor)) {
                  prefix = cursor + 18 <= source.length && !memcmp(source.data + cursor, "CROSS JOIN LATERAL", 18)
                               ? " CROSS JOIN LATERAL "
                               : " CROSS JOIN ";
                  break;
                }
                if (sequel_summary_word(source.data, source.length, cursor, "LEFT") &&
                    !sequel_summary_cursor_is_quoted_identifier(query, cursor)) {
                  prefix = cursor + 10 <= source.length && !memcmp(source.data + cursor, "LEFT OUTER", 10)
                               ? " LEFT OUTER JOIN "
                               : " LEFT JOIN ";
                  break;
                }
                if (sequel_summary_word(source.data, source.length, cursor, "RIGHT") &&
                    !sequel_summary_cursor_is_quoted_identifier(query, cursor)) {
                  prefix = " RIGHT JOIN ";
                  break;
                }
                if (sequel_summary_word(source.data, source.length, cursor, "FULL") &&
                    !sequel_summary_cursor_is_quoted_identifier(query, cursor)) {
                  prefix = cursor + 10 <= source.length && !memcmp(source.data + cursor, "FULL OUTER", 10)
                               ? " FULL OUTER JOIN "
                               : " FULL JOIN ";
                  break;
                }
                if (sequel_summary_word(source.data, source.length, cursor, "INNER") &&
                    !sequel_summary_cursor_is_quoted_identifier(query, cursor)) {
                  prefix = " INNER JOIN ";
                  break;
                }
                if (sequel_summary_word(source.data, source.length, cursor, "NATURAL") &&
                    !sequel_summary_cursor_is_quoted_identifier(query, cursor)) {
                  prefix = cursor + 12 <= source.length && !memcmp(source.data + cursor, "NATURAL LEFT", 12)
                               ? " NATURAL LEFT JOIN "
                               : " NATURAL JOIN ";
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
            if (join_marker && (size_t)(join_marker - source.data) < relations[i].range.start &&
                !sequel_summary_cursor_is_quoted_identifier(query, (size_t)(join_marker - source.data)))
              prefix = " JOIN ";
          }
          if (!sequel_summary_append_text(&text, &length, &capacity, prefix))
            goto no_memory;
        }
        bool lateral_relation = relations[i].name_range.end - relations[i].name_range.start == 7 &&
                                !memcmp(source.data + relations[i].name_range.start, "LATERAL", 7);
        bool parenthesized_relation = source.data[relations[i].name_range.start] == '(';
        if (join_index == SIZE_MAX && parenthesized_relation &&
            sequel_summary_find_text(source.data + relations[i].name_range.start, "SELECT") &&
            sequel_summary_find_text(source.data + relations[i].name_range.start, "unnest(")) {
          if (!sequel_summary_append_text(&text, &length, &capacity, "(SELECT unnest(…), unnest(…))"))
            goto no_memory;
          if (relations[i].alias_range.end > relations[i].alias_range.start &&
              (!sequel_summary_append_text(&text, &length, &capacity, " AS ") ||
               !sequel_summary_append_bytes(&text, &length, &capacity, source.data + relations[i].alias_range.start,
                                            relations[i].alias_range.end - relations[i].alias_range.start)))
            goto no_memory;
          ++rendered_relations;
          continue;
        }
        if (join_index != SIZE_MAX && relations[i].name_range.start < source.length &&
            (lateral_relation || parenthesized_relation)) {
          if (lateral_relation && sequel_summary_find_text(source.data, "CROSS JOIN LATERAL") &&
              sequel_summary_find_text(source.data, "unnest(")) {
            const char *open = source.data + relations[i].name_range.end;
            while (open < source.data + source.length && *open != '(')
              ++open;
            const char *close = open < source.data + source.length
                                    ? sequel_summary_matching_close(open, source.data + source.length)
                                    : NULL;
            if (!close ||
                !sequel_summary_append_redacted_range(&text, &length, &capacity, query, (size_t)(open - source.data),
                                                      (size_t)(close + 1 - source.data)))
              goto no_memory;
            if (relations[i].alias_range.end > relations[i].alias_range.start &&
                (!sequel_summary_append_text(&text, &length, &capacity, " AS ") ||
                 !sequel_summary_append_bytes(&text, &length, &capacity, source.data + relations[i].alias_range.start,
                                              relations[i].alias_range.end - relations[i].alias_range.start)))
              goto no_memory;
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
            bool outer_on_true =
                outer_on && sequel_summary_word(outer_on + 4, (size_t)(outer_on_end - (outer_on + 4)), 0, "true");
            if (!sequel_summary_append_text(&text, &length, &capacity,
                                            lateral_relation &&
                                                    !sequel_summary_find_text(source.data, "CROSS JOIN LATERAL")
                                                ? "LATERAL (SELECT "
                                                : "(SELECT ") ||
                !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis,
                                             sizeof(summary_ellipsis) - 1) ||
                !sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
                !sequel_summary_append_bytes(&text, &length, &capacity, table, (size_t)(table_end - table)) ||
                !sequel_summary_append_redacted_range(&text, &length, &capacity, query, (size_t)(limit - source.data),
                                                      (size_t)(close + 1 - source.data)) ||
                !sequel_summary_append_text(&text, &length, &capacity, " AS ") ||
                !sequel_summary_append_bytes(&text, &length, &capacity, alias, (size_t)(alias_end - alias)) ||
                !sequel_summary_append_text(&text, &length, &capacity, " ON ") || !outer_on ||
                (outer_on_true ? !sequel_summary_append_text(&text, &length, &capacity, "?")
                               : !sequel_summary_append_bytes(&text, &length, &capacity, outer_on + 4,
                                                              (size_t)(outer_on_end - (outer_on + 4)))) ||
                ((outer_limit_marker || sequel_summary_find_text(close + 1, "$")) &&
                 !sequel_summary_append_text(&text, &length, &capacity, " LIMIT ?")))
              goto no_memory;
            ++rendered_relations;
            continue;
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
            bool outer_on_true =
                outer_on && sequel_summary_word(outer_on + 4, (size_t)(outer_on_end - (outer_on + 4)), 0, "true");
            const char *inner_alias = table_end;
            const char *inner_alias_end = table_end;
            if (cross_lateral) {
              inner_alias = table_end;
              while (*inner_alias && isspace((unsigned char)*inner_alias))
                ++inner_alias;
              inner_alias_end = inner_alias;
              while (*inner_alias_end && !isspace((unsigned char)*inner_alias_end) && *inner_alias_end != ')')
                ++inner_alias_end;
              if (inner_alias >= where || inner_alias_end > where)
                inner_alias = inner_alias_end = table_end;
            }
            const char *alias = close + 1;
            while (*alias && isspace((unsigned char)*alias))
              ++alias;
            const char *alias_end = alias;
            while (*alias_end && !isspace((unsigned char)*alias_end))
              ++alias_end;
            if (!sequel_summary_append_text(&text, &length, &capacity,
                                            lateral_relation &&
                                                    !sequel_summary_find_text(source.data, "CROSS JOIN LATERAL")
                                                ? "LATERAL (SELECT "
                                                : "(SELECT ") ||
                !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis,
                                             sizeof(summary_ellipsis) - 1) ||
                !sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
                !sequel_summary_append_bytes(&text, &length, &capacity, table, (size_t)(table_end - table)) ||
                (cross_lateral && inner_alias_end > inner_alias &&
                 (!sequel_summary_append_text(&text, &length, &capacity, " AS ") ||
                  !sequel_summary_append_bytes(&text, &length, &capacity, inner_alias,
                                               (size_t)(inner_alias_end - inner_alias)))) ||
                !sequel_summary_append_text(&text, &length, &capacity, " WHERE ") ||
                !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis,
                                             sizeof(summary_ellipsis) - 1) ||
                (cross_lateral && order && order < close &&
                 (!sequel_summary_append_text(&text, &length, &capacity, " ORDER BY ") ||
                  !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis,
                                               sizeof(summary_ellipsis) - 1))) ||
                (limit && limit < close &&
                 !sequel_summary_append_bytes(&text, &length, &capacity, limit, (size_t)(close + 1 - limit))) ||
                !sequel_summary_append_text(&text, &length, &capacity, (limit && limit < close) ? " AS " : ") AS ") ||
                !sequel_summary_append_bytes(&text, &length, &capacity, alias, (size_t)(alias_end - alias)) ||
                (omit_cross_condition
                     ? 0
                     : (!sequel_summary_append_text(&text, &length, &capacity, " ON ") || !outer_on ||
                        (outer_on_true ? !sequel_summary_append_text(&text, &length, &capacity, "?")
                                       : !sequel_summary_append_bytes(&text, &length, &capacity, outer_on + 4,
                                                                      (size_t)(outer_on_end - (outer_on + 4)))))) ||
                (outer_limit && (!next_join || outer_limit < next_join) &&
                 !sequel_summary_append_text(&text, &length, &capacity, " LIMIT ?")))
              goto no_memory;
            ++rendered_relations;
            continue;
          }
        }
        int okay = sequel_summary_append_relation_name(&text, &length, &capacity, source, &relations[i]);
        if (okay && join_index != SIZE_MAX && joins[join_index].has_condition &&
            joins[join_index].condition_range.end > joins[join_index].condition_range.start) {
          okay = sequel_summary_append_text(&text, &length, &capacity, " ON ") &&
                 sequel_summary_append_redacted_range(&text, &length, &capacity, query,
                                                      joins[join_index].condition_range.start,
                                                      joins[join_index].condition_range.end);
        }
        if (okay && join_index != SIZE_MAX && !joins[join_index].has_condition) {
          size_t condition_start = relations[i].alias_range.end > relations[i].range.end ? relations[i].alias_range.end
                                                                                         : relations[i].range.end;
          while (condition_start < source.length && isspace((unsigned char)source.data[condition_start]))
            ++condition_start;
          if (condition_start < source.length && source.data[condition_start] == '(') {
            const char *close =
                sequel_summary_matching_close(source.data + condition_start, source.data + source.length);
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
          const char *join_markers[] = {" JOIN ",       " LEFT JOIN ",  " RIGHT JOIN ",  " FULL JOIN ",
                                        " INNER JOIN ", " CROSS JOIN ", " NATURAL JOIN "};
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
          if (condition_end > condition_start &&
              sequel_summary_word(source.data, source.length, condition_start, "ON")) {
            bool literal_true =
                sequel_summary_word(source.data, source.length, condition_start, "ON") &&
                sequel_summary_word(source.data, source.length,
                                    sequel_summary_skip_space(source.data, source.length, condition_start + 2), "true");
            okay = sequel_summary_append_text(&text, &length, &capacity, " ") &&
                   (literal_true ? (sequel_summary_append_text(&text, &length, &capacity, "ON ") &&
                                    sequel_summary_append_text(&text, &length, &capacity, "?"))
                                 : sequel_summary_append_redacted_range(&text, &length, &capacity, query,
                                                                        condition_start, condition_end));
          }
        }
        if (okay && rendered_relations) {
          const char *using = sequel_summary_find_text(source.data + relations[i].range.end, "USING");
          if (using && i + 1 < relation_count && using >= source.data + relations[i + 1].range.start)
            using = NULL;
          if (using && (!sequel_summary_find_text(source.data + relations[i].range.end, "WHERE") ||
                        using < sequel_summary_find_text(source.data + relations[i].range.end, "WHERE"))) {
            const char *open = strchr(using, '(');
            const char *close = open ? strchr(open, ')') : NULL;
            if (open && close &&
                (!sequel_summary_append_text(&text, &length, &capacity, " USING (") ||
                 !sequel_summary_append_bytes(&text, &length, &capacity, open + 1, (size_t)(close - open - 1)) ||
                 !sequel_summary_append_text(&text, &length, &capacity, ")")))
              goto no_memory;
          }
        }
        if (okay)
          ++rendered_relations;
        if (!okay)
          goto no_memory;
      }
    if (operation == SEQUEL_OPERATION_INSERT && sequel_summary_find_text(source.data, " ON CONFLICT") &&
        !sequel_summary_find_text(text ? text : "", " ON CONFLICT") &&
        (!sequel_summary_find_text(source.data, "DO NOTHING") ||
         sequel_summary_find_text(source.data, " RETURNING "))) {
      const char *conflict = sequel_summary_find_text(source.data, "DO NOTHING") ? " ON CONFLICT DO NOTHING"
                                                                                 : " ON CONFLICT DO UPDATE SET ";
      if (!sequel_summary_append_text(&text, &length, &capacity, conflict) ||
          (!sequel_summary_find_text(source.data, "DO NOTHING") &&
           !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1)))
        goto no_memory;
    }
    if (operation == SEQUEL_OPERATION_INSERT && !cte_insert_scope &&
        !sequel_summary_find_text(source.data, "DELETE FROM") && sequel_summary_find_text(source.data, " RETURNING ") &&
        (!sequel_summary_append_text(&text, &length, &capacity, " RETURNING ") ||
         !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1)))
      goto no_memory;
    if (sequel_query_cte_count(query) && statement_count &&
        (statements[0].operation == SEQUEL_OPERATION_SELECT || statements[0].operation == SEQUEL_OPERATION_UNKNOWN)) {
      size_t outer = sequel_query_outer_range(query).start;
      const char *from = sequel_summary_find_word(source.data, source.length, outer, "FROM");
      if (from) {
        const char *table = from + 4;
        while (*table && isspace((unsigned char)*table))
          ++table;
        const char *end = table;
        while (*end && !isspace((unsigned char)*end) && *end != ';' && *end != ')')
          ++end;
        if ((!text || !sequel_summary_find_text(text, " FROM ")) &&
            (!sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
             !sequel_summary_append_bytes(&text, &length, &capacity, table, (size_t)(end - table))))
          goto no_memory;
      }
    }
  }
  if (settings->include_tables && sequel_query_cte_count(query) && statement_count &&
      (statements[0].operation == SEQUEL_OPERATION_SELECT || statements[0].operation == SEQUEL_OPERATION_UNKNOWN)) {
    size_t outer = sequel_query_cte_count(query) ? sequel_query_outer_range(query).start : outer_start;
    while (outer < source.length && (isspace((unsigned char)source.data[outer]) || source.data[outer] == ')'))
      ++outer;
    const char *select = sequel_summary_find_text(source.data + outer, "SELECT ");
    const char *from =
        select ? sequel_summary_find_word(source.data, source.length, (size_t)(select - source.data), "FROM") : NULL;
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
      if (sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "LEFT") ||
          sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "JOIN") ||
          sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "WHERE") ||
          sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "ORDER") ||
          sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "UNION") ||
          sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "INTERSECT") ||
          sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "EXCEPT"))
        alias = alias_end = end;
      if ((!sequel_query_outer_distinct_on(query) &&
           (!sequel_summary_append_text(&text, &length, &capacity, "SELECT ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))) ||
          !sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
          !sequel_summary_append_bytes(&text, &length, &capacity, table, (size_t)(end - table)) ||
          (alias < alias_end && !sequel_summary_append_text(&text, &length, &capacity, " AS ")) ||
          (alias < alias_end &&
           !sequel_summary_append_bytes(&text, &length, &capacity, alias, (size_t)(alias_end - alias))))
        goto no_memory;
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
          bool lateral_subquery = relations[i].name_range.end - relations[i].name_range.start == 7 &&
                                  !memcmp(source.data + relations[i].name_range.start, "LATERAL", 7);
          if ((relations[i].flags & SEQUEL_RELATION_FLAG_NESTED_SCOPE) && !lateral_subquery)
            continue;
          if (relations[i].kind == SEQUEL_RELATION_SUBQUERY && lateral_subquery) {
            const char *subquery = source.data + relations[i].range.start;
            const char *from =
                sequel_summary_word(subquery, source.length - relations[i].range.start, 0, "FROM")
                    ? subquery
                    : sequel_summary_find_word(subquery, source.length - relations[i].range.start, 0, "FROM");
            const char *where =
                from ? sequel_summary_find_word(from, source.length - (size_t)(from - source.data), 0, "WHERE") : NULL;
            const char *order =
                from ? sequel_summary_find_word(from, source.length - (size_t)(from - source.data), 0, "ORDER") : NULL;
            const char *alias = relations[i].alias_range.end > relations[i].alias_range.start
                                    ? source.data + relations[i].alias_range.start
                                    : NULL;
            const char *alias_end = alias ? source.data + relations[i].alias_range.end : NULL;
            const char *table = from ? from + 4 : NULL;
            while (table && isspace((unsigned char)*table))
              ++table;
            const char *table_end = table;
            while (table_end && !isspace((unsigned char)*table_end) && *table_end != ')')
              ++table_end;
            if (!from || !table ||
                !sequel_summary_append_text(&text, &length, &capacity, " LEFT JOIN LATERAL (SELECT ") ||
                !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis,
                                             sizeof(summary_ellipsis) - 1) ||
                !sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
                !sequel_summary_append_bytes(&text, &length, &capacity, table, (size_t)(table_end - table)) ||
                (where && (!sequel_summary_append_text(&text, &length, &capacity, " WHERE ") ||
                           !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis,
                                                        sizeof(summary_ellipsis) - 1))) ||
                (order && (!sequel_summary_append_text(&text, &length, &capacity, " ORDER BY ") ||
                           !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis,
                                                        sizeof(summary_ellipsis) - 1))) ||
                !sequel_summary_append_text(&text, &length, &capacity,
                                            sequel_summary_find_text(from, " LIMIT ") ? " LIMIT ?) AS " : ") AS ") ||
                (alias &&
                 !sequel_summary_append_bytes(&text, &length, &capacity, alias, (size_t)(alias_end - alias))) ||
                !sequel_summary_append_text(&text, &length, &capacity, " ON ?"))
              goto no_memory;
            previous_outer_end = relations[i].range.end;
            continue;
          }
          const char *join_marker =
              previous_outer_end < relations[i].range.start
                  ? sequel_summary_find_word(source.data, source.length, previous_outer_end, "JOIN")
                  : NULL;
          if (!join_marker || (size_t)(join_marker - source.data) >= relations[i].range.start)
            continue;
          if (!sequel_summary_append_text(&text, &length, &capacity, " JOIN ") ||
              !sequel_summary_append_relation_name(&text, &length, &capacity, source, &relations[i]))
            goto no_memory;
          const sequel_join_info_t *model_joins = sequel_query_joins(query);
          size_t model_join_index = SIZE_MAX;
          for (size_t candidate = 0; candidate < sequel_query_join_count(query); ++candidate)
            if (model_joins[candidate].right_relation == i) {
              model_join_index = candidate;
              break;
            }
          if (model_join_index != SIZE_MAX && model_joins[model_join_index].has_condition &&
              model_joins[model_join_index].condition_range.end > model_joins[model_join_index].condition_range.start &&
              (!sequel_summary_append_text(&text, &length, &capacity, " ON ") ||
               !sequel_summary_append_redacted_range(&text, &length, &capacity, query,
                                                     model_joins[model_join_index].condition_range.start,
                                                     model_joins[model_join_index].condition_range.end)))
            goto no_memory;
          size_t condition_start = relations[i].alias_range.end > relations[i].range.end ? relations[i].alias_range.end
                                                                                         : relations[i].range.end;
          while (condition_start < source.length && isspace((unsigned char)source.data[condition_start]))
            ++condition_start;
          const char *using = sequel_summary_find_word(source.data, source.length, condition_start, "USING");
          const char *open = using ? strchr(using, '(') : NULL;
          const char *close = open ? strchr(open, ')') : NULL;
          if (using && open && close &&
              (!sequel_summary_append_text(&text, &length, &capacity, " USING (") ||
               !sequel_summary_append_bytes(&text, &length, &capacity, open + 1, (size_t)(close - open - 1)) ||
               !sequel_summary_append_text(&text, &length, &capacity, ")")))
            goto no_memory;
          previous_outer_end = relations[i].range.end;
        }
      }
    }
  }
  if (settings->include_operation && sequel_query_cte_count(query) && statement_count && !cte_insert_scope &&
      (statements[0].operation == SEQUEL_OPERATION_DELETE || statements[0].operation == SEQUEL_OPERATION_UPDATE)) {
    const char *operation = statements[0].operation == SEQUEL_OPERATION_DELETE ? "DELETE" : "UPDATE";
    if (!sequel_summary_append_text(&text, &length, &capacity, operation))
      goto no_memory;
  }
  if (insert_values_scope && sequel_summary_find_text(source.data, " ON CONFLICT") &&
      !sequel_summary_find_text(source.data, "DO NOTHING") &&
      !sequel_summary_find_text(text ? text : "", " ON CONFLICT")) {
    if (!sequel_summary_append_text(&text, &length, &capacity, " ON CONFLICT DO UPDATE SET ") ||
        !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
      goto no_memory;
  }
  if (insert_values_scope && !cte_insert_scope && !sequel_summary_find_text(source.data, "DELETE FROM") &&
      sequel_summary_find_text(source.data, " RETURNING ") &&
      !sequel_summary_find_text(text ? text : "", " RETURNING ") &&
      (!sequel_summary_append_text(&text, &length, &capacity, " RETURNING ") ||
       !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1)))
    goto no_memory;
  if (statement_count &&
      (sequel_summary_find_text(source.data, " DELETE FROM") || sequel_summary_find_text(source.data, "UPDATE ")) &&
      sequel_summary_find_text(source.data, " USING ") && !sequel_summary_find_text(text ? text : "", " USING ")) {
    const char *using = sequel_summary_find_text(source.data, " USING ") + 7;
    const char *end = using;
    while (*end && !isspace((unsigned char)*end))
      ++end;
    if (!sequel_summary_append_text(&text, &length, &capacity, " USING ") ||
        !sequel_summary_append_bytes(&text, &length, &capacity, using, (size_t)(end - using)))
      goto no_memory;
  }
  while (outer_start < source.length &&
         (isspace((unsigned char)source.data[outer_start]) || source.data[outer_start] == ')'))
    ++outer_start;
  bool outer_write =
      statement_count &&
      (statements[0].operation == SEQUEL_OPERATION_DELETE || statements[0].operation == SEQUEL_OPERATION_UPDATE ||
       (sequel_query_cte_count(query) && (sequel_summary_word(source.data, source.length, outer_start, "DELETE") ||
                                          sequel_summary_word(source.data, source.length, outer_start, "UPDATE"))));
  if (settings->include_tables && statement_count && outer_write &&
      !sequel_summary_find_text(text ? text : "", "DELETE FROM") &&
      !sequel_summary_find_text(text ? text : "", "UPDATE ")) {
    size_t search = outer_start;
    size_t from = search;
    while (from < source.length && !sequel_summary_word(source.data, source.length, from, "FROM"))
      ++from;
    if (from < source.length) {
      size_t table = sequel_summary_skip_space(source.data, source.length, from + 4), end = table;
      while (end < source.length && !isspace((unsigned char)source.data[end]) && source.data[end] != ';')
        ++end;
      if (!sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
          !sequel_summary_append_bytes(&text, &length, &capacity, source.data + table, end - table))
        goto no_memory;
      size_t using = end;
      while (using < source.length && !sequel_summary_word(source.data, source.length, using, "USING") &&
             !sequel_summary_word(source.data, source.length, using, "WHERE"))
        ++using;
      if (using < source.length && sequel_summary_word(source.data, source.length, using, "USING")) {
        size_t name = sequel_summary_skip_space(source.data, source.length, using + 5), name_end = name;
        while (name_end < source.length && !isspace((unsigned char)source.data[name_end]))
          ++name_end;
        if (!sequel_summary_append_text(&text, &length, &capacity, " USING ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, source.data + name, name_end - name))
          goto no_memory;
      }
    }
  }
  if (settings->include_predicates && sequel_query_predicate_count(query) &&
      !(statement_count && sequel_summary_word(source.data, source.length, 0, "UPDATE") &&
        sequel_summary_find_text(source.data, " SET ") && sequel_summary_find_text(source.data, " FROM ")) &&
      !sequel_summary_find_text(source.data, " JOIN LATERAL ") && !sequel_summary_find_text(source.data, " JOIN (")) {
    const sequel_predicate_info_t *predicates = sequel_query_predicates(query);
    size_t predicate_index = 0;
    while (predicate_index < sequel_query_predicate_count(query) &&
           predicates[predicate_index].range.start < outer_start)
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
    if (!sequel_summary_append_text(&text, &length, &capacity, label) ||
        !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
      goto no_memory;
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
            (sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "LIMIT") ||
             sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "WHERE")))
          alias_end = alias;
        if (!sequel_summary_append_text(&text, &length, &capacity, " EXISTS (SELECT ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, table, (size_t)(table_end - table)) ||
            !sequel_summary_append_text(&text, &length, &capacity, " AS ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, alias, (size_t)(alias_end - alias)) ||
            !sequel_summary_append_text(&text, &length, &capacity, " WHERE ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !sequel_summary_append_text(&text, &length, &capacity, ")"))
          goto no_memory;
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
        if (!sequel_summary_append_text(&text, &length, &capacity, " NOT EXISTS (SELECT ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, table, (size_t)(table_end - table)) ||
            !sequel_summary_append_text(&text, &length, &capacity, " AS ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, alias, (size_t)(alias_end - alias)) ||
            !sequel_summary_append_text(&text, &length, &capacity, " WHERE ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !sequel_summary_append_text(&text, &length, &capacity, ")"))
          goto no_memory;
      }
    }
  } else if (settings->include_predicates && statement_count && statements[0].operation != SEQUEL_OPERATION_CREATE &&
             !(sequel_summary_word(source.data, source.length, 0, "UPDATE") &&
               sequel_summary_find_text(source.data, " SET ") && sequel_summary_find_text(source.data, " FROM ")) &&
             !sequel_summary_find_text(source.data, " JOIN LATERAL ") &&
             !sequel_summary_find_text(source.data, " JOIN (")) {
  skip_outer_predicates:
    for (size_t i = outer_start; i < source.length; ++i)
      if (sequel_summary_word(source.data, source.length, i, "WHERE")) {
        if (!sequel_summary_append_text(&text, &length, &capacity, " WHERE ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
          goto no_memory;
        break;
      }
  }
  if (settings->include_tables && statement_count && sequel_query_outer_limit(query) &&
      !sequel_summary_find_text(text ? text : "", "LATERAL (") && !sequel_summary_find_text(source.data, " JOIN (")) {
    for (size_t i = outer_start; i < source.length; ++i)
      if (sequel_summary_word(source.data, source.length, i, "LIMIT")) {
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
          bool preserve_limit = !limit_has_parameter || memchr(source.data + start, ',', end - start) != NULL;
          if (!sequel_summary_append_text(&text, &length, &capacity, " LIMIT ") ||
              (preserve_limit &&
               !sequel_summary_append_bytes(&text, &length, &capacity, source.data + start, end - start)) ||
              (!preserve_limit && !sequel_summary_append_text(&text, &length, &capacity, "?")))
            goto no_memory;
        }
        break;
      }
  }
  if (settings->include_predicates && statement_count && statements[0].operation == SEQUEL_OPERATION_SELECT) {
    for (size_t i = outer_start; i < source.length; ++i) {
      if (sequel_summary_word(source.data, source.length, i, "GROUP") &&
          sequel_summary_word(source.data, source.length, sequel_summary_skip_space(source.data, source.length, i + 5),
                              "BY")) {
        if (!sequel_summary_append_text(&text, &length, &capacity, " GROUP BY"))
          goto no_memory;
        break;
      }
    }
  }
  if (settings->include_tables && statement_count && sequel_summary_find_text(source.data, " SAMPLE ") &&
      !sequel_summary_find_text(text ? text : "", " SAMPLE")) {
    if (!sequel_summary_append_text(&text, &length, &capacity, " SAMPLE"))
      goto no_memory;
  }
  if (settings->include_predicates && text) {
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
      if (sequel_summary_find_text(text, name))
        continue;
      if (!sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
          !sequel_summary_append_bytes(&text, &length, &capacity, name, name_length) ||
          !sequel_summary_append_text(&text, &length, &capacity, " WHERE"))
        goto no_memory;
      break;
    }
  }
  if (settings->include_tables && !sequel_query_cte_count(query) && statement_count &&
      statements[0].operation == SEQUEL_OPERATION_SELECT) {
    for (size_t i = outer_start; i < source.length; ++i) {
      if (sequel_summary_word(source.data, source.length, i, "ORDER") &&
          sequel_summary_word(source.data, source.length, sequel_summary_skip_space(source.data, source.length, i + 5),
                              "BY")) {
        if (!sequel_summary_append_text(&text, &length, &capacity, " ORDER BY …"))
          goto no_memory;
        break;
      }
    }
  }
  if (!sequel_summary_repair_set_operators(&text, &length, &capacity, source, query, statements, statement_count,
                                           settings->include_predicates))
    goto no_memory;
  if (!sequel_summary_repair_format_clauses(&text, &length, &capacity, source, query, outer_start,
                                            settings->include_tables, statement_count))
    goto no_memory;
  if (!sequel_summary_append_bind_count(&text, &length, &capacity, query, settings->include_bind_count))
    goto no_memory;
  if (!sequel_summary_repair_cte_insert(&text, &length, source, cte_insert_scope))
    goto no_memory;
  if (!sequel_summary_repair_outer_limit(&text, &length, &capacity, query))
    goto no_memory;
  if (!sequel_summary_repair_group_order(&text, &length, &capacity, source))
    goto no_memory;
  sequel_summary_redact_rendered_literals(text, &length);
  if (!sequel_summary_normalize_nested_subqueries(&text, &length, &capacity))
    goto no_memory;
  for (size_t i = length; i < length; ++i) {
    if (text[i] != '\'')
      continue;
    char quote = '\'';
    size_t end = i + 1;
    while (end < length) {
      if (text[end] == quote) {
        if (end + 1 < length && text[end + 1] == quote) {
          end += 2;
          continue;
        }
        break;
      }
      ++end;
    }
    text[i] = '?';
    if (end < length)
      memmove(text + i + 1, text + end + 1, length - end - 1);
    else
      memmove(text + i + 1, text + end, length - end);
    length -= end < length ? end - i : end - i - 1;
  }
  if (sequel_summary_find_text(source.data, "POSITIONAL JOIN") && !sequel_summary_find_text(text, "POSITIONAL JOIN")) {
    char *join = sequel_summary_find_text(text, " JOIN ");
    if (join) {
      size_t offset = (size_t)(join - text);
      if (!sequel_summary_append_text(&text, &length, &capacity, ""))
        goto no_memory;
      memmove(text + offset + 11, text + offset, length - offset + 1);
      memcpy(text + offset, " POSITIONAL", 11);
      length += 11;
    }
  }
  if (sequel_summary_find_text(source.data, "NATURAL LEFT JOIN") &&
      !sequel_summary_find_text(text, "NATURAL LEFT JOIN")) {
    char *join = sequel_summary_find_text(text, " LEFT JOIN ");
    if (join) {
      size_t offset = (size_t)(join - text) + 1;
      const char natural[] = "NATURAL ";
      size_t natural_length = sizeof(natural) - 1;
      if (capacity < length + natural_length + 1) {
        size_t next = length + natural_length + 1;
        char *grown = realloc(text, next);
        if (!grown)
          goto no_memory;
        text = grown;
        capacity = next;
      }
      if (!text)
        goto no_memory;
      memmove(text + offset + natural_length, text + offset, length - offset + 1);
      memcpy(text + offset, natural, natural_length);
      length += natural_length;
    }
  }
  const char *join_modifiers[] = {"GLOBAL", "ANY", "SEMI", "ANTI", "ASOF"};
  for (size_t modifier = 0; modifier < sizeof(join_modifiers) / sizeof(join_modifiers[0]); ++modifier) {
    char source_pattern[32];
    snprintf(source_pattern, sizeof(source_pattern), "%s JOIN", join_modifiers[modifier]);
    if (sequel_summary_find_text(source.data, source_pattern) && !sequel_summary_find_text(text, source_pattern)) {
      size_t actual_length = strlen(text);
      if (actual_length != length)
        length = actual_length;
      char *join = sequel_summary_find_text(text, " JOIN ");
      if (join) {
        size_t offset = (size_t)(join - text) + 1;
        size_t modifier_length = strlen(join_modifiers[modifier]);
        if (offset > length || modifier_length > SIZE_MAX - length - 2)
          goto no_memory;
        size_t required = length + modifier_length + 2;
        if (capacity < required) {
          char *grown = realloc(text, required);
          if (!grown)
            goto no_memory;
          text = grown;
          capacity = required;
          join = text + offset - 1;
        }
        memmove(text + offset + modifier_length + 1, text + offset, length - offset + 1);
        memcpy(text + offset, join_modifiers[modifier], modifier_length);
        text[offset + modifier_length] = ' ';
        length += modifier_length + 1;
      }
    }
  }
  if (sequel_summary_find_text(source.data, "GLOBAL ANY") && !sequel_summary_find_text(text, "GLOBAL ANY")) {
    char *join = sequel_summary_find_text(text, " LEFT JOIN ");
    if (!join)
      join = sequel_summary_find_text(text, " RIGHT JOIN ");
    if (!join)
      join = sequel_summary_find_text(text, " FULL JOIN ");
    if (!join)
      join = sequel_summary_find_text(text, " INNER JOIN ");
    if (!join)
      join = sequel_summary_find_text(text, " JOIN ");
    if (join) {
      const char modifier[] = "GLOBAL ANY ";
      size_t offset = (size_t)(join - text) + 1;
      size_t modifier_length = sizeof(modifier) - 1;
      if (capacity < length + modifier_length + 1) {
        char *grown = realloc(text, length + modifier_length + 1);
        if (!grown)
          goto no_memory;
        text = grown;
        capacity = length + modifier_length + 1;
      }
      memmove(text + offset + modifier_length, text + offset, length - offset + 1);
      memcpy(text + offset, modifier, modifier_length);
      length += modifier_length;
    }
  }
  const char compact_using[] = "USING(id,tenant_id)";
  const char normalized_using[] = "USING (id, tenant_id)";
  char *using = sequel_summary_find_text(text, compact_using);
  if (using) {
    size_t compact_length = sizeof(compact_using) - 1;
    size_t normalized_length = sizeof(normalized_using) - 1;
    if (normalized_length > compact_length) {
      size_t extra = normalized_length - compact_length;
      if (capacity < length + extra + 1) {
        char *grown = realloc(text, length + extra + 1);
        if (!grown)
          goto no_memory;
        text = grown;
        capacity = length + extra + 1;
        using = sequel_summary_find_text(text, compact_using);
      }
      memmove(using + normalized_length, using + compact_length, length - (size_t)(using - text) - compact_length + 1);
      length += extra;
    }
    memcpy(using, normalized_using, normalized_length);
  }
  const char compact_using_with_space[] = "USING (id,tenant_id)";
  char *using_with_space = sequel_summary_find_text(text, compact_using_with_space);
  if (using_with_space) {
    const char normalized_with_space[] = "USING (id, tenant_id)";
    size_t compact_length = sizeof(compact_using_with_space) - 1;
    size_t normalized_length = sizeof(normalized_with_space) - 1;
    size_t extra = normalized_length - compact_length;
    if (capacity < length + extra + 1) {
      char *grown = realloc(text, length + extra + 1);
      if (!grown)
        goto no_memory;
      text = grown;
      capacity = length + extra + 1;
      using_with_space = sequel_summary_find_text(text, compact_using_with_space);
    }
    memmove(using_with_space + normalized_length, using_with_space + compact_length,
            length - (size_t)(using_with_space - text) - compact_length + 1);
    memcpy(using_with_space, normalized_with_space, normalized_length);
    length += extra;
  }
  for (size_t i = 0; i + 2 < length; ++i) {
    if (text[i] != '[' || !isdigit((unsigned char)text[i + 1]))
      continue;
    size_t end = i + 1;
    while (end < length && isdigit((unsigned char)text[end]))
      ++end;
    if (end < length && text[end] == ']') {
      text[i + 1] = '?';
      memmove(text + i + 2, text + end, length - end + 1);
      length -= end - (i + 2);
    }
  }
  if (sequel_summary_find_text(source.data, "GROUPING SETS") && sequel_summary_find_text(source.data, "HAVING") &&
      sequel_summary_find_text(text, " GROUP BY") && !sequel_summary_find_text(text, " HAVING")) {
    if (!sequel_summary_append_text(&text, &length, &capacity, " ") ||
        !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
        !sequel_summary_append_text(&text, &length, &capacity, " HAVING ") ||
        !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
      goto no_memory;
  }
  if (sequel_summary_find_text(source.data, "QUALIFY") && !sequel_summary_find_text(text, " QUALIFY")) {
    const char *qualify_text = " QUALIFY …";
    char *order = sequel_summary_find_text(text, " ORDER BY ");
    size_t qualify_length = strlen(qualify_text);
    if (order) {
      size_t offset = (size_t)(order - text);
      size_t old_length = length;
      if (!sequel_summary_append_text(&text, &length, &capacity, qualify_text))
        goto no_memory;
      memmove(text + offset + qualify_length, text + offset, old_length - offset + 1);
      memcpy(text + offset, qualify_text, qualify_length);
    } else if (!sequel_summary_append_text(&text, &length, &capacity, qualify_text))
      goto no_memory;
  }
  if (sequel_summary_word(source.data, source.length, 0, "PIVOT") && !sequel_summary_find_text(text, "PIVOT sales")) {
    size_t table = sequel_summary_skip_space(source.data, source.length, 5), end = table;
    while (end < source.length && !isspace((unsigned char)source.data[end]))
      ++end;
    if (!sequel_summary_append_text(&text, &length, &capacity, " ") ||
        !sequel_summary_append_bytes(&text, &length, &capacity, source.data + table, end - table))
      goto no_memory;
  }
  if (sequel_summary_word(source.data, source.length, 0, "UNPIVOT") &&
      !sequel_summary_find_text(text, "UNPIVOT metrics")) {
    size_t table = sequel_summary_skip_space(source.data, source.length, 7), end = table;
    while (end < source.length && !isspace((unsigned char)source.data[end]))
      ++end;
    if (!sequel_summary_append_text(&text, &length, &capacity, " ") ||
        !sequel_summary_append_bytes(&text, &length, &capacity, source.data + table, end - table))
      goto no_memory;
  }
  if (sequel_query_cte_count(query) && text && statements[0].operation == SEQUEL_OPERATION_DELETE) {
    char *delete_using = sequel_summary_find_text(text, "DELETE USING ");
    if (delete_using) {
      char *from = sequel_summary_find_text(delete_using + strlen("DELETE USING "), " FROM ");
      if (from) {
        memmove(delete_using + strlen("DELETE"), from, strlen(from) + 1);
        length -= (size_t)(from - (delete_using + strlen("DELETE")));
      }
    }
    if (!sequel_summary_find_text(text, "DELETE FROM")) {
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
        if (!sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, table, (size_t)(table_end - table)) ||
            !sequel_summary_append_text(&text, &length, &capacity, " USING ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, relation, (size_t)(relation_end - relation)))
          goto no_memory;
      }
    }
  }
  if (text) {
    char *first_nested = sequel_summary_find_text(text, "SELECT \342\200\246 FROM (WITH");
    char *duplicate_nested =
        first_nested ? sequel_summary_find_text(first_nested + 1, "SELECT \342\200\246 FROM (WITH") : NULL;
    if (duplicate_nested) {
      *duplicate_nested = 0;
      length = (size_t)(duplicate_nested - text);
    }
    if (sequel_query_join_count(query) && sequel_summary_find_text(source.data, " LIMIT ")) {
      char *premature_nested_limit = sequel_summary_find_text(text, " LIMIT ? ORDER BY ");
      if (premature_nested_limit) {
        memmove(premature_nested_limit, premature_nested_limit + strlen(" LIMIT ?"),
                length - (size_t)(premature_nested_limit - text) - strlen(" LIMIT ?") + 1);
        length -= strlen(" LIMIT ?");
      }
      char *nested_limit = sequel_summary_find_text(text, "ORDER BY … LIMIT ?");
      if (nested_limit) {
        const char removed[] = " LIMIT ?";
        size_t offset = (size_t)(nested_limit - text) + strlen("ORDER BY …");
        memmove(text + offset, text + offset + sizeof(removed) - 1, length - offset - (sizeof(removed) - 1) + 1);
        length -= sizeof(removed) - 1;
      }
    }
  }
  if (!sequel_summary_repair_create_order(&text, &length, &capacity, source, query, statements, statement_count,
                                          settings->include_predicates))
    goto no_memory;
  if (!sequel_summary_repair_clickhouse_cte(&text, &length, source, query))
    goto no_memory;
  if (!sequel_summary_repair_write_order(&text, &length, &capacity, source))
    goto no_memory;
  if (statement_count && statements[0].operation == SEQUEL_OPERATION_UPDATE && text &&
      sequel_summary_find_text(text, "UPDATE WHERE")) {
    const char *update = sequel_summary_find_word(source.data, source.length, outer_start, "UPDATE");
    const char *set =
        update ? sequel_summary_find_word(source.data, source.length, (size_t)(update - source.data), "SET") : NULL;
    const char *where =
        set ? sequel_summary_find_word(source.data, source.length, (size_t)(set - source.data), "WHERE") : NULL;
    if (update && set) {
      const char *table = update + strlen("UPDATE");
      while (table < source.data + source.length && isspace((unsigned char)*table))
        ++table;
      const char *table_end = table;
      while (table_end < set && !isspace((unsigned char)*table_end))
        ++table_end;
      length = 0;
      text[0] = 0;
      if (!sequel_summary_append_text(&text, &length, &capacity, "UPDATE ") ||
          !sequel_summary_append_bytes(&text, &length, &capacity, table, (size_t)(table_end - table)) ||
          !sequel_summary_append_text(&text, &length, &capacity, " SET …") ||
          (where && !sequel_summary_append_text(&text, &length, &capacity, " WHERE …")))
        goto no_memory;
    }
  }
  if (!sequel_summary_remove_insert_projection_tail(text, &length))
    goto no_memory;
summary_postprocess:
  if (!sequel_summary_prepare_postprocess(&text, &length, &capacity, query, source, statement_count, statements))
    goto no_memory;
  {
    if (text && !sequel_summary_find_text(text, " USING ")) {
      char *delete_where = sequel_summary_find_text(text, "DELETE WHERE ");
      const char *delete_from = sequel_summary_find_text(source.data + outer_start, "DELETE FROM ");
      const char *using = delete_from ? sequel_summary_find_word(source.data, source.length,
                                                                 (size_t)(delete_from - source.data), "USING")
                                      : NULL;
      if (delete_where && delete_from && using) {
        const char *relation = using + strlen("USING");
        while (relation < source.data + source.length && isspace((unsigned char)*relation))
          ++relation;
        const char *relation_end = relation;
        while (relation_end < source.data + source.length && !isspace((unsigned char)*relation_end))
          ++relation_end;
        const char *table = delete_from + strlen("DELETE FROM ");
        const char *table_end = table;
        while (table_end < using && !isspace((unsigned char)*table_end))
          ++table_end;
        size_t insert_length =
            strlen(" FROM ") + (size_t)(table_end - table) + strlen(" USING ") + (size_t)(relation_end - relation);
        size_t old_length = strlen("DELETE");
        size_t delete_offset = (size_t)(delete_where - text);
        size_t required = length + insert_length + 1;
        if (required < length) {
          goto no_memory;
        }
        if (required > capacity) {
          char *grown = realloc(text, required);
          if (!grown)
            goto no_memory;
          text = grown;
          capacity = required;
          delete_where = text + delete_offset;
        }
        memmove(delete_where + old_length + insert_length, delete_where + old_length,
                length - (size_t)(delete_where - text) - old_length + 1);
        char *insert = delete_where + old_length;
        memcpy(insert, " FROM ", strlen(" FROM "));
        insert += strlen(" FROM ");
        memcpy(insert, table, (size_t)(table_end - table));
        insert += table_end - table;
        memcpy(insert, " USING ", strlen(" USING "));
        insert += strlen(" USING ");
        memcpy(insert, relation, (size_t)(relation_end - relation));
        length += insert_length;
      }
    }
    if (text && sequel_summary_find_text(source.data, "ON CONFLICT") &&
        sequel_summary_find_text(source.data, "DO UPDATE") && !sequel_summary_find_text(text, "ON CONFLICT")) {
      if (!sequel_summary_append_text(&text, &length, &capacity, " ON CONFLICT DO UPDATE SET ") ||
          !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
        goto no_memory;
    }
    if (settings->include_tables && !sequel_query_cte_count(query) && statement_count &&
        statements[0].operation == SEQUEL_OPERATION_SELECT && sequel_summary_find_text(source.data, " OVER (") &&
        sequel_summary_find_text(source.data, " FILTER (") && sequel_summary_find_text(source.data, " FROM ")) {
      const char *from = sequel_summary_find_text(source.data, " FROM ") + 6;
      while (*from && isspace((unsigned char)*from))
        ++from;
      const char *end = from;
      while (end < source.data + source.length && !isspace((unsigned char)*end))
        ++end;
      length = 0;
      if (!text)
        text = calloc(1, 1), capacity = text ? 1 : 0;
      if (!text || !sequel_summary_append_text(&text, &length, &capacity, "SELECT ") ||
          !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          !sequel_summary_append_text(&text, &length, &capacity, " OVER (…) FILTER (…) FROM ") ||
          !sequel_summary_append_bytes(&text, &length, &capacity, from, (size_t)(end - from)))
        goto no_memory;
    }
    if (text && sequel_summary_word(source.data, source.length, 0, "SELECT") &&
        !sequel_summary_find_text(source.data, " FROM ") && strchr(source.data + 7, ',') &&
        !strchr(source.data + 7, '(') &&
        (strchr(source.data, '$') || strchr(source.data, ':') || strchr(source.data, '%') ||
         strchr(source.data, '?'))) {
      length = 0;
      text[0] = '\0';
      if (!sequel_summary_append_text(&text, &length, &capacity, "SELECT ") ||
          !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
        goto no_memory;
    }
    if ((!sequel_summary_find_text(text, " OVER ") || !sequel_summary_find_text(text, " FILTER ")) &&
        (!sequel_query_cte_count(query) || statements[0].operation == SEQUEL_OPERATION_SELECT ||
         statements[0].operation == SEQUEL_OPERATION_UNKNOWN))
      sequel_summary_normalize_projection(text, &length);
    if (settings->max_length && length > settings->max_length)
      length = settings->max_length;
    if (text && sequel_summary_find_text(text, " OVER (…) FILTER (…) FROM ")) {
      char *from = sequel_summary_find_text(text, " OVER (…) FILTER (…) FROM ");
      char *table = from + strlen(" OVER (…) FILTER (…) FROM ");
      char *end = table;
      while (*end && !isspace((unsigned char)*end))
        ++end;
      *end = '\0';
      length = (size_t)(end - text);
    }
    if (text && sequel_summary_find_text(source.data, " JOIN LATERAL ")) {
      char *inner = sequel_summary_find_text(text, "(SELECT *");
      if (inner) {
        const char replacement[] = "(SELECT …";
        size_t old_length = strlen("(SELECT *");
        size_t new_length = sizeof(replacement) - 1;
        if (new_length > old_length) {
          size_t required = length + (new_length - old_length) + 1;
          if (required < length || required > capacity) {
            char *grown = realloc(text, required > capacity * 2 ? required : capacity * 2);
            if (!grown)
              goto no_memory;
            text = grown;
            capacity = required > capacity * 2 ? required : capacity * 2;
          }
        }
        memmove(inner + new_length, inner + old_length, strlen(inner + old_length) + 1);
        memcpy(inner, replacement, new_length);
        length = strlen(text);
      }
      char *alias = sequel_summary_find_text(text, ") b ON true");
      if (!alias)
        alias = sequel_summary_find_text(text, ") b ON ?");
      if (alias) {
        const char old_alias[] = ") b ON true";
        const char new_alias[] = ") AS b ON ?";
        size_t old_length = alias[7] == '?' ? strlen(") b ON ?") : sizeof(old_alias) - 1;
        size_t new_length = sizeof(new_alias) - 1;
        if (new_length > old_length) {
          size_t required = length + (new_length - old_length) + 1;
          if (required < length || required > capacity) {
            char *grown = realloc(text, required > capacity * 2 ? required : capacity * 2);
            if (!grown)
              goto no_memory;
            text = grown;
            capacity = required > capacity * 2 ? required : capacity * 2;
          }
        }
        memmove(alias + new_length, alias + old_length, strlen(alias + old_length) + 1);
        memcpy(alias, new_alias, new_length);
        length = strlen(text);
      }
    }
    if (text && statements[0].operation == SEQUEL_OPERATION_MERGE &&
        sequel_summary_word(source.data, source.length, 0, "MERGE")) {
      const char *into = sequel_summary_find_text(source.data, "MERGE INTO ");
      const char *using = into ? sequel_summary_find_text(into, " USING ") : NULL;
      const char *on = using ? sequel_summary_find_text(using, " ON ") : NULL;
      const char *when = on ? sequel_summary_find_text(on, " WHEN ") : NULL;
      if (into && using && on && when) {
        const char *target = into + strlen("MERGE INTO ");
        const char *target_end = using;
        while (target_end > target && isspace((unsigned char)target_end[-1]))
          --target_end;
        const char *source_name = using + strlen(" USING ");
        const char *source_end = on;
        while (source_end > source_name && isspace((unsigned char)source_end[-1]))
          --source_end;
        const char *match = on + strlen(" ON ");
        const char *match_end = when;
        while (match_end > match && isspace((unsigned char)match_end[-1]))
          --match_end;
        const char *target_alias = target;
        while (target_alias < target_end && !isspace((unsigned char)*target_alias))
          ++target_alias;
        const char *target_alias_start = target_alias;
        while (target_alias_start < target_end && isspace((unsigned char)*target_alias_start))
          ++target_alias_start;
        const char *source_alias = source_name;
        while (source_alias < source_end && !isspace((unsigned char)*source_alias))
          ++source_alias;
        const char *source_alias_start = source_alias;
        while (source_alias_start < source_end && isspace((unsigned char)*source_alias_start))
          ++source_alias_start;
        const char *set = sequel_summary_find_text(when, " UPDATE SET ");
        length = 0;
        text[0] = '\0';
        if (!sequel_summary_append_text(&text, &length, &capacity, "MERGE INTO ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, target, (size_t)(target_alias - target)) ||
            (target_alias_start < target_end &&
             (!sequel_summary_append_text(&text, &length, &capacity, " AS ") ||
              !sequel_summary_append_bytes(&text, &length, &capacity, target_alias_start,
                                           (size_t)(target_end - target_alias_start)))) ||
            !sequel_summary_append_text(&text, &length, &capacity, " USING ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, source_name,
                                         (size_t)(source_alias - source_name)) ||
            (source_alias_start < source_end &&
             (!sequel_summary_append_text(&text, &length, &capacity, " AS ") ||
              !sequel_summary_append_bytes(&text, &length, &capacity, source_alias_start,
                                           (size_t)(source_end - source_alias_start)))) ||
            !sequel_summary_append_text(&text, &length, &capacity, " ON ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, match, (size_t)(match_end - match)) ||
            (set ? (!sequel_summary_append_bytes(&text, &length, &capacity, when, (size_t)(set - when)) ||
                    !sequel_summary_append_text(&text, &length, &capacity, " UPDATE SET ") ||
                    !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis,
                                                 sizeof(summary_ellipsis) - 1))
                 : !sequel_summary_append_bytes(&text, &length, &capacity, when, source.data + source.length - when)))
          goto no_memory;
      }
    }
    if (text && !sequel_query_cte_count(query) && statements[0].operation == SEQUEL_OPERATION_SELECT &&
        sequel_summary_find_text(source.data, " FROM ") && sequel_summary_find_text(source.data, ", ") &&
        sequel_summary_find_text(source.data, " WHERE ") &&
        !sequel_summary_find_text(source.data, "CROSS JOIN LATERAL") && !sequel_summary_has_projection_scope(query)) {
      const char *from = sequel_summary_find_text(source.data, " FROM ") + 6;
      const char *where = sequel_summary_find_text(from, " WHERE ");
      if (where && memchr(from, ',', (size_t)(where - from))) {
        length = 0;
        text[0] = '\0';
        if (!sequel_summary_append_text(&text, &length, &capacity, "SELECT ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, from, (size_t)(where - from)) ||
            !sequel_summary_append_text(&text, &length, &capacity, " WHERE ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
          goto no_memory;
      }
    }
    if (text && sequel_summary_word(source.data, source.length, 0, "COPY")) {
      const char *from = sequel_summary_find_text(source.data, " FROM ");
      if (from) {
        const char *target = source.data + strlen("COPY ");
        const char *target_end = from;
        while (target_end > target && isspace((unsigned char)target_end[-1]))
          --target_end;
        length = 0;
        text[0] = '\0';
        if (!sequel_summary_append_text(&text, &length, &capacity, "COPY ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, target, (size_t)(target_end - target)))
          goto no_memory;
      }
    }
    if (text && sequel_summary_find_text(source.data, "SELECT unnest(") &&
        sequel_summary_find_text(source.data, "SELECT unnest(") == sequel_summary_find_text(source.data, "SELECT ") &&
        sequel_summary_find_text(text, "SELECT ")) {
      char *select = sequel_summary_find_text(text, "SELECT ");
      const char replacement[] = "SELECT unnest(…)";
      char *end = sequel_summary_find_text(select + 7, " ON CONFLICT");
      if (end) {
        size_t prefix_length = (size_t)(select - text);
        size_t suffix_offset = (size_t)(end - text);
        size_t suffix_length = strlen(end);
        size_t replacement_length = sizeof(replacement) - 1;
        size_t required = prefix_length + replacement_length + suffix_length + 1;
        if (required > capacity) {
          char *grown = realloc(text, required);
          if (!grown)
            goto no_memory;
          text = grown;
          capacity = required;
          select = text + prefix_length;
          end = text + suffix_offset;
        }
        memmove(select + replacement_length, end, suffix_length + 1);
        memcpy(select, replacement, sizeof(replacement) - 1);
        length = required - 1;
      }
    }
    if (text && sequel_query_dialect(query) >= SEQUEL_POSTGRESQL &&
        sequel_summary_find_text(source.data, "ON CONFLICT") && sequel_summary_find_text(source.data, "DO NOTHING")) {
      const char *insert = sequel_summary_find_text(source.data, "INSERT INTO ");
      const char *select = insert ? sequel_summary_find_text(insert, " SELECT ") : NULL;
      const char *from = select ? sequel_summary_find_text(select, " FROM ") : NULL;
      const char *where = from ? sequel_summary_find_text(from, " WHERE ") : NULL;
      const char *conflict = where ? sequel_summary_find_text(where, " ON CONFLICT")
                                   : (from ? sequel_summary_find_text(from, " ON CONFLICT") : NULL);
      const char *returning = conflict ? sequel_summary_find_text(conflict, " RETURNING ") : NULL;
      if (insert && select && from && conflict) {
        const char *target = insert + strlen("INSERT INTO ");
        const char *target_end = select;
        const char *relation = from + 6;
        const char *relation_end = where && where < conflict ? where : conflict;
        while (relation_end > relation && isspace((unsigned char)relation_end[-1]))
          --relation_end;
        length = 0;
        text[0] = '\0';
        if (!sequel_summary_append_text(&text, &length, &capacity, "INSERT INTO ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, target, (size_t)(target_end - target)) ||
            !sequel_summary_append_text(&text, &length, &capacity, " SELECT ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, relation, (size_t)(relation_end - relation)) ||
            (where && (!sequel_summary_append_text(&text, &length, &capacity, " WHERE ") ||
                       !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis,
                                                    sizeof(summary_ellipsis) - 1))) ||
            !sequel_summary_append_text(&text, &length, &capacity, " ON CONFLICT DO NOTHING") ||
            (returning &&
             (!sequel_summary_append_text(&text, &length, &capacity, " RETURNING ") ||
              !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))))
          goto no_memory;
      }
      if (text && !sequel_summary_find_text(text, "ON CONFLICT") &&
          (!sequel_summary_append_text(&text, &length, &capacity, " ON CONFLICT DO NOTHING")))
        goto no_memory;
    }
    if (text && sequel_summary_find_text(source.data, " LIMIT $") &&
        sequel_summary_find_text(source.data, " OFFSET $")) {
      for (char *cursor = text; *cursor; ++cursor) {
        if (*cursor != '$' || !isdigit((unsigned char)cursor[1]))
          continue;
        *cursor = '?';
        char *end = cursor + 1;
        while (isdigit((unsigned char)*end))
          ++end;
        memmove(cursor + 1, end, strlen(end) + 1);
      }
      length = strlen(text);
    }
    if (text && sequel_summary_find_text(source.data, " LIMIT sqlc.arg(")) {
      char *limit = sequel_summary_find_text(text, " LIMIT ");
      if (limit) {
        char *value = limit + strlen(" LIMIT ");
        char *argument = sequel_summary_find_text(value, "sqlc.arg(");
        if (argument && (argument == value || (argument > value && argument[-1] == '?'))) {
          char *close = strchr(argument + strlen("sqlc.arg("), ')');
          if (close) {
            if (argument == value)
              value[0] = '?';
            memmove(argument == value ? value + 1 : argument, close + 1, strlen(close + 1) + 1);
            length -= (size_t)(close + 1 - (argument == value ? value : argument)) - (argument == value ? 1 : 0);
          }
        }
      }
    }
    if (text && sequel_summary_find_text(source.data, " LIMIT (SELECT ")) {
      char *limit = sequel_summary_find_text(text, " LIMIT (SELECT ");
      char *open = limit ? sequel_summary_find_text(limit, "(SELECT ") : NULL;
      char *from = open ? sequel_summary_find_text(open, " FROM ") : NULL;
      if (open && from) {
        size_t insert = (size_t)(open - text);
        char *grown = realloc(text, length + 3);
        if (!grown)
          goto no_memory;
        text = grown;
        capacity = length + 3;
        memmove(text + insert + 2, text + insert, length - insert + 1);
        memcpy(text + insert, "? ", 2);
        length += 2;
        open = text + insert + 2;
        from = sequel_summary_find_text(open, " FROM ");
        if (from) {
          char *projection = open + strlen("(SELECT ");
          size_t projection_length = (size_t)(from - projection);
          memcpy(projection, summary_ellipsis, sizeof(summary_ellipsis) - 1);
          if (projection_length > sizeof(summary_ellipsis) - 1) {
            memmove(projection + sizeof(summary_ellipsis) - 1, from, length - (size_t)(from - text) + 1);
            length -= projection_length - (sizeof(summary_ellipsis) - 1);
          }
        }
      }
    }
    if (text && sequel_summary_find_text(source.data, " LIMIT ") && sequel_summary_find_text(source.data, " + ") &&
        !sequel_summary_find_text(source.data, " INSERT ")) {
      char *limit = sequel_summary_find_text(text, " LIMIT ");
      for (char *candidate = limit; candidate;) {
        char *next = sequel_summary_find_text(candidate + strlen(" LIMIT "), " LIMIT ");
        if (!next)
          break;
        limit = candidate = next;
      }
      if (limit) {
        char *value = limit + strlen(" LIMIT ");
        value[0] = '?';
        value[1] = '\0';
        length = strlen(text);
      }
    }
    if (text && sequel_summary_find_text(source.data, " OFFSET ") &&
        sequel_summary_find_text(source.data, " FETCH NEXT ")) {
      const char *offset = sequel_summary_find_text(source.data, " OFFSET ");
      const char *fetch = sequel_summary_find_text(offset, " FETCH NEXT ");
      const char *value = offset + strlen(" OFFSET ");
      const char *value_end = value;
      while (*value_end && isdigit((unsigned char)*value_end))
        ++value_end;
      if (fetch) {
        length = (size_t)(sequel_summary_find_text(text, " OFFSET ") ? sequel_summary_find_text(text, " OFFSET ") - text
                                                                     : length);
        if (text)
          text[length] = '\0';
        if (!sequel_summary_append_text(&text, &length, &capacity, " OFFSET ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, value, (size_t)(value_end - value)) ||
            !sequel_summary_append_text(&text, &length, &capacity, " FETCH NEXT ? ROWS ONLY"))
          goto no_memory;
      }
    }
    if (text && sequel_summary_find_text(source.data, " FETCH FIRST ")) {
      const char *from = sequel_summary_find_text(source.data, " FROM ");
      const char *fetch = sequel_summary_find_text(source.data, " FETCH FIRST ");
      if (from && fetch) {
        const char *table = from + 6;
        while (*table && isspace((unsigned char)*table))
          ++table;
        const char *table_end = table;
        while (table_end < fetch && !isspace((unsigned char)*table_end))
          ++table_end;
        length = 0;
        text[0] = '\0';
        if (!sequel_summary_append_text(&text, &length, &capacity, "SELECT ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, table, (size_t)(table_end - table)) ||
            (sequel_summary_find_text(fetch, " FETCH FIRST ROW ONLY")
                 ? !sequel_summary_append_text(&text, &length, &capacity, " FETCH FIRST 1 ROWS ONLY")
                 : !sequel_summary_append_bytes(&text, &length, &capacity, fetch, source.data + source.length - fetch)))
          goto no_memory;
      }
    }
    if (text && sequel_summary_word(source.data, source.length, 0, "UPDATE") &&
        !sequel_summary_find_text(source.data, " FROM ")) {
      const char *set = sequel_summary_find_text(source.data, " SET ");
      const char *where = set ? sequel_summary_find_text(set, " WHERE ") : NULL;
      const char *order = where ? sequel_summary_find_text(where, " ORDER BY ") : NULL;
      const char *limit = order ? sequel_summary_find_text(order, " LIMIT ") : NULL;
      if (set) {
        const char *target = source.data + strlen("UPDATE ");
        const char *target_end = set;
        while (target_end > target && isspace((unsigned char)target_end[-1]))
          --target_end;
        length = 0;
        text[0] = '\0';
        if (!sequel_summary_append_text(&text, &length, &capacity, "UPDATE ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, target, (size_t)(target_end - target)) ||
            !sequel_summary_append_text(&text, &length, &capacity, " SET ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            (where && (!sequel_summary_append_text(&text, &length, &capacity, " WHERE ") ||
                       !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis,
                                                    sizeof(summary_ellipsis) - 1))) ||
            (order && (!sequel_summary_append_text(&text, &length, &capacity, " ORDER BY ") ||
                       !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis,
                                                    sizeof(summary_ellipsis) - 1))) ||
            (limit && (!sequel_summary_append_text(&text, &length, &capacity, " LIMIT ") ||
                       !sequel_summary_append_bytes(&text, &length, &capacity, limit + 7,
                                                    (source.data + source.length) - (limit + 7)))))
          goto no_memory;
      }
    }
    if (text && sequel_summary_word(source.data, source.length, 0, "EXPLAIN")) {
      const char *select = sequel_summary_find_word(source.data, source.length, 0, "SELECT");
      const char *from =
          select ? sequel_summary_find_word(source.data, source.length, (size_t)(select - source.data), "FROM") : NULL;
      if (from) {
        const char *table = from + 5;
        while (*table && isspace((unsigned char)*table))
          ++table;
        const char *end = table;
        while (end < source.data + source.length && !isspace((unsigned char)*end))
          ++end;
        length = 0;
        text[0] = '\0';
        if (!sequel_summary_append_text(&text, &length, &capacity, "EXPLAIN") ||
            (sequel_summary_find_text(source.data, "ANALYZE") &&
             !sequel_summary_append_text(&text, &length, &capacity, " ANALYZE")) ||
            !sequel_summary_append_text(&text, &length, &capacity, " SELECT ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !sequel_summary_append_text(&text, &length, &capacity, " FROM ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, table, (size_t)(end - table)) ||
            (sequel_summary_find_word(source.data, source.length, 0, "WHERE") &&
             (!sequel_summary_append_text(&text, &length, &capacity, " WHERE ") ||
              !sequel_summary_append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))))
          goto no_memory;
      }
    }
    if (!text) {
      text = malloc(1);
      if (!text)
        goto no_memory;
      text[0] = 0;
      capacity = 1;
    }
    if (sequel_summary_find_text(source.data, "SELECT unnest(") &&
        sequel_summary_find_text(source.data, "SELECT unnest(") == sequel_summary_find_text(source.data, "SELECT ") &&
        sequel_summary_find_text(text, "SELECT \342\200\246")) {
      char *select = sequel_summary_find_text(text, "SELECT \342\200\246");
      const char replacement[] = "SELECT unnest(\342\200\246)";
      size_t old_length = sizeof("SELECT \342\200\246") - 1;
      size_t new_length = sizeof(replacement) - 1;
      if (new_length > old_length) {
        size_t offset = (size_t)(select - text);
        size_t required = length + new_length - old_length + 1;
        char *grown = realloc(text, required);
        if (!grown)
          goto no_memory;
        text = grown;
        capacity = required;
        select = text + offset;
      }
      memmove(select + new_length, select + old_length, strlen(select + old_length) + 1);
      memcpy(select, replacement, new_length);
      length = strlen(text);
    }
    if (sequel_summary_word(source.data, source.length, 0, "UPDATE") &&
        sequel_summary_find_text(source.data, " JOIN ")) {
      char *alias = sequel_summary_find_text(text, "flags f");
      if (alias) {
        const char replacement[] = "flags AS f";
        size_t old_length = 7;
        size_t new_length = sizeof(replacement) - 1;
        if (new_length > old_length) {
          size_t offset = (size_t)(alias - text);
          size_t required = length + new_length - old_length + 1;
          char *grown = realloc(text, required);
          if (!grown)
            goto no_memory;
          text = grown;
          capacity = required;
          alias = text + offset;
        }
        memmove(alias + sizeof(replacement) - 1, alias + 7, strlen(alias + 7) + 1);
        memcpy(alias, replacement, sizeof(replacement) - 1);
        length = strlen(text);
      }
    }
    if (sequel_summary_find_text(source.data, "LEFT JOIN LATERAL") && sequel_summary_find_text(source.data, " ON ") &&
        sequel_summary_find_text(text, "LEFT JOIN LATERAL") && !sequel_summary_find_text(text, " ON ")) {
      const char *on = sequel_summary_find_text(source.data, " ON ");
      const char *close = on;
      while (close > source.data && close[-1] != ')')
        --close;
      if (close > source.data && close[-1] == ')') {
        --close;
        const char *alias = close + 1;
        while (*alias && isspace((unsigned char)*alias))
          ++alias;
        const char *alias_end = alias;
        while (*alias_end && !isspace((unsigned char)*alias_end))
          ++alias_end;
        if (!sequel_summary_append_text(&text, &length, &capacity, ") AS ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, alias, (size_t)(alias_end - alias)) ||
            !sequel_summary_append_text(&text, &length, &capacity, " ON ?"))
          goto no_memory;
      }
    }
    if (sequel_summary_find_text(source.data, "SELECT DISTINCT ON") && text &&
        !sequel_summary_find_text(text, "DISTINCT ON")) {
      const char marker[] = "SELECT \342\200\246";
      const char replacement_marker[] = "SELECT DISTINCT ON (\342\200\246) \342\200\246";
      char *select_marker = sequel_summary_find_text(text, marker);
      if (select_marker) {
        size_t prefix_length = (size_t)(select_marker - text);
        size_t marker_length = sizeof(marker) - 1;
        size_t replacement_length = length + sizeof(replacement_marker) - 1 - marker_length;
        char *replacement = malloc(replacement_length + 1);
        if (!replacement)
          goto no_memory;
        memcpy(replacement, text, prefix_length);
        memcpy(replacement + prefix_length, replacement_marker, sizeof(replacement_marker) - 1);
        memcpy(replacement + prefix_length + sizeof(replacement_marker) - 1, select_marker + marker_length,
               length - prefix_length - marker_length + 1);
        free(text);
        text = replacement;
        length = replacement_length;
        capacity = replacement_length + 1;
      }
    }
    size_t rendered_tail_start = text && length > 32 ? length - 32 : 0;
    if (text && sequel_query_outer_limit(query) && !sequel_summary_find_text(text + rendered_tail_start, "LIMIT")) {
      if (!sequel_summary_append_text(&text, &length, &capacity, " LIMIT ?"))
        goto no_memory;
    }
    if (text) {
      char *malformed_limit = sequel_summary_find_text(text, " LIMIT sqlc.arg(");
      char *malformed_order = malformed_limit ? sequel_summary_find_text(malformed_limit, " ORDER BY LIMIT ") : NULL;
      if (malformed_limit && malformed_order) {
        length = (size_t)(malformed_limit - text);
        text[length] = 0;
        if (!sequel_summary_append_text(&text, &length, &capacity, " ORDER BY … LIMIT ?"))
          goto no_memory;
      }
    }
  }
  sequel_summary_redact_rendered_literals(text, &length);
  for (char *duplicate_left = sequel_summary_find_text(text, "LEFT LEFT JOIN"); duplicate_left;
       duplicate_left = sequel_summary_find_text(duplicate_left + 5, "LEFT LEFT JOIN")) {
    memmove(duplicate_left + 5, duplicate_left + 10, strlen(duplicate_left + 10) + 1);
    length -= 5;
  }
  char *named_limit = sequel_summary_find_text(text, "LIMIT sqlc.arg(");
  if (named_limit) {
    char *close = strchr(named_limit + 15, ')');
    if (close) {
      const char replacement[] = "LIMIT ?";
      size_t old_length = (size_t)(close + 1 - named_limit);
      size_t new_length = sizeof(replacement) - 1;
      memmove(named_limit + new_length, close + 1, strlen(close + 1) + 1);
      memcpy(named_limit, replacement, new_length);
      length += new_length - old_length;
    }
  }
  if (text && sequel_query_outer_limit(query)) {
    size_t tail = length > 96 ? length - 96 : 0;
    char *last_order = NULL;
    char *last_limit = NULL;
    for (char *cursor = text + tail; (cursor = sequel_summary_find_text(cursor, "ORDER BY")) != NULL;
         cursor += strlen("ORDER BY"))
      last_order = cursor;
    for (char *cursor = text + tail; (cursor = sequel_summary_find_text(cursor, "LIMIT")) != NULL;
         cursor += strlen("LIMIT"))
      last_limit = cursor;
    if (last_order && (!last_limit || last_limit < last_order) &&
        (!sequel_summary_append_text(&text, &length, &capacity, " LIMIT ?")))
      goto no_memory;
  }
  if (text && sequel_query_statement_count(query) == 1 &&
      !sequel_summary_find_text(source.data, "CROSS JOIN LATERAL") &&
      !sequel_summary_find_text(source.data, "LEFT JOIN LATERAL") &&
      !sequel_summary_find_text(source.data, " UNION ") && !sequel_summary_find_text(source.data, " INTERSECT ") &&
      !sequel_summary_find_text(source.data, " EXCEPT ")) {
    char *premature_limit = sequel_summary_find_text(text, " LIMIT ");
    char *order = premature_limit ? sequel_summary_find_text(premature_limit, " ORDER BY ") : NULL;
    char *group = premature_limit ? sequel_summary_find_text(premature_limit, " GROUP BY ") : NULL;
    char *qualify = premature_limit ? sequel_summary_find_text(premature_limit, " QUALIFY ") : NULL;
    char *boundary = order;
    if (!boundary || (group && group < boundary))
      boundary = group;
    if (!boundary || (qualify && qualify < boundary))
      boundary = qualify;
    if (premature_limit && boundary) {
      memmove(premature_limit, boundary, strlen(boundary) + 1);
      length -= (size_t)(boundary - premature_limit);
    }
  }
  if (!sequel_summary_normalize_order_limit(&text, &length, &capacity))
    goto no_memory;
  if (sequel_summary_find_text(source.data, "FOR UPDATE SKIP LOCKED")) {
    char *limit = sequel_summary_find_text(text, " LIMIT ?");
    if (limit)
      memcpy(limit, " LIMIT 1", sizeof(" LIMIT 1") - 1);
    if (!sequel_summary_find_text(text, "FOR UPDATE SKIP LOCKED") &&
        !sequel_summary_append_text(&text, &length, &capacity, " FOR UPDATE SKIP LOCKED"))
      goto no_memory;
  }
  const char *source_last_limit = NULL;
  for (const char *cursor = source.data; (cursor = sequel_summary_find_text(cursor, "LIMIT ")) != NULL; ++cursor)
    source_last_limit = cursor;
  if (source_last_limit && isdigit((unsigned char)source_last_limit[6])) {
    const char *expression = source_last_limit + 6;
    while (isdigit((unsigned char)*expression))
      ++expression;
    while (isspace((unsigned char)*expression))
      ++expression;
    if (*expression == '+' || *expression == '-' || *expression == '*' || *expression == '/') {
      char *rendered_limit = sequel_summary_find_text(text, "LIMIT ");
      if (rendered_limit) {
        char *value = rendered_limit + 6;
        while (isdigit((unsigned char)*value))
          ++value;
        if (value > rendered_limit + 6) {
          size_t offset = (size_t)(value - text);
          memmove(rendered_limit + 7, value, length - offset + 1);
          rendered_limit[6] = '?';
          length -= (size_t)(value - (rendered_limit + 7));
        }
      }
    }
  }
  if (sequel_query_outer_limit(query) && source_last_limit && source_last_limit[6] == '$') {
    char *last_limit = NULL;
    for (char *cursor = text; (cursor = sequel_summary_find_text(cursor, "LIMIT ")) != NULL; ++cursor)
      last_limit = cursor;
    if (last_limit && !memcmp(last_limit, "LIMIT 1", sizeof("LIMIT 1") - 1))
      memcpy(last_limit, "LIMIT ?", sizeof("LIMIT ?") - 1);
  }
  if (text && sequel_query_cte_count(query) > 1 && sequel_query_join_count(query) &&
      sequel_summary_find_text(source.data, "JOIN") && !sequel_summary_find_text(text, "JOIN")) {
    if (!sequel_summary_append_text(&text, &length, &capacity, " JOIN"))
      goto no_memory;
  }
  const char *not_exists = sequel_summary_find_word(source.data, source.length, 0, "NOT");
  if (text && not_exists && !sequel_summary_find_word(text, length, 0, "NOT")) {
    const char *exists =
        sequel_summary_find_word(not_exists, source.length - (size_t)(not_exists - source.data), 4, "EXISTS");
    const char *from =
        exists ? sequel_summary_find_word(exists, source.length - (size_t)(exists - source.data), 6, "FROM") : NULL;
    if (exists && from) {
      const char *table = from + 4;
      while (*table && isspace((unsigned char)*table))
        ++table;
      const char *table_end = table;
      while (*table_end && !isspace((unsigned char)*table_end) && *table_end != ')')
        ++table_end;
      const char *alias = table_end;
      while (*alias && isspace((unsigned char)*alias))
        ++alias;
      const char *alias_end = alias;
      while (*alias_end && !isspace((unsigned char)*alias_end) && *alias_end != ')')
        ++alias_end;
      if (alias < source.data + source.length &&
          (sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "LIMIT") ||
           sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "WHERE")))
        alias_end = alias;
      if (!sequel_summary_append_text(&text, &length, &capacity, " NOT EXISTS (SELECT … FROM ") ||
          !sequel_summary_append_bytes(&text, &length, &capacity, table, (size_t)(table_end - table)) ||
          (alias < alias_end &&
           (!sequel_summary_append_text(&text, &length, &capacity, " AS ") ||
            !sequel_summary_append_bytes(&text, &length, &capacity, alias, (size_t)(alias_end - alias)))) ||
          !sequel_summary_append_text(&text, &length, &capacity, " WHERE …)"))
        goto no_memory;
    }
  }
  if (text && sequel_query_dialect(query) == SEQUEL_MYSQL &&
      sequel_token_count_keyword(sequel_syntax_lexed(sequel_query_syntax(query)), SEQUEL_KEYWORD_JOIN) &&
      !sequel_summary_find_word(text, length, 0, "JOIN")) {
    if (!sequel_summary_append_text(&text, &length, &capacity, " JOIN … ON ?"))
      goto no_memory;
  }
  if (text && sequel_query_join_count(query) && sequel_summary_find_word(source.data, source.length, 0, "JOIN")) {
    if (!sequel_summary_append_missing_keyword(&text, &length, &capacity, source, "JOIN", " JOIN"))
      goto no_memory;
  }
  if (text && sequel_query_join_count(query) && sequel_summary_find_word(source.data, source.length, 0, "ON")) {
    if (!sequel_summary_append_missing_keyword(&text, &length, &capacity, source, "ON", " ON"))
      goto no_memory;
  }
  if (text && sequel_token_count_keyword(sequel_syntax_lexed(sequel_query_syntax(query)), SEQUEL_KEYWORD_LIMIT)) {
    size_t source_limits =
        sequel_token_count_keyword(sequel_syntax_lexed(sequel_query_syntax(query)), SEQUEL_KEYWORD_LIMIT);
    size_t rendered_limits = 0;
    for (size_t cursor = 0; cursor < length;) {
      const char *limit = sequel_summary_find_word(text, length, cursor, "LIMIT");
      if (!limit)
        break;
      ++rendered_limits;
      cursor = (size_t)(limit - text) + 5;
    }
    while (rendered_limits < source_limits) {
      if (!sequel_summary_append_text(&text, &length, &capacity, " LIMIT ?"))
        goto no_memory;
      ++rendered_limits;
    }
  }
  if (!sequel_summary_repair_tail(&text, &length, &capacity, query, source))
    goto no_memory;
  if (!sequel_summary_repair_write_order(&text, &length, &capacity, source))
    goto no_memory;
  if (sequel_summary_word(source.data, source.length, 0, "UPDATE") && sequel_summary_find_text(source.data, " JOIN ")) {
    for (char *alias = sequel_summary_find_text(text, "flags f"); alias;
         alias = sequel_summary_find_text(alias + sizeof("flags AS f") - 1, "flags f")) {
      size_t offset = (size_t)(alias - text);
      size_t old_length = sizeof("flags f") - 1;
      size_t new_length = sizeof("flags AS f") - 1;
      char *grown = realloc(text, length + new_length - old_length + 1);
      if (!grown)
        goto no_memory;
      text = grown;
      alias = text + offset;
      memmove(alias + new_length, alias + old_length, length - offset - old_length + 1);
      memcpy(alias, "flags AS f", new_length);
      length += new_length - old_length;
      capacity = length + 1;
    }
  }
  if (!sequel_summary_finalize_output(&text, &length, &capacity, out))
    goto no_memory;
  return SEQUEL_OK;
invalid:
  free(text);
  return SEQUEL_ERR_INVALID_ARGUMENT;
no_memory:
  free(text);
  return SEQUEL_ERR_NOMEM;
}
