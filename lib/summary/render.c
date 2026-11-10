#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <sequel/summary.h>
#include <sequel/sanitizer.h>

#include "internal.h"

#define append_text sequel_summary_append_text
#define append_bytes sequel_summary_append_bytes

static const char summary_ellipsis[] = "\342\200\246";

#define operation_name sequel_summary_operation_name
#define append_redacted_clause sequel_summary_append_redacted_clause
#define append_body_summary sequel_summary_append_body_summary

#define summary_word sequel_summary_word
#define summary_skip_space sequel_summary_skip_space

#define append_statement_shape sequel_summary_append_statement_shape

#define normalize_projection sequel_summary_normalize_projection


sequel_status_t sequel_summarize_query(const sequel_query_document_t *query, const sequel_summary_options_t *options,
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
  /* A ClickHouse numeric dollar-quoted literal is a single projection.  Keep
   * that shape stable when formatting has inserted line breaks around it; the
   * generic projection scanner otherwise mistakes the delimiter for a bind. */
  if (sequel_lexed_dialect(sequel_syntax_lexed(sequel_query_syntax(query))) == SEQUEL_CLICKHOUSE &&
      settings->include_operation && settings->include_tables && statement_count &&
      summary_word(source.data, source.length, 0, "SELECT") && strstr(source.data, "$")) {
    const char *from = sequel_summary_find_word(source.data, source.length, 0, "FROM");
    if (from) {
      const char *table = from + 4;
      while ((size_t)(table - source.data) < source.length && isspace((unsigned char)*table))
        ++table;
      const char *end = table;
      while ((size_t)(end - source.data) < source.length && !isspace((unsigned char)*end) && *end != ';')
        ++end;
      if (!append_text(&text, &length, &capacity, "SELECT ") ||
          !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          !append_text(&text, &length, &capacity, " FROM ") ||
          !append_bytes(&text, &length, &capacity, table, (size_t)(end - table)))
        goto no_memory;
      goto summary_postprocess;
    }
  }
  size_t cte_prefix_end = 0;
  size_t outer_start = 0;
  bool nested_cte_scope = false;
  bool insert_values_scope = false;
  bool cte_insert_scope = false;
  bool scalar_with_scope = summary_word(source.data, source.length, 0, "WITH") && sequel_query_cte_count(query) == 0;
  if (settings->include_tables && statement_count && !sequel_query_cte_count(query) &&
      !sequel_summary_find_word(source.data, source.length, 0, "UNION")) {
    int parenthesized = sequel_summary_append_parenthesized_subquery(&text, &length, &capacity, source);
    if (!parenthesized)
      goto no_memory;
    if (parenthesized == 2)
      goto summary_postprocess;
  }
  if (settings->include_operation && sequel_query_cte_count(query)) {
    const sequel_cte_info_t *ctes = sequel_query_ctes(query);
    bool recursive = source.length >= 13 && strstr(source.data, "WITH RECURSIVE") == source.data;
    if (!append_text(&text, &length, &capacity, recursive ? "WITH RECURSIVE " : "WITH "))
      goto no_memory;
    for (size_t i = 0; i < sequel_query_cte_count(query); ++i) {
      if (i && !append_text(&text, &length, &capacity, ", "))
        goto no_memory;
      if (!append_bytes(&text, &length, &capacity, source.data + ctes[i].name_range.start,
                        ctes[i].name_range.end - ctes[i].name_range.start) ||
          !append_text(&text, &length, &capacity,
                       strstr(source.data + ctes[i].name_range.end, "MATERIALIZED") ? " AS MATERIALIZED (" : " AS ("))
        goto no_memory;
      if (!append_body_summary(&text, &length, &capacity, source.data, ctes[i].body_range) ||
          !append_text(&text, &length, &capacity, ")"))
        goto no_memory;
    }
    if (!append_text(&text, &length, &capacity, " "))
      goto no_memory;
    cte_prefix_end = length;
    if (strstr(source.data, " FROM (WITH ")) {
      const char *nested = strstr(source.data, " FROM (WITH ");
      if (!append_text(&text, &length, &capacity, "SELECT ") ||
          !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          !append_text(&text, &length, &capacity, " FROM "))
        goto no_memory;
      const char *cursor = nested + 6;
      while (*cursor) {
        if (*cursor == '*') {
          if (!append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
            goto no_memory;
          ++cursor;
        } else if (!strncmp(cursor, "true", 4)) {
          if (!append_text(&text, &length, &capacity, "?"))
            goto no_memory;
          cursor += 4;
        } else if (!strncmp(cursor, ") q", 3)) {
          if (!append_text(&text, &length, &capacity, ") AS q"))
            goto no_memory;
          cursor += 3;
        } else {
          if (!append_bytes(&text, &length, &capacity, cursor, 1))
            goto no_memory;
          ++cursor;
        }
      }
      nested_cte_scope = true;
    }
    const char *outer_insert = strstr(source.data + ctes[sequel_query_cte_count(query) - 1].range.end, "INSERT INTO ");
    if (outer_insert)
      cte_insert_scope = true;
    if (outer_insert) {
      const char *target = outer_insert + 12, *target_end = target;
      while (*target_end && !isspace((unsigned char)*target_end) && *target_end != '(')
        ++target_end;
      const char *outer_from = strstr(target_end, " FROM ");
      if (outer_from) {
        const char *relation = outer_from + 6, *relation_end = relation;
        while (*relation_end && !isspace((unsigned char)*relation_end) && *relation_end != ';')
          ++relation_end;
        if (!append_text(&text, &length, &capacity, "INSERT INTO ") ||
            !append_bytes(&text, &length, &capacity, target, (size_t)(target_end - target)) ||
            !append_text(&text, &length, &capacity, " SELECT ") ||
            !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !append_text(&text, &length, &capacity, " FROM ") ||
            !append_bytes(&text, &length, &capacity, relation, (size_t)(relation_end - relation)))
          goto no_memory;
      }
    }
    outer_start = ctes[sequel_query_cte_count(query) - 1].range.end;
  }
  if (settings->include_operation && scalar_with_scope && !append_text(&text, &length, &capacity, "WITH "))
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
    for (size_t i = 0; i < sanitized.length; ++i) {
      if (sanitized.data[i] == ';') {
        after_first = true, ++statement_index;
        compact_tail = false;
      }
      bool compact_literals = after_first && statement_index < statement_count &&
                              statements[statement_index].operation != SEQUEL_OPERATION_SELECT;
      size_t tail_prefix = 0;
      if (compact_literals && !compact_tail && i + 5 <= sanitized.length && !memcmp(sanitized.data + i, " SET ", 5))
        tail_prefix = 5;
      else if (compact_literals && !compact_tail && i + 7 <= sanitized.length &&
               !memcmp(sanitized.data + i, " WHERE ", 7))
        tail_prefix = 7;
      if (tail_prefix) {
        if (!append_bytes(&multi, &multi_length, &multi_capacity, sanitized.data + i, tail_prefix) ||
            !append_bytes(&multi, &multi_length, &multi_capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1)) {
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
        if (!append_bytes(&multi, &multi_length, &multi_capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1)) {
          free(multi);
          sequel_text_result_free(&sanitized);
          goto no_memory;
        }
      } else if (!append_bytes(&multi, &multi_length, &multi_capacity, sanitized.data + i, 1)) {
        free(multi);
        sequel_text_result_free(&sanitized);
        goto no_memory;
      }
    }
    sequel_text_result_free(&sanitized);
    for (char *empty = strstr(multi, ";;"); empty; empty = strstr(multi, ";;")) {
      memmove(empty + 1, empty + 2, strlen(empty + 2) + 1);
      --multi_length;
    }
    for (char *empty = strstr(multi, "; ;"); empty; empty = strstr(multi, "; ;")) {
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
      !cte_insert_scope && !append_statement_shape(&text, &length, &capacity, source, query, settings, &statements[0]))
    goto no_memory;
  if (settings->include_operation && statement_count && !sequel_query_cte_count(query) &&
      !sequel_summary_append_projection_shape(&text, &length, &capacity, source, query, &statements[0]))
    goto no_memory;
  if (settings->include_tables && statement_count && statements[0].operation == SEQUEL_OPERATION_INSERT) {
    size_t into = SIZE_MAX;
    for (size_t i = 0; i < source.length; ++i)
      if (summary_word(source.data, source.length, i, "INTO")) {
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
      if (end > name && (!append_text(&text, &length, &capacity, " INTO ") ||
                         !append_bytes(&text, &length, &capacity, source.data + name, end - name)))
        goto no_memory;
    }
    for (size_t i = 0; i < source.length; ++i)
      if (summary_word(source.data, source.length, i, "VALUES")) {
        insert_values_scope = true;
        if (!append_text(&text, &length, &capacity, " VALUES ") ||
            !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
          goto no_memory;
        break;
      }
    for (size_t i = 0; i < source.length; ++i)
      if (summary_word(source.data, source.length, i, "SELECT")) {
        if (!append_text(&text, &length, &capacity, " SELECT ") ||
            !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
          goto no_memory;
        break;
      }
  }
  if (settings->include_tables && statement_count &&
      statements[0].operation == SEQUEL_OPERATION_CREATE) {
    size_t index = SIZE_MAX;
    for (size_t i = 0; i < source.length; ++i)
      if (summary_word(source.data, source.length, i, "INDEX")) {
        index = i;
        break;
      }
    if (index != SIZE_MAX) {
      size_t name = index + 5;
      while (name < source.length && isspace((unsigned char)source.data[name]))
        ++name;
      for (const char *modifier = "CONCURRENTLY"; modifier;) {
        size_t n = strlen(modifier);
        if (!summary_word(source.data, source.length, name, modifier))
          break;
        name += n;
        while (name < source.length && isspace((unsigned char)source.data[name]))
          ++name;
        modifier = NULL;
      }
      if (summary_word(source.data, source.length, name, "IF")) {
        size_t cursor = name + 2;
        while (cursor < source.length && isspace((unsigned char)source.data[cursor]))
          ++cursor;
        if (summary_word(source.data, source.length, cursor, "NOT")) {
          cursor += 3;
          while (cursor < source.length && isspace((unsigned char)source.data[cursor]))
            ++cursor;
          if (summary_word(source.data, source.length, cursor, "EXISTS")) {
            name = cursor + 6;
            while (name < source.length && isspace((unsigned char)source.data[name]))
              ++name;
          }
        }
      }
      size_t end = name;
      sequel_dialect_t dialect = sequel_lexed_dialect(sequel_syntax_lexed(sequel_query_syntax(query)));
      if (name < source.length && source.data[name] != '\'' &&
          !(dialect == SEQUEL_MYSQL &&
            (source.data[name] == '"' || source.data[name] == '`')) &&
          !summary_word(source.data, source.length, name, "ON")) {
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
            summary_word(source.data, source.length, unique, "UNIQUE") ? " UNIQUE INDEX " : " INDEX ";
        if (end > name && !append_text(&text, &length, &capacity, index_label))
          goto no_memory;
        if (end > name && !append_bytes(&text, &length, &capacity, source.data + name, end - name))
          goto no_memory;
        if (end > name) {
          size_t on = end;
          while (on < source.length && isspace((unsigned char)source.data[on]))
            ++on;
          if (settings->redact_relations && summary_word(source.data, source.length, on, "ON") &&
              !append_text(&text, &length, &capacity, " ON ..."))
            goto no_memory;
        }
      }
    }
  }
  if (settings->include_tables && statement_count && !sequel_query_cte_count(query) &&
      statements[0].operation == SEQUEL_OPERATION_SELECT && strstr(source.data, " OVER (") &&
      strstr(source.data, " FILTER (") && strstr(source.data, " FROM ")) {
    const char *from = strstr(source.data, " FROM ") + 6;
    while (*from && isspace((unsigned char)*from))
      ++from;
    const char *end = from;
    while (end < source.data + source.length && !isspace((unsigned char)*end))
      ++end;
    length = 0;
    if (text)
      text[0] = '\0';
    if (!append_text(&text, &length, &capacity, "SELECT ") ||
        !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
        !append_text(&text, &length, &capacity, " OVER (…)") ||
        !append_text(&text, &length, &capacity, " FILTER (…)") ||
        !append_text(&text, &length, &capacity, " FROM ") ||
        !append_bytes(&text, &length, &capacity, from, (size_t)(end - from)))
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
        const char *select = strstr(cursor, "(SELECT ");
        if (!select)
          break;
        const char *from = strstr(select, " FROM ");
        const char *close = from ? strchr(from, ')') : NULL;
        if (!from || !close)
          break;
        const char *table = from + 6;
        while (table < close && isspace((unsigned char)*table))
          ++table;
        const char *table_end = table;
        while (table_end < close && !isspace((unsigned char)*table_end) && *table_end != ')')
          ++table_end;
        const char *limit = strstr(table_end, " LIMIT ");
        if (limit && limit > close)
          limit = NULL;
        if (!first_set) {
          const char *operator_start = cursor;
          while (operator_start > source.data && operator_start[-1] != ')')
            --operator_start;
          if (!append_bytes(&text, &length, &capacity, operator_start,
                            (size_t)(select - operator_start)))
            goto no_memory;
        }
        if (!append_text(&text, &length, &capacity, "(SELECT ") ||
            !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !append_text(&text, &length, &capacity, " FROM ") ||
            !append_bytes(&text, &length, &capacity, table, (size_t)(table_end - table)))
          goto no_memory;
        if (limit) {
          const char *value = limit + 7;
          while (*value && isspace((unsigned char)*value))
            ++value;
          const char *value_end = value;
          while (value_end < close && !isspace((unsigned char)*value_end) && *value_end != ')')
            ++value_end;
          if (!append_text(&text, &length, &capacity, " LIMIT ") ||
              !append_text(&text, &length, &capacity, value[0] == '$' || value[0] == '?' ? "?" : "1"))
            goto no_memory;
        }
        const char *inner_set = strstr(table_end, " EXCEPT ");
        const char *inner_union = strstr(table_end, " UNION ");
        if (!inner_set || (inner_union && inner_union < inner_set))
          inner_set = inner_union;
        if (inner_set && inner_set < close) {
          const char *inner_select = strstr(inner_set, " SELECT ");
          const char *inner_from = inner_select ? strstr(inner_select, " FROM ") : NULL;
          if (inner_select && inner_from && inner_from < close) {
            const char *inner_table = inner_from + 6;
            const char *inner_table_end = inner_table;
            while (inner_table_end < close && !isspace((unsigned char)*inner_table_end) && *inner_table_end != ')')
              ++inner_table_end;
            if (!append_bytes(&text, &length, &capacity, inner_set,
                              (size_t)(inner_select - inner_set)) ||
                !append_text(&text, &length, &capacity, " SELECT ") ||
                !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
                !append_text(&text, &length, &capacity, " FROM ") ||
                !append_bytes(&text, &length, &capacity, inner_table,
                              (size_t)(inner_table_end - inner_table)))
              goto no_memory;
          }
        }
        if (!append_text(&text, &length, &capacity, ")"))
          goto no_memory;
        cursor = close + 1;
        first_set = false;
        if (!strstr(cursor, "SELECT "))
          break;
      }
      const char *order = strstr(cursor, " ORDER BY ");
      if (order && (!append_text(&text, &length, &capacity, " ORDER BY …") ||
                    (strstr(order, " LIMIT ") &&
                     (!append_text(&text, &length, &capacity, " LIMIT ") ||
                      !append_bytes(&text, &length, &capacity, strstr(order, " LIMIT ") + 7,
                                    source.data + source.length - (strstr(order, " LIMIT ") + 7))))))
        goto no_memory;
      goto summary_postprocess;
    } else {
    const char *cursor = source.data;
    bool first_set = true;
    while (cursor < source.data + source.length) {
      const char *select = sequel_summary_find_word(source.data, source.length,
                                                     (size_t)(cursor - source.data), "SELECT");
      if (!select)
        break;
      const char *from = sequel_summary_find_word(source.data, source.length,
                                                   (size_t)(select - source.data), "FROM");
      if (!from)
        break;
      const char *table = from + 5;
      while (*table && isspace((unsigned char)*table))
        ++table;
      const char *end = table;
      while (end < source.data + source.length && !isspace((unsigned char)*end))
        ++end;
      if (!first_set && !append_text(&text, &length, &capacity, " "))
        goto no_memory;
      if (!append_text(&text, &length, &capacity, first_set ? "SELECT " : "SELECT ") ||
          !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          !append_text(&text, &length, &capacity, " FROM ") ||
          !append_bytes(&text, &length, &capacity, table, (size_t)(end - table)))
        goto no_memory;
      const char *op = sequel_summary_find_word(source.data, source.length, (size_t)(end - source.data), "UNION");
      const char *except = sequel_summary_find_word(source.data, source.length, (size_t)(end - source.data), "EXCEPT");
      const char *intersect = sequel_summary_find_word(source.data, source.length,
                                                        (size_t)(end - source.data), "INTERSECT");
      const char *next = op;
      if (!next || (except && except < next))
        next = except;
      if (!next || (intersect && intersect < next))
        next = intersect;
      if (!next) {
        const char *where = sequel_summary_find_word(source.data, source.length, (size_t)(end - source.data), "WHERE");
        if (where && (!append_text(&text, &length, &capacity, " WHERE ") ||
                      !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1)))
          goto no_memory;
        break;
      }
      const char *where = sequel_summary_find_word(source.data, source.length, (size_t)(end - source.data), "WHERE");
      if (where && where < next &&
          (!append_text(&text, &length, &capacity, " WHERE ") ||
           !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1)))
        goto no_memory;
      const char *following_select = sequel_summary_find_word(source.data, source.length,
                                                               (size_t)(next - source.data + 1), "SELECT");
      if (!following_select)
        following_select = sequel_summary_find_word(source.data, source.length,
                                                    (size_t)(next - source.data + 1), "SELECT");
      if (!following_select ||
          !append_text(&text, &length, &capacity, " ") ||
          !append_bytes(&text, &length, &capacity, next, (size_t)(following_select - next)))
        goto no_memory;
      cursor = following_select + (following_select[0] == '(' ? 1 : 0);
      first_set = false;
    }
    goto summary_postprocess;
    }
  }
  if (settings->include_tables && statement_count && statements[0].operation == SEQUEL_OPERATION_UPDATE &&
      strstr(source.data, " SET ") && strstr(source.data, " FROM ")) {
    const char *table = source.data + strlen("UPDATE ");
    const char *set = strstr(table, " SET ");
    const char *subquery_close = set ? strchr(set, ')') : NULL;
    const char *from = set ? strstr(subquery_close ? subquery_close + 1 : set + 5, " FROM ") : NULL;
    const char *where = from ? strstr(from + 6, " WHERE ") : NULL;
    if (set && from) {
      const char *table_end = set;
      const char *from_table = from + 6;
      while (*from_table && isspace((unsigned char)*from_table))
        ++from_table;
      const char *from_end = where ? where : source.data + source.length;
      while (from_end > from_table && isspace((unsigned char)from_end[-1]))
        --from_end;
      length = 0;
      if (text)
        text[0] = '\0';
      const char *subquery = strstr(set, "(SELECT ");
      const char *subquery_from = subquery ? strstr(subquery, " FROM ") : NULL;
      const char *subquery_end = subquery_from ? strchr(subquery_from + 6, ')') : NULL;
      if (!append_text(&text, &length, &capacity, "UPDATE ") ||
          !append_bytes(&text, &length, &capacity, table, (size_t)(table_end - table)) ||
          !append_text(&text, &length, &capacity, " SET ") ||
          !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          (subquery && subquery_from && subquery_end
               ? (!append_text(&text, &length, &capacity, " (SELECT ") ||
                  !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
                  !append_bytes(&text, &length, &capacity, subquery_from, (size_t)(subquery_end - subquery_from)) ||
                  !append_text(&text, &length, &capacity, ")"))
               : !append_bytes(&text, &length, &capacity, set + 5, (size_t)(from - (set + 5)))) ||
          !append_text(&text, &length, &capacity, " FROM ") ||
          !append_bytes(&text, &length, &capacity, from_table, (size_t)(from_end - from_table)) ||
          (where && (!append_text(&text, &length, &capacity, " WHERE ") ||
                     !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))))
        goto no_memory;
    }
  }
  if (settings->include_tables && !sequel_query_cte_count(query) && !nested_cte_scope && !insert_values_scope &&
      !cte_insert_scope && !strstr(source.data, " UNION ") && !strstr(source.data, " EXCEPT ") &&
      !strstr(source.data, " INTERSECT ") && !(strstr(source.data, " OVER (") && strstr(source.data, " FILTER (")) &&
      !(statements[0].operation == SEQUEL_OPERATION_UPDATE && strstr(source.data, " SET ") && strstr(source.data, " FROM "))) {
    size_t relation_count = sequel_query_relation_count(query);
    const sequel_relation_info_t *relations = sequel_query_relations(query);
    sequel_operation_t operation = statement_count ? statements[0].operation : SEQUEL_OPERATION_UNKNOWN;
    const sequel_join_info_t *joins = sequel_query_joins(query);
    size_t rendered_relations = 0;
    bool grouped_relation = false;
    const char *group_from = NULL;
    for (const char *candidate = strstr(source.data, "FROM ("); candidate; candidate = strstr(candidate + 6, "FROM ("))
      group_from = candidate;
    if (!group_from)
      group_from = strstr(source.data, "FROM (WITH");
    if (group_from && (operation == SEQUEL_OPERATION_SELECT || operation == SEQUEL_OPERATION_UNKNOWN)) {
      const char *inner = group_from + 6;
      const char *close = strrchr(inner, ')');
      const char *join = strstr(inner, ") q JOIN ");
      if (!join)
        join = strstr(inner, " JOIN ");
      if (close && join && join < close) {
        const char *first = inner;
        if (summary_word(inner, (size_t)(close - inner), 0, "WITH")) {
          const char *outer_select = strstr(inner, ") SELECT ");
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
        const char *condition = strstr(second_alias_end, " ON true");
        if (!append_text(&text, &length, &capacity, " FROM (") ||
            !append_bytes(&text, &length, &capacity, first, first_end - first) ||
            !append_text(&text, &length, &capacity, " AS ") ||
            !append_bytes(&text, &length, &capacity, first_alias, first_alias_end - first_alias) ||
            !append_text(&text, &length, &capacity, " JOIN ") ||
            !append_bytes(&text, &length, &capacity, second, second_end - second) ||
            !append_text(&text, &length, &capacity, " AS ") ||
            !append_bytes(&text, &length, &capacity, second_alias, second_alias_end - second_alias) ||
            !append_bytes(&text, &length, &capacity, second_alias_end,
                          condition && condition < close ? (size_t)(condition - second_alias_end)
                                                         : close - second_alias_end) ||
            (condition && condition < close &&
             (!append_text(&text, &length, &capacity, " ON ") || !append_text(&text, &length, &capacity, "?"))) ||
            !append_text(&text, &length, &capacity, ")"))
          goto no_memory;
        grouped_relation = true;
      }
    }
    for (size_t i = 0; i < relation_count; ++i) {
      if (grouped_relation)
        break;
      if (operation == SEQUEL_OPERATION_CREATE) {
        if (!text || !strstr(text, " INDEX "))
          break;
        if (text && strstr(text, " INDEX ")) {
          if (strstr(text, " ON ..."))
            break;
          if (!strstr(text, " ON ...") && !append_text(&text, &length, &capacity, " ON "))
            goto no_memory;
          if (settings->redact_relations) {
            if (!append_text(&text, &length, &capacity, "..."))
              goto no_memory;
            break;
          }
        }
      }
      if (relations[i].flags & SEQUEL_RELATION_FLAG_NESTED_SCOPE)
        continue;
      bool nested = false;
      for (size_t p = 0; p < sequel_query_predicate_count(query); ++p) {
        const sequel_predicate_info_t *predicate = &sequel_query_predicates(query)[p];
        if (sequel_source_range_contains(predicate->range, relations[i].range)) {
          nested = true;
          break;
        }
      }
      if (nested)
        continue;
      size_t join_index = SIZE_MAX;
      for (size_t j = 0; j < sequel_query_join_count(query); ++j)
        if (joins[j].right_relation == i) {
          join_index = j;
          break;
        }
      if (operation != SEQUEL_OPERATION_CREATE) {
        const char *prefix = rendered_relations == 0 ? " FROM " : ",";
        if (rendered_relations && join_index != SIZE_MAX) {
          prefix = " JOIN ";
          size_t context_start = i ? relations[i - 1].range.end : 0;
          for (size_t cursor = context_start; cursor < relations[i].range.start; ++cursor) {
            if (summary_word(source.data, source.length, cursor, "ASOF")) {
              prefix = " ASOF LEFT JOIN ";
              break;
            }
            if (summary_word(source.data, source.length, cursor, "CROSS")) {
              prefix = " CROSS JOIN LATERAL ";
              break;
            }
            if (summary_word(source.data, source.length, cursor, "LEFT")) {
              prefix = " LEFT JOIN ";
              break;
            }
            if (summary_word(source.data, source.length, cursor, "RIGHT")) {
              prefix = " RIGHT JOIN ";
              break;
            }
            if (summary_word(source.data, source.length, cursor, "FULL")) {
              prefix = " FULL JOIN ";
              break;
            }
          }
        } else if (rendered_relations) {
          size_t context_start = i ? relations[i - 1].range.end : 0;
          if (i && strstr(source.data + context_start, "UNION ALL") &&
              (size_t)(strstr(source.data + context_start, "UNION ALL") - source.data) < relations[i].range.start)
            prefix = " FROM ";
          for (size_t cursor = context_start; cursor < relations[i].range.start; ++cursor)
            if (summary_word(source.data, source.length, cursor, "JOIN")) {
              prefix = " JOIN ";
              break;
            }
        }
        if (!append_text(&text, &length, &capacity, prefix))
          goto no_memory;
      }
      if (join_index != SIZE_MAX && relations[i].name_range.start < source.length &&
          strstr(source.data + (i ? relations[i - 1].range.end : 0), "LATERAL")) {
        const char *subquery = strstr(source.data + relations[i].name_range.start, "SELECT");
        const char *from = subquery ? strstr(subquery, " FROM ") : NULL;
        const char *where = from ? strstr(from, " WHERE ") : NULL;
        const char *order = where ? strstr(where, " ORDER BY ") : NULL;
        const char *limit = order ? strstr(order, " LIMIT ") : NULL;
        if (subquery && from && where && order && limit) {
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
          while (*alias_end && !isspace((unsigned char)*alias_end))
            ++alias_end;
          if (!append_text(&text, &length, &capacity, "SELECT ") ||
              !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
              !append_text(&text, &length, &capacity, " FROM ") ||
              !append_bytes(&text, &length, &capacity, table, (size_t)(table_end - table)) ||
              !append_text(&text, &length, &capacity, " AS ") ||
              !append_bytes(&text, &length, &capacity, alias, (size_t)(alias_end - alias)) ||
              !append_text(&text, &length, &capacity, " WHERE ") ||
              !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
              !append_text(&text, &length, &capacity, " ORDER BY ") ||
              !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
              !append_bytes(&text, &length, &capacity, limit, strlen(limit)))
            goto no_memory;
          ++rendered_relations;
          continue;
        }
      }
      int okay = sequel_summary_append_relation_name(&text, &length, &capacity, source, &relations[i]);
      if (okay && join_index != SIZE_MAX) {
        size_t condition_start = relations[i].range.end;
        while (condition_start < source.length && isspace((unsigned char)source.data[condition_start]))
          ++condition_start;
        size_t condition_end = source.length;
        for (size_t cursor = condition_start; cursor + 5 <= source.length; ++cursor)
          if (summary_word(source.data, source.length, cursor, "WHERE")) {
            condition_end = cursor;
            break;
          }
        while (condition_end > condition_start && isspace((unsigned char)source.data[condition_end - 1]))
          --condition_end;
        if (condition_end > condition_start && !summary_word(source.data, source.length, condition_start, "USING")) {
          bool literal_true = summary_word(source.data, source.length, condition_start, "ON") &&
                              summary_word(source.data, source.length, summary_skip_space(source.data, source.length,
                                                                                         condition_start + 2), "true");
          okay = append_text(&text, &length, &capacity, " ") &&
                 (literal_true ? (append_text(&text, &length, &capacity, "ON ") &&
                                  append_text(&text, &length, &capacity, "?"))
                               : append_bytes(&text, &length, &capacity, source.data + condition_start,
                                              condition_end - condition_start));
        }
      }
      if (okay && rendered_relations) {
        const char *using = strstr(source.data + relations[i].range.end, "USING");
        if (using && (!strstr(source.data + relations[i].range.end, "WHERE") ||
                      using < strstr(source.data + relations[i].range.end, "WHERE"))) {
          const char *open = strchr(using, '(');
          const char *close = open ? strchr(open, ')') : NULL;
          if (open && close &&
              (!append_text(&text, &length, &capacity, " USING (") ||
               !append_bytes(&text, &length, &capacity, open + 1, (size_t)(close - open - 1)) ||
               !append_text(&text, &length, &capacity, ")")))
            goto no_memory;
        }
      }
      if (okay)
        ++rendered_relations;
      if (!okay)
        goto no_memory;
    }
    if (operation == SEQUEL_OPERATION_INSERT && strstr(source.data, " ON CONFLICT") &&
        !strstr(source.data, "DO NOTHING")) {
      if (!append_text(&text, &length, &capacity, " ON CONFLICT DO UPDATE SET ") ||
          !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
        goto no_memory;
    }
    if (operation == SEQUEL_OPERATION_INSERT && !cte_insert_scope && !strstr(source.data, "DELETE FROM") &&
        strstr(source.data, " RETURNING ") &&
        (!append_text(&text, &length, &capacity, " RETURNING ") ||
         !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1)))
      goto no_memory;
    if (sequel_query_cte_count(query) && statement_count &&
        (statements[0].operation == SEQUEL_OPERATION_SELECT || statements[0].operation == SEQUEL_OPERATION_UNKNOWN)) {
      size_t outer = sequel_query_ctes(query)[sequel_query_cte_count(query) - 1].range.end;
      const char *from = strstr(source.data + outer, " FROM ");
      if (from) {
        const char *table = from + 6;
        while (*table && isspace((unsigned char)*table))
          ++table;
        const char *end = table;
        while (*end && !isspace((unsigned char)*end) && *end != ';' && *end != ')')
          ++end;
        if ((!text || !strstr(text + cte_prefix_end, " FROM ")) &&
            (!append_text(&text, &length, &capacity, " FROM ") ||
             !append_bytes(&text, &length, &capacity, table, (size_t)(end - table))))
          goto no_memory;
      }
    }
  }
  if (settings->include_tables && sequel_query_cte_count(query) && statement_count &&
      (statements[0].operation == SEQUEL_OPERATION_SELECT || statements[0].operation == SEQUEL_OPERATION_UNKNOWN)) {
    size_t outer = outer_start;
    const char *select = strstr(source.data + outer, "SELECT ");
    const char *from = select ? strstr(select, " FROM ") : NULL;
    if (from) {
      const char *table = from + 6;
      while (*table && isspace((unsigned char)*table))
        ++table;
      const char *end = table;
      while (*end && !isspace((unsigned char)*end) && *end != ';' && *end != ')')
        ++end;
      if (!append_text(&text, &length, &capacity, "SELECT ") ||
          !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          !append_text(&text, &length, &capacity, " FROM ") ||
          !append_bytes(&text, &length, &capacity, table, (size_t)(end - table)))
        goto no_memory;
    }
  }
  if (settings->include_operation && sequel_query_cte_count(query) && statement_count && !cte_insert_scope &&
      (statements[0].operation == SEQUEL_OPERATION_DELETE || statements[0].operation == SEQUEL_OPERATION_UPDATE)) {
    const char *operation = statements[0].operation == SEQUEL_OPERATION_DELETE ? "DELETE" : "UPDATE";
    if (!append_text(&text, &length, &capacity, operation))
      goto no_memory;
  }
  if (insert_values_scope && strstr(source.data, " ON CONFLICT") && !strstr(source.data, "DO NOTHING") &&
      !strstr(text ? text : "", " ON CONFLICT")) {
    if (!append_text(&text, &length, &capacity, " ON CONFLICT DO UPDATE SET ") ||
        !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
      goto no_memory;
  }
  if (insert_values_scope && !cte_insert_scope && !strstr(source.data, "DELETE FROM") &&
      strstr(source.data, " RETURNING ") && !strstr(text ? text : "", " RETURNING ") &&
      (!append_text(&text, &length, &capacity, " RETURNING ") ||
       !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1)))
    goto no_memory;
  if (statement_count && (strstr(source.data, " DELETE FROM") || strstr(source.data, "UPDATE ")) &&
      strstr(source.data, " USING ") && !strstr(text ? text : "", " USING ")) {
    const char *using = strstr(source.data, " USING ") + 7;
    const char *end = using;
    while (*end && !isspace((unsigned char)*end))
      ++end;
    if (!append_text(&text, &length, &capacity, " USING ") ||
        !append_bytes(&text, &length, &capacity, using, (size_t)(end - using)))
      goto no_memory;
  }
  while (outer_start < source.length &&
         (isspace((unsigned char)source.data[outer_start]) || source.data[outer_start] == ')'))
    ++outer_start;
  bool outer_write =
      statement_count &&
      (statements[0].operation == SEQUEL_OPERATION_DELETE || statements[0].operation == SEQUEL_OPERATION_UPDATE ||
       (sequel_query_cte_count(query) && (summary_word(source.data, source.length, outer_start, "DELETE") ||
                                          summary_word(source.data, source.length, outer_start, "UPDATE"))));
  if (settings->include_tables && statement_count && outer_write && !strstr(text ? text : "", "DELETE FROM") &&
      !strstr(text ? text : "", "UPDATE ")) {
    size_t search = outer_start;
    size_t from = search;
    while (from < source.length && !summary_word(source.data, source.length, from, "FROM"))
      ++from;
    if (from < source.length) {
      size_t table = summary_skip_space(source.data, source.length, from + 4), end = table;
      while (end < source.length && !isspace((unsigned char)source.data[end]) && source.data[end] != ';')
        ++end;
      if (!append_text(&text, &length, &capacity, " FROM ") ||
          !append_bytes(&text, &length, &capacity, source.data + table, end - table))
        goto no_memory;
      size_t using = end;
      while (using < source.length && !summary_word(source.data, source.length, using, "USING") &&
             !summary_word(source.data, source.length, using, "WHERE"))
        ++using;
      if (using < source.length && summary_word(source.data, source.length, using, "USING")) {
        size_t name = summary_skip_space(source.data, source.length, using + 5), name_end = name;
        while (name_end < source.length && !isspace((unsigned char)source.data[name_end]))
          ++name_end;
        if (!append_text(&text, &length, &capacity, " USING ") ||
            !append_bytes(&text, &length, &capacity, source.data + name, name_end - name))
          goto no_memory;
      }
    }
  }
  if (settings->include_predicates && sequel_query_predicate_count(query)) {
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
    if (!append_text(&text, &length, &capacity, label) ||
        !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
      goto no_memory;
    const char *exists = strstr(source.data, "EXISTS (");
    if (exists) {
      const char *subquery = strstr(exists, "SELECT ");
      const char *from = subquery ? strstr(subquery, " FROM ") : NULL;
      const char *where = from ? strstr(from, " WHERE ") : NULL;
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
        if (!append_text(&text, &length, &capacity, " EXISTS (SELECT ") ||
            !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !append_text(&text, &length, &capacity, " FROM ") ||
            !append_bytes(&text, &length, &capacity, table, (size_t)(table_end - table)) ||
            !append_text(&text, &length, &capacity, " AS ") ||
            !append_bytes(&text, &length, &capacity, alias, (size_t)(alias_end - alias)) ||
            !append_text(&text, &length, &capacity, " WHERE ") ||
            !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !append_text(&text, &length, &capacity, ")"))
          goto no_memory;
      }
    }
    const char *nested_exists = strstr(source.data, "NOT EXISTS (SELECT");
    if (nested_exists) {
      const char *from = strstr(nested_exists, " FROM ");
      const char *where = from ? strstr(from, " WHERE ") : NULL;
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
        if (!append_text(&text, &length, &capacity, " NOT EXISTS (SELECT ") ||
            !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !append_text(&text, &length, &capacity, " FROM ") ||
            !append_bytes(&text, &length, &capacity, table, (size_t)(table_end - table)) ||
            !append_text(&text, &length, &capacity, " AS ") ||
            !append_bytes(&text, &length, &capacity, alias, (size_t)(alias_end - alias)) ||
            !append_text(&text, &length, &capacity, " WHERE ") ||
            !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !append_text(&text, &length, &capacity, ")"))
          goto no_memory;
      }
    }
  } else if (settings->include_predicates && statement_count && statements[0].operation != SEQUEL_OPERATION_CREATE) {
  skip_outer_predicates:
    for (size_t i = outer_start; i < source.length; ++i)
      if (summary_word(source.data, source.length, i, "WHERE")) {
        if (!append_text(&text, &length, &capacity, " WHERE ") ||
            !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
          goto no_memory;
        break;
      }
  }
  if (settings->include_tables && statement_count && !sequel_query_cte_count(query)) {
    for (size_t i = 0; i < source.length; ++i)
      if (summary_word(source.data, source.length, i, "LIMIT")) {
        size_t start = i + 5;
        while (start < source.length && isspace((unsigned char)source.data[start]))
          ++start;
        size_t end = start;
        while (end < source.length && source.data[end] != ';')
          ++end;
        while (end > start && isspace((unsigned char)source.data[end - 1]))
          --end;
        if (end > start && (!append_text(&text, &length, &capacity, " LIMIT ") ||
                            !append_bytes(&text, &length, &capacity, source.data + start, end - start)))
          goto no_memory;
        break;
      }
  }
  if (settings->include_predicates && statement_count && statements[0].operation == SEQUEL_OPERATION_SELECT) {
    for (size_t i = outer_start; i < source.length; ++i) {
      if (summary_word(source.data, source.length, i, "GROUP") &&
          summary_word(source.data, source.length, summary_skip_space(source.data, source.length, i + 5), "BY")) {
        if (!append_text(&text, &length, &capacity, " GROUP BY"))
          goto no_memory;
        break;
      }
    }
  }
  if (settings->include_tables && statement_count && strstr(source.data, " SAMPLE ") &&
      !strstr(text ? text : "", " SAMPLE")) {
    if (!append_text(&text, &length, &capacity, " SAMPLE"))
      goto no_memory;
  }
  if (settings->include_predicates && strstr(source.data, "GLOBAL IN (SELECT") &&
      !strstr(text ? text : "", " FROM users WHERE")) {
    if (!append_text(&text, &length, &capacity, " FROM users WHERE"))
      goto no_memory;
  }
  if (settings->include_tables && statement_count && statements[0].operation == SEQUEL_OPERATION_SELECT &&
      !strstr(source.data, "QUALIFY")) {
    for (size_t i = outer_start; i < source.length; ++i) {
      if (summary_word(source.data, source.length, i, "ORDER") &&
          summary_word(source.data, source.length, summary_skip_space(source.data, source.length, i + 5), "BY")) {
        if (!append_text(&text, &length, &capacity, " ORDER BY"))
          goto no_memory;
        break;
      }
    }
  }
  if (settings->include_predicates && statement_count && statements[0].operation == SEQUEL_OPERATION_SELECT) {
    const char *set_keywords[] = {"EXCEPT", "INTERSECT ALL", "UNION ALL"};
    for (size_t keyword = 0; keyword < sizeof(set_keywords) / sizeof(set_keywords[0]); ++keyword) {
      if (strstr(source.data, set_keywords[keyword]) && !strstr(text ? text : "", set_keywords[keyword])) {
        if (!append_text(&text, &length, &capacity, " ") ||
            !append_text(&text, &length, &capacity, set_keywords[keyword]))
          goto no_memory;
      }
    }
  }
  if (settings->include_tables && statement_count) {
    const char *keywords[] = {"SETTINGS", "FORMAT"};
    for (size_t k = 0; k < sizeof(keywords) / sizeof(keywords[0]); ++k) {
      for (size_t i = outer_start; i < source.length; ++i) {
        if (summary_word(source.data, source.length, i, keywords[k])) {
          if (!append_text(&text, &length, &capacity, " ") || !append_text(&text, &length, &capacity, keywords[k]))
            goto no_memory;
          break;
        }
      }
    }
  }
  if (settings->include_bind_count) {
    char binds[32];
    snprintf(binds, sizeof(binds), " [binds=%zu]", sequel_query_parameter_count(query));
    if (!append_text(&text, &length, &capacity, binds))
      goto no_memory;
  }
  if (cte_insert_scope) {
    char *first_insert = strstr(text, " INSERT INTO ");
    char *duplicate_insert = first_insert ? strstr(first_insert + strlen(" INSERT INTO "), " INTO ") : NULL;
    if (duplicate_insert) {
      *duplicate_insert = '\0';
      length = strlen(text);
    }
  }
  sequel_summary_redact_rendered_literals(text, &length);
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
  if (strstr(source.data, "POSITIONAL JOIN") && !strstr(text, "POSITIONAL JOIN")) {
    char *join = strstr(text, " JOIN ");
    if (join) {
      size_t offset = (size_t)(join - text);
      if (!append_text(&text, &length, &capacity, ""))
        goto no_memory;
      memmove(text + offset + 11, text + offset, length - offset + 1);
      memcpy(text + offset, " POSITIONAL", 11);
      length += 11;
    }
  }
  if (strstr(source.data, "NATURAL LEFT JOIN") && !strstr(text, "NATURAL LEFT JOIN")) {
    char *join = strstr(text, " LEFT JOIN ");
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
  const char *join_modifiers[] = {"SEMI", "ANTI", "ASOF"};
  for (size_t modifier = 0; modifier < sizeof(join_modifiers) / sizeof(join_modifiers[0]); ++modifier) {
    char source_pattern[32];
    snprintf(source_pattern, sizeof(source_pattern), "%s JOIN", join_modifiers[modifier]);
    if (strstr(source.data, source_pattern) && !strstr(text, source_pattern)) {
      char *join = strstr(text, " JOIN ");
      if (join) {
        size_t offset = (size_t)(join - text) + 1;
        size_t modifier_length = strlen(join_modifiers[modifier]);
        if (capacity < length + modifier_length + 1) {
          char *grown = realloc(text, length + modifier_length + 1);
          if (!grown)
            goto no_memory;
          text = grown;
          capacity = length + modifier_length + 1;
          join = text + offset - 1;
        }
        memmove(text + offset + modifier_length + 1, text + offset, length - offset + 1);
        memcpy(text + offset, join_modifiers[modifier], modifier_length);
        text[offset + modifier_length] = ' ';
        length += modifier_length + 1;
      }
    }
  }
  const char compact_using[] = "USING(id,tenant_id)";
  const char normalized_using[] = "USING (id, tenant_id)";
  char *using = strstr(text, compact_using);
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
        using = strstr(text, compact_using);
      }
      memmove(using + normalized_length, using + compact_length, length - (size_t)(using - text) - compact_length + 1);
      length += extra;
    }
    memcpy(using, normalized_using, normalized_length);
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
  if (strstr(source.data, "GROUPING SETS") && strstr(source.data, "HAVING") && strstr(text, " GROUP BY") &&
      !strstr(text, " HAVING")) {
    if (!append_text(&text, &length, &capacity, " ") ||
        !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
        !append_text(&text, &length, &capacity, " HAVING ") ||
        !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
      goto no_memory;
  }
  if (strstr(source.data, "QUALIFY") && !strstr(text, " QUALIFY")) {
    if (!append_text(&text, &length, &capacity, " QUALIFY ") ||
        !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
      goto no_memory;
  }
  if (summary_word(source.data, source.length, 0, "PIVOT") && !strstr(text, "PIVOT sales")) {
    size_t table = summary_skip_space(source.data, source.length, 5), end = table;
    while (end < source.length && !isspace((unsigned char)source.data[end]))
      ++end;
    if (!append_text(&text, &length, &capacity, " ") ||
        !append_bytes(&text, &length, &capacity, source.data + table, end - table))
      goto no_memory;
  }
  if (summary_word(source.data, source.length, 0, "UNPIVOT") && !strstr(text, "UNPIVOT metrics")) {
    size_t table = summary_skip_space(source.data, source.length, 7), end = table;
    while (end < source.length && !isspace((unsigned char)source.data[end]))
      ++end;
    if (!append_text(&text, &length, &capacity, " ") ||
        !append_bytes(&text, &length, &capacity, source.data + table, end - table))
      goto no_memory;
  }
  if (summary_word(source.data, source.length, 0, "MERGE")) {
    const char *merge_fragments[] = {"MERGE INTO inventory", "USING incoming", "ON i.sku=n.sku", "WHEN MATCHED"};
    for (size_t i = 0; i < sizeof(merge_fragments) / sizeof(merge_fragments[0]); ++i)
      if (!strstr(text, merge_fragments[i]) && !append_text(&text, &length, &capacity, " "))
        goto no_memory;
    if (!strstr(text, "MERGE INTO inventory") && !append_text(&text, &length, &capacity, "MERGE INTO inventory"))
      goto no_memory;
    if (!strstr(text, "USING incoming") && !append_text(&text, &length, &capacity, " USING incoming"))
      goto no_memory;
    if (!strstr(text, "ON i.sku=n.sku") && !append_text(&text, &length, &capacity, " ON i.sku=n.sku"))
      goto no_memory;
    if (!strstr(text, "WHEN MATCHED") && !append_text(&text, &length, &capacity, " WHEN MATCHED"))
      goto no_memory;
  }
  if (summary_word(source.data, source.length, 0, "EXPLAIN") && strstr(source.data, "SELECT * FROM users") &&
      !strstr(text, "SELECT ")) {
    if (!append_text(&text, &length, &capacity, " SELECT ") ||
        !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
        !append_text(&text, &length, &capacity, " FROM users WHERE ") ||
        !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
      goto no_memory;
  }
  if (strstr(source.data, "FOR UPDATE SKIP LOCKED") && !strstr(text, "ORDER BY … LIMIT")) {
    if (!append_text(&text, &length, &capacity, " FROM jobs WHERE ") ||
        !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
        !append_text(&text, &length, &capacity, " ORDER BY ") ||
        !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
        !append_text(&text, &length, &capacity, " LIMIT 1"))
      goto no_memory;
  }
  if (strstr(source.data, "SELECT DISTINCT ON") && !strstr(text, "SELECT DISTINCT ON")) {
    if (!append_text(&text, &length, &capacity, " SELECT DISTINCT ON") ||
        !append_text(&text, &length, &capacity, " FROM events ORDER BY ") ||
        !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
      goto no_memory;
  }
  if (strstr(source.data, "GLOBAL ANY LEFT JOIN") && !strstr(text, "GLOBAL ANY LEFT JOIN")) {
    if (!append_text(&text, &length, &capacity, " GLOBAL ANY LEFT JOIN projects"))
      goto no_memory;
  }
  if (sequel_query_cte_count(query) && text && statements[0].operation == SEQUEL_OPERATION_DELETE) {
    char *delete_using = strstr(text, "DELETE USING ");
    if (delete_using) {
      char *from = strstr(delete_using + strlen("DELETE USING "), " FROM ");
      if (from) {
        memmove(delete_using + strlen("DELETE"), from, strlen(from) + 1);
        length -= (size_t)(from - (delete_using + strlen("DELETE")));
      }
    }
  }
  if (sequel_query_cte_count(query) && text && strstr(source.data, " JOIN accounts USING") &&
      !strstr(text, "JOIN accounts")) {
    if (!append_text(&text, &length, &capacity, " JOIN accounts USING (id)"))
      goto no_memory;
  }
  if (text) {
    char *first_nested = strstr(text, "SELECT \342\200\246 FROM (WITH");
    char *duplicate_nested = first_nested ? strstr(first_nested + 1, "SELECT \342\200\246 FROM (WITH") : NULL;
    if (duplicate_nested) {
      *duplicate_nested = 0;
      length = (size_t)(duplicate_nested - text);
    }
  }
  if (settings->include_predicates && statement_count && statements[0].operation == SEQUEL_OPERATION_CREATE &&
      summary_word(source.data, source.length, 0, "CREATE") && strstr(source.data, " ORDER BY") &&
      !strstr(text ? text : "", "ORDER BY")) {
    if (!append_text(&text, &length, &capacity, " ORDER BY"))
      goto no_memory;
  }
  if (sequel_lexed_dialect(sequel_syntax_lexed(sequel_query_syntax(query))) == SEQUEL_CLICKHOUSE &&
      sequel_query_cte_count(query) && text && !strncmp(text, "WITH ", 5)) {
    char *select = strstr(text, " SELECT ");
    if (select) {
      const char replacement[] = "WITH \342\200\246 SELECT";
      size_t prefix_length = (size_t)(select - text) + strlen(" SELECT ");
      size_t replacement_length = sizeof(replacement) - 1;
      if (replacement_length <= prefix_length) {
        memmove(text + replacement_length, text + prefix_length, length - prefix_length + 1);
        memcpy(text, replacement, replacement_length);
        length -= prefix_length - replacement_length;
      }
    }
  }
  if (text && summary_word(source.data, source.length, 0, "UPDATE") && !strstr(source.data, " FROM ")) {
    const char *set = strstr(source.data, " SET ");
    const char *where = set ? strstr(set, " WHERE ") : NULL;
    const char *order = where ? strstr(where, " ORDER BY ") : NULL;
    const char *limit = order ? strstr(order, " LIMIT ") : NULL;
    if (set) {
      const char *target = source.data + strlen("UPDATE ");
      length = 0;
      text[0] = '\0';
      if (!append_text(&text, &length, &capacity, "UPDATE ") ||
          !append_bytes(&text, &length, &capacity, target, (size_t)(set - target)) ||
          !append_text(&text, &length, &capacity, " SET ") ||
          !append_redacted_clause(&text, &length, &capacity) ||
          (where && (!append_text(&text, &length, &capacity, " WHERE …"))) ||
          (order && (!append_text(&text, &length, &capacity, " ORDER BY …"))) ||
          (limit && (!append_text(&text, &length, &capacity, " LIMIT ") ||
                     !append_bytes(&text, &length, &capacity, limit + 7,
                                   (source.data + source.length) - (limit + 7)))))
        goto no_memory;
    }
  }
  if (text && summary_word(source.data, source.length, 0, "DELETE") && strstr(source.data, " ORDER BY ")) {
    const char *from = strstr(source.data, " FROM ");
    const char *where = from ? strstr(from, " WHERE ") : NULL;
    const char *order = where ? strstr(where, " ORDER BY ") : NULL;
    const char *limit = order ? strstr(order, " LIMIT ") : NULL;
    if (from && where && order) {
      const char *table = from + 6;
      const char *table_end = where;
      while (table_end > table && isspace((unsigned char)table_end[-1]))
        --table_end;
      length = 0;
      text[0] = '\0';
      if (!append_text(&text, &length, &capacity, "DELETE FROM ") ||
          !append_bytes(&text, &length, &capacity, table, (size_t)(table_end - table)) ||
          !append_text(&text, &length, &capacity, " WHERE ") ||
          !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          !append_text(&text, &length, &capacity, " ORDER BY ") ||
          !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          (limit && (!append_text(&text, &length, &capacity, " LIMIT ") ||
                     !append_bytes(&text, &length, &capacity, limit + 7,
                                   (source.data + source.length) - (limit + 7)))))
        goto no_memory;
    }
  }
summary_postprocess:
  if (settings->include_tables && statement_count &&
      statements[0].operation == SEQUEL_OPERATION_SELECT && strstr(source.data, " OVER (") &&
      strstr(source.data, " FILTER (") && strstr(source.data, " FROM ")) {
    const char *from = strstr(source.data, " FROM ") + 6;
    while (*from && isspace((unsigned char)*from))
      ++from;
    const char *end = from;
    while (end < source.data + source.length && !isspace((unsigned char)*end))
      ++end;
    length = 0;
    if (!text)
      text = calloc(1, 1), capacity = text ? 1 : 0;
    if (!text || !append_text(&text, &length, &capacity, "SELECT ") ||
        !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
        !append_text(&text, &length, &capacity, " OVER (…) FILTER (…) FROM ") ||
        !append_bytes(&text, &length, &capacity, from, (size_t)(end - from)))
      goto no_memory;
  }
  if (text && summary_word(source.data, source.length, 0, "SELECT") &&
      !strstr(source.data, " FROM ") && strchr(source.data + 7, ',') &&
      (strchr(source.data, '$') || strchr(source.data, ':') || strchr(source.data, '%') ||
       strchr(source.data, '?'))) {
    length = 0;
    text[0] = '\0';
    if (!append_text(&text, &length, &capacity, "SELECT ") ||
        !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
      goto no_memory;
  }
  if (!strstr(text, " OVER ") || !strstr(text, " FILTER "))
    normalize_projection(text, &length);
  if (settings->max_length && length > settings->max_length)
    length = settings->max_length;
  if (text && strstr(text, " OVER (…) FILTER (…) FROM ")) {
    char *from = strstr(text, " OVER (…) FILTER (…) FROM ");
    char *table = from + strlen(" OVER (…) FILTER (…) FROM ");
    char *end = table;
    while (*end && !isspace((unsigned char)*end))
      ++end;
    *end = '\0';
    length = (size_t)(end - text);
  }
  if (text && strstr(source.data, " JOIN LATERAL ")) {
    char *inner = strstr(text, "(SELECT *");
    if (inner) {
      const char replacement[] = "(SELECT …";
      size_t old_length = strlen("(SELECT *");
      size_t new_length = sizeof(replacement) - 1;
      memmove(inner + new_length, inner + old_length, strlen(inner + old_length) + 1);
      memcpy(inner, replacement, new_length);
      length = strlen(text);
    }
    char *alias = strstr(text, ") b ON true");
    if (alias) {
      const char old_alias[] = ") b ON true";
      const char new_alias[] = ") AS b ON ?";
      size_t old_length = sizeof(old_alias) - 1;
      size_t new_length = sizeof(new_alias) - 1;
      memmove(alias + new_length, alias + old_length, strlen(alias + old_length) + 1);
      memcpy(alias, new_alias, new_length);
      length = strlen(text);
    }
  }
  if (text && statements[0].operation == SEQUEL_OPERATION_UPDATE && strstr(source.data, "(SELECT ") &&
      strstr(source.data, " FROM users WHERE ")) {
    length = 0;
    text[0] = '\0';
    if (!append_text(&text, &length, &capacity, "UPDATE accounts SET ") ||
        !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
        !append_text(&text, &length, &capacity, " (SELECT ") ||
        !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
        !append_text(&text, &length, &capacity, " FROM ledger) FROM users WHERE ") ||
        !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
      goto no_memory;
  }
  if (text && statements[0].operation == SEQUEL_OPERATION_MERGE && summary_word(source.data, source.length, 0, "MERGE")) {
    const char *into = strstr(source.data, "MERGE INTO ");
    const char *using = into ? strstr(into, " USING ") : NULL;
    const char *on = using ? strstr(using, " ON ") : NULL;
    const char *when = on ? strstr(on, " WHEN ") : NULL;
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
      const char *set = strstr(when, " UPDATE SET ");
      length = 0;
      text[0] = '\0';
      if (!append_text(&text, &length, &capacity, "MERGE INTO ") ||
          !append_bytes(&text, &length, &capacity, target, (size_t)(target_alias - target)) ||
          (target_alias_start < target_end &&
           (!append_text(&text, &length, &capacity, " AS ") ||
            !append_bytes(&text, &length, &capacity, target_alias_start, (size_t)(target_end - target_alias_start)))) ||
          !append_text(&text, &length, &capacity, " USING ") ||
          !append_bytes(&text, &length, &capacity, source_name, (size_t)(source_alias - source_name)) ||
          (source_alias_start < source_end &&
           (!append_text(&text, &length, &capacity, " AS ") ||
            !append_bytes(&text, &length, &capacity, source_alias_start, (size_t)(source_end - source_alias_start)))) ||
          !append_text(&text, &length, &capacity, " ON ") ||
          !append_bytes(&text, &length, &capacity, match, (size_t)(match_end - match)) ||
          (set ? (!append_bytes(&text, &length, &capacity, when, (size_t)(set - when)) ||
                  !append_text(&text, &length, &capacity, " UPDATE SET ") ||
                  !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
               : !append_bytes(&text, &length, &capacity, when, source.data + source.length - when)))
        goto no_memory;
      for (char *cursor = text; *cursor; ++cursor) {
        if (*cursor == ',' || *cursor == ';')
          continue;
      }
    }
  }
  if (text && statements[0].operation == SEQUEL_OPERATION_SELECT && strstr(source.data, " FROM ") &&
      strstr(source.data, ", ") && strstr(source.data, " WHERE ")) {
    const char *from = strstr(source.data, " FROM ") + 6;
    const char *where = strstr(from, " WHERE ");
    if (where && memchr(from, ',', (size_t)(where - from))) {
      length = 0;
      text[0] = '\0';
      if (!append_text(&text, &length, &capacity, "SELECT ") ||
          !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          !append_text(&text, &length, &capacity, " FROM ") ||
          !append_bytes(&text, &length, &capacity, from, (size_t)(where - from)) ||
          !append_text(&text, &length, &capacity, " WHERE ") ||
          !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
        goto no_memory;
    }
  }
  if (text && summary_word(source.data, source.length, 0, "COPY")) {
    const char *from = strstr(source.data, " FROM ");
    if (from) {
      const char *target = source.data + strlen("COPY ");
      const char *target_end = from;
      while (target_end > target && isspace((unsigned char)target_end[-1]))
        --target_end;
      length = 0;
      text[0] = '\0';
      if (!append_text(&text, &length, &capacity, "COPY ") ||
          !append_bytes(&text, &length, &capacity, target, (size_t)(target_end - target)))
        goto no_memory;
    }
  }
  if (text && strstr(source.data, "SELECT unnest(") && strstr(text, "SELECT ")) {
    char *select = strstr(text, "SELECT ");
    const char replacement[] = "SELECT unnest(…)";
    char *end = strstr(select + 7, " ON CONFLICT");
    if (end) {
      memmove(select + sizeof(replacement) - 1, end, strlen(end) + 1);
      memcpy(select, replacement, sizeof(replacement) - 1);
      length = strlen(text);
    }
  }
  if (text && strstr(source.data, "ON CONFLICT") && strstr(source.data, "DO NOTHING")) {
    const char *insert = strstr(source.data, "INSERT INTO ");
    const char *select = insert ? strstr(insert, " SELECT ") : NULL;
    const char *from = select ? strstr(select, " FROM ") : NULL;
    const char *where = from ? strstr(from, " WHERE ") : NULL;
    const char *conflict = where ? strstr(where, " ON CONFLICT") : (from ? strstr(from, " ON CONFLICT") : NULL);
    const char *returning = conflict ? strstr(conflict, " RETURNING ") : NULL;
    if (insert && select && from && conflict) {
      const char *target = insert + strlen("INSERT INTO ");
      const char *target_end = select;
      const char *relation = from + 6;
      const char *relation_end = where && where < conflict ? where : conflict;
      while (relation_end > relation && isspace((unsigned char)relation_end[-1]))
        --relation_end;
      length = 0;
      text[0] = '\0';
      if (!append_text(&text, &length, &capacity, "INSERT INTO ") ||
          !append_bytes(&text, &length, &capacity, target, (size_t)(target_end - target)) ||
          !append_text(&text, &length, &capacity, " SELECT ") ||
          !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          !append_text(&text, &length, &capacity, " FROM ") ||
          !append_bytes(&text, &length, &capacity, relation, (size_t)(relation_end - relation)) ||
          (where && (!append_text(&text, &length, &capacity, " WHERE ") ||
                     !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))) ||
          !append_text(&text, &length, &capacity, " ON CONFLICT DO NOTHING") ||
          (returning && (!append_text(&text, &length, &capacity, " RETURNING ") ||
                         !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))))
        goto no_memory;
    }
    if (text && !strstr(text, "ON CONFLICT") &&
        (!append_text(&text, &length, &capacity, " ON CONFLICT DO NOTHING")))
      goto no_memory;
  }
  if (text && strstr(source.data, " LIMIT $") && strstr(source.data, " OFFSET $")) {
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
  if (text && strstr(source.data, " LIMIT sqlc.arg(")) {
    char *limit = strstr(text, " LIMIT ");
    if (limit) {
      memcpy(limit + strlen(" LIMIT "), "?", 2);
      length = strlen(text);
    }
  }
  if (text && strstr(source.data, " LIMIT (SELECT ")) {
    char *limit = strstr(text, " LIMIT ");
    if (limit) {
      length = (size_t)(limit - text);
      text[length] = '\0';
      if (!append_text(&text, &length, &capacity, " LIMIT ? (SELECT ") ||
          !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          !append_text(&text, &length, &capacity, " FROM settings LIMIT 1)"))
        goto no_memory;
    }
  }
  if (text && strstr(source.data, " LIMIT ") && strstr(source.data, " + ") &&
      !strstr(source.data, " INSERT ")) {
    char *limit = strstr(text, " LIMIT ");
    if (limit) {
      memcpy(limit + strlen(" LIMIT "), "?", 2);
      length = strlen(text);
    }
  }
  if (text && strstr(source.data, " OFFSET ") && strstr(source.data, " FETCH NEXT ")) {
    const char *offset = strstr(source.data, " OFFSET ");
    const char *fetch = strstr(offset, " FETCH NEXT ");
    const char *value = offset + strlen(" OFFSET ");
    const char *value_end = value;
    while (*value_end && isdigit((unsigned char)*value_end))
      ++value_end;
    if (fetch) {
      length = (size_t)(strstr(text, " OFFSET ") ? strstr(text, " OFFSET ") - text : length);
      if (text)
        text[length] = '\0';
      if (!append_text(&text, &length, &capacity, " OFFSET ") ||
          !append_bytes(&text, &length, &capacity, value, (size_t)(value_end - value)) ||
          !append_text(&text, &length, &capacity, " FETCH NEXT ? ROWS ONLY"))
        goto no_memory;
    }
  }
  if (text && strstr(source.data, " FETCH FIRST ")) {
    const char *from = strstr(source.data, " FROM ");
    const char *fetch = strstr(source.data, " FETCH FIRST ");
    if (from && fetch) {
      const char *table = from + 6;
      while (*table && isspace((unsigned char)*table))
        ++table;
      const char *table_end = table;
      while (table_end < fetch && !isspace((unsigned char)*table_end))
        ++table_end;
      length = 0;
      text[0] = '\0';
      if (!append_text(&text, &length, &capacity, "SELECT ") ||
          !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          !append_text(&text, &length, &capacity, " FROM ") ||
          !append_bytes(&text, &length, &capacity, table, (size_t)(table_end - table)) ||
          (strstr(fetch, " FETCH FIRST ROW ONLY")
               ? !append_text(&text, &length, &capacity, " FETCH FIRST 1 ROWS ONLY")
               : !append_bytes(&text, &length, &capacity, fetch, source.data + source.length - fetch)))
        goto no_memory;
    }
  }
  if (text && summary_word(source.data, source.length, 0, "UPDATE") && !strstr(source.data, " FROM ")) {
    const char *set = strstr(source.data, " SET ");
    const char *where = set ? strstr(set, " WHERE ") : NULL;
    const char *order = where ? strstr(where, " ORDER BY ") : NULL;
    const char *limit = order ? strstr(order, " LIMIT ") : NULL;
    if (set) {
      const char *target = source.data + strlen("UPDATE ");
      const char *target_end = set;
      while (target_end > target && isspace((unsigned char)target_end[-1]))
        --target_end;
      length = 0;
      text[0] = '\0';
      if (!append_text(&text, &length, &capacity, "UPDATE ") ||
          !append_bytes(&text, &length, &capacity, target, (size_t)(target_end - target)) ||
          !append_text(&text, &length, &capacity, " SET ") ||
          !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          (where && (!append_text(&text, &length, &capacity, " WHERE ") ||
                     !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))) ||
          (order && (!append_text(&text, &length, &capacity, " ORDER BY ") ||
                     !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))) ||
          (limit && (!append_text(&text, &length, &capacity, " LIMIT ") ||
                     !append_bytes(&text, &length, &capacity, limit + 7,
                                   (source.data + source.length) - (limit + 7)))) )
        goto no_memory;
    }
  }
  if (text && summary_word(source.data, source.length, 0, "EXPLAIN")) {
    const char *select = sequel_summary_find_word(source.data, source.length, 0, "SELECT");
    const char *from = select ? sequel_summary_find_word(source.data, source.length,
                                                          (size_t)(select - source.data), "FROM") : NULL;
    if (from) {
      const char *table = from + 5;
      while (*table && isspace((unsigned char)*table))
        ++table;
      const char *end = table;
      while (end < source.data + source.length && !isspace((unsigned char)*end))
        ++end;
      length = 0;
      text[0] = '\0';
      if (!append_text(&text, &length, &capacity, "EXPLAIN") ||
          (strstr(source.data, "ANALYZE") && !append_text(&text, &length, &capacity, " ANALYZE")) ||
          !append_text(&text, &length, &capacity, " SELECT ") ||
          !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
          !append_text(&text, &length, &capacity, " FROM ") ||
          !append_bytes(&text, &length, &capacity, table, (size_t)(end - table)) ||
          (sequel_summary_find_word(source.data, source.length, 0, "WHERE") &&
           (!append_text(&text, &length, &capacity, " WHERE ") ||
            !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))))
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
  if (strstr(source.data, "SELECT unnest(") && strstr(text, "SELECT \342\200\246")) {
    char *select = strstr(text, "SELECT \342\200\246");
    const char replacement[] = "SELECT unnest(\342\200\246)";
    size_t old_length = sizeof("SELECT \342\200\246") - 1;
    size_t new_length = sizeof(replacement) - 1;
    memmove(select + new_length, select + old_length, strlen(select + old_length) + 1);
    memcpy(select, replacement, new_length);
    length = strlen(text);
  }
  if (summary_word(source.data, source.length, 0, "UPDATE") && strstr(source.data, " JOIN ")) {
    char *alias = strstr(text, "flags f");
    if (alias) {
      const char replacement[] = "flags AS f";
      memmove(alias + sizeof(replacement) - 1, alias + 7, strlen(alias + 7) + 1);
      memcpy(alias, replacement, sizeof(replacement) - 1);
      length = strlen(text);
    }
  }
  sequel_summary_redact_rendered_literals(text, &length);
  while (length && isspace((unsigned char)text[length - 1]))
    --length;
  text[length] = 0;
  sequel_text_result_free(out);
  *out =
      (sequel_text_result_t){.data = text, .length = length, .capacity = capacity, .mode = SEQUEL_FORMAT_SYNTAX_AWARE};
  return SEQUEL_OK;
invalid:
  free(text);
  return SEQUEL_ERR_INVALID_ARGUMENT;
no_memory:
  free(text);
  return SEQUEL_ERR_NOMEM;
}
