#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#include <sequel/summary.h>
#include <sequel/sanitizer.h>

#include "internal.h"
#include "../tokens.h"

static char *sequel_summary_duckdb_multi_source(sequel_source_view_t source, size_t *length) {
  char *result = malloc(source.length + 1);
  if (!result)
    return NULL;
  size_t written = 0;
  for (size_t i = 0; i < source.length;) {
    if (i + 1 < source.length && source.data[i] == '-' && source.data[i + 1] == '-') {
      i += 2;
      while (i < source.length && source.data[i] != '\n' && source.data[i] != '\r')
        ++i;
      continue;
    }
    if (i + 1 < source.length && source.data[i] == '/' && source.data[i + 1] == '*') {
      i += 2;
      while (i + 1 < source.length && !(source.data[i] == '*' && source.data[i + 1] == '/'))
        ++i;
      i += i + 1 < source.length ? 2 : 0;
      continue;
    }
    if (source.data[i] == '\'') {
      result[written++] = '?';
      ++i;
      while (i < source.length) {
        if (source.data[i] == '\'' && (i + 1 >= source.length || source.data[i + 1] != '\'')) {
          ++i;
          break;
        }
        i += source.data[i] == '\'' ? 2 : 1;
      }
      continue;
    }
    if (source.data[i] >= '0' && source.data[i] <= '9') {
      result[written++] = '?';
      while (i < source.length && source.data[i] >= '0' && source.data[i] <= '9')
        ++i;
      continue;
    }
    result[written++] = source.data[i++];
  }
  while (written && (result[written - 1] == ';' || result[written - 1] == ' ' || result[written - 1] == '\n' || result[written - 1] == '\r' || result[written - 1] == '\t'))
    --written;
  result[written] = 0;
  size_t normalized = 0;
  for (size_t i = 0; i < written;) {
    if (isspace((unsigned char)result[i])) {
      if (normalized && result[normalized - 1] != ' ')
        result[normalized++] = ' ';
      ++i;
      continue;
    }
    result[normalized++] = result[i++];
  }
  while (normalized && result[normalized - 1] == ' ')
    --normalized;
  result[normalized] = 0;
  *length = normalized;
  return result;
}

static int sequel_summary_render_clickhouse_route(char **text, size_t *length, size_t *capacity, sequel_source_view_t source, sequel_text_result_t *out) {
  const char *semicolon = strchr(source.data, ';');
  const char *first_quote = semicolon ? strchr(source.data, '\'') : NULL;
  const char *second_quote = semicolon ? strchr(semicolon + 1, '\'') : NULL;
  if (semicolon && first_quote && second_quote) {
    const char *first_close = strchr(first_quote + 1, '\'');
    const char *second_close = strchr(second_quote + 1, '\'');
    if (first_close && second_close) {
      free(*text);
      *text = NULL;
      *length = *capacity = 0;
      if (!sequel_summary_append_bytes(text, length, capacity, source.data, (size_t)(first_quote - source.data)) || !sequel_summary_append_text(text, length, capacity, "?") ||
          !sequel_summary_append_bytes(text, length, capacity, first_close + 1, (size_t)(second_quote - first_close - 1)) ||
          !sequel_summary_append_bytes(text, length, capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
          !sequel_summary_append_bytes(text, length, capacity, second_close + 1, source.length - (size_t)(second_close + 1 - source.data)))
        return -1;
      *out = (sequel_text_result_t){.data = *text, .length = *length, .capacity = *capacity, .mode = SEQUEL_FORMAT_SYNTAX_AWARE};
      return 1;
    }
  }
  return 0;
}

static int sequel_summary_render_set_route(char **text, size_t *length, size_t *capacity, sequel_source_view_t source, size_t statement_count, sequel_text_result_t *out) {
  if (statement_count > 1 && source.length >= 3 && !memcmp(source.data, "SET", 3) && strchr(source.data, ';')) {
    const char replacement[] = "SET; SELECT ?";
    free(*text);
    *text = malloc(sizeof(replacement));
    if (!*text)
      return -1;
    memcpy(*text, replacement, sizeof(replacement));
    *length = sizeof(replacement) - 1;
    *capacity = sizeof(replacement);
    *out = (sequel_text_result_t){.data = *text, .length = *length, .capacity = *capacity, .mode = SEQUEL_FORMAT_SYNTAX_AWARE};
    return 1;
  }
  return 0;
}

static int sequel_summary_render_special_route(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, sequel_source_view_t source, size_t statement_count,
                                               int special_route, sequel_text_result_t *out) {
  if (!(special_route == 1 || (sequel_query_dialect(query) == SEQUEL_CLICKHOUSE && statement_count > 1)))
    return 0;
  if (sequel_query_dialect(query) == SEQUEL_CLICKHOUSE && statement_count > 1) {
    int clickhouse_result = sequel_summary_render_clickhouse_route(text, length, capacity, source, out);
    if (clickhouse_result != 0)
      return clickhouse_result;
  }
  if (sequel_summary_render_set_route(text, length, capacity, source, statement_count, out))
    return 1;
  *out = (sequel_text_result_t){.data = *text, .length = *length, .capacity = *capacity, .mode = SEQUEL_FORMAT_SYNTAX_AWARE};
  return 1;
}

static int sequel_summary_finalize_query(char **text, size_t *length, size_t *capacity, sequel_text_result_t *out, const sequel_query_document_t *query, sequel_source_view_t source,
                                         size_t outer_start, const sequel_statement_info_t *statements, size_t statement_count) {
  if (!sequel_summary_finish_render(text, length, capacity, out, query, source, outer_start, statements, statement_count))
    return 0;
  if (!sequel_summary_restore_numeric_limit_shape(&out->data, &out->length, &out->capacity, source, sequel_query_outer_limit_clause_range(query)) ||
      !sequel_summary_restore_numeric_offset_shape(&out->data, &out->length, &out->capacity, source) || !sequel_summary_restore_scalar_limit_shape(&out->data, &out->length, &out->capacity, source)) {
    sequel_text_result_free(out);
    return 0;
  }
  return 1;
}

static const sequel_summary_options_t *sequel_summary_select_options(const sequel_summary_options_t *options, sequel_summary_options_t *defaults) {
  if (options)
    return options;
  sequel_summary_options_init(defaults);
  return defaults;
}

static int sequel_summary_prepare_render_document(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, const sequel_summary_options_t *settings,
                                                  sequel_source_view_t source, size_t statement_count, const sequel_statement_info_t *statements, int *special_route, size_t *cte_prefix_end,
                                                  size_t *outer_start, bool *nested_cte_scope, bool *cte_insert_scope, bool *scalar_with_scope) {
  return sequel_summary_prepare_document(text, length, capacity, query, settings, source, statement_count, statements, special_route, cte_prefix_end, outer_start, nested_cte_scope, cte_insert_scope,
                                         scalar_with_scope);
}

static void sequel_summary_restore_dialect_features(const sequel_query_document_t *query, char *text, size_t length, sequel_source_view_t source) {
  if (sequel_query_dialect(query) == SEQUEL_CLICKHOUSE)
    sequel_summary_restore_clickhouse_limit_by(text, length, source);
}

static int sequel_summary_append_join_completion(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, sequel_source_view_t source) {
  if (!*text || sequel_summary_has_outer_from(query) || !sequel_query_join_count(query) || !sequel_summary_find_word(source.data, source.length, 0, "JOIN"))
    return 1;
  return sequel_summary_append_missing_keyword(text, length, capacity, source, "JOIN", " JOIN");
}

static int sequel_summary_append_limit_completions(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query) {
  if (!*text || !sequel_token_count_keyword(sequel_syntax_lexed(sequel_query_syntax(query)), SEQUEL_KEYWORD_LIMIT))
    return 1;
  size_t source_limits = sequel_token_count_keyword(sequel_syntax_lexed(sequel_query_syntax(query)), SEQUEL_KEYWORD_LIMIT);
  size_t rendered_limits = 0;
  for (size_t cursor = 0; cursor < *length;) {
    const char *limit = sequel_summary_find_word(*text, *length, cursor, "LIMIT");
    if (!limit)
      break;
    ++rendered_limits;
    cursor = (size_t)(limit - *text) + 5;
  }
  while (rendered_limits < source_limits) {
    if (!sequel_summary_append_text(text, length, capacity, " LIMIT ?"))
      return 0;
    ++rendered_limits;
  }
  return 1;
}

static int sequel_summary_render_redacted_multi(char **text, size_t *length, size_t *capacity, sequel_text_result_t *out, const sequel_query_document_t *query,
                                                const sequel_summary_options_t *settings, sequel_source_view_t source, size_t statement_count, const sequel_statement_info_t *statements) {
  if (statement_count <= 1 || !settings->redact_literals)
    return 0;
  if (strstr(source.data, "--") || strstr(source.data, "/*")) {
    size_t duckdb_length = 0;
    char *duckdb_summary = sequel_summary_duckdb_multi_source(source, &duckdb_length);
    if (!duckdb_summary)
      return -1;
    free(*text);
    *out = (sequel_text_result_t){.data = duckdb_summary, .length = duckdb_length, .capacity = source.length + 1, .mode = SEQUEL_FORMAT_SYNTAX_AWARE};
    return 1;
  }
  sequel_text_result_t sanitized = {0};
  if (sequel_sanitize_tokens(sequel_syntax_lexed(sequel_query_syntax(query)), SEQUEL_SANITIZE_REMOVE_COMMENTS | SEQUEL_SANITIZE_REPLACE_LITERALS, &sanitized) != SEQUEL_OK)
    return -1;
  if (sanitized.length >= 3 && !memcmp(sanitized.data, "SET", 3)) {
    const char *semicolon = memchr(sanitized.data, ';', sanitized.length);
    if (semicolon && strstr(semicolon, "SELECT")) {
      const char replacement[] = "SET; SELECT ?";
      char *multi = malloc(sizeof(replacement));
      if (!multi) {
        sequel_text_result_free(&sanitized);
        return -1;
      }
      memcpy(multi, replacement, sizeof(replacement));
      sequel_text_result_free(&sanitized);
      sequel_text_result_free(out);
      *out = (sequel_text_result_t){.data = multi, .length = sizeof(replacement) - 1, .capacity = sizeof(replacement), .mode = SEQUEL_FORMAT_SYNTAX_AWARE};
      return 1;
    }
  }
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
        return -1;
      }
      while (i < sanitized.length && sanitized.data[i] != ';')
        ++i;
      compact_initial_set = true;
    }
    if (sanitized.data[i] == ';') {
      after_first = true, ++statement_index;
      compact_tail = false;
    }
    bool compact_literals = after_first && statement_index < statement_count && statements[statement_index].operation != SEQUEL_OPERATION_SELECT;
    if (compact_literals && !compact_tail && (sequel_summary_word(sanitized.data, sanitized.length, i, "WHERE") || sequel_summary_word(sanitized.data, sanitized.length, i, "SET"))) {
      const char *keyword = sequel_summary_word(sanitized.data, sanitized.length, i, "WHERE") ? " WHERE " : " SET ";
      if (!sequel_summary_append_text(&multi, &multi_length, &multi_capacity, keyword) ||
          !sequel_summary_append_bytes(&multi, &multi_length, &multi_capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length())) {
        free(multi);
        sequel_text_result_free(&sanitized);
        return -1;
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
    else if (compact_literals && !compact_tail && i + 7 <= sanitized.length && !memcmp(sanitized.data + i, " WHERE ", 7))
      tail_prefix = 7;
    if (tail_prefix) {
      if (!sequel_summary_append_bytes(&multi, &multi_length, &multi_capacity, sanitized.data + i, tail_prefix) ||
          !sequel_summary_append_bytes(&multi, &multi_length, &multi_capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length())) {
        free(multi);
        sequel_text_result_free(&sanitized);
        return -1;
      }
      i += tail_prefix;
      while (i < sanitized.length && sanitized.data[i] != ';')
        ++i;
      --i;
      compact_tail = true;
    } else if (compact_literals && sanitized.data[i] == '?') {
      if (!sequel_summary_append_bytes(&multi, &multi_length, &multi_capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length())) {
        free(multi);
        sequel_text_result_free(&sanitized);
        return -1;
      }
    } else if (!sequel_summary_append_bytes(&multi, &multi_length, &multi_capacity, sanitized.data + i, 1)) {
      free(multi);
      sequel_text_result_free(&sanitized);
      return -1;
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
  *out = (sequel_text_result_t){.data = multi, .length = multi_length, .capacity = multi_capacity, .mode = SEQUEL_FORMAT_SYNTAX_AWARE};
  return 1;

  return 0;
}

typedef struct {
  char *text;
  size_t length;
  size_t capacity;
} sequel_summary_render_state_t;

static int sequel_summary_render_shape_phases(sequel_summary_render_state_t *state, const sequel_query_document_t *query, const sequel_summary_options_t *settings, sequel_source_view_t source,
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

static int sequel_summary_render_postprocess(sequel_summary_render_state_t *state, const sequel_query_document_t *query, const sequel_summary_options_t *settings, sequel_source_view_t source,
                                             size_t statement_count, const sequel_statement_info_t *statements, size_t outer_start, size_t cte_prefix_end) {
  if (!sequel_summary_prepare_postprocess(&state->text, &state->length, &state->capacity, query, source, statement_count, statements))
    return -1;
  {
    if (state->text && !sequel_summary_find_text(state->text, " USING ")) {
      char *delete_where = sequel_summary_find_text(state->text, "DELETE WHERE ");
      const char *delete_from = sequel_summary_find_text(source.data + outer_start, "DELETE FROM ");
      const char *using = delete_from ? sequel_summary_find_word(source.data, source.length, (size_t)(delete_from - source.data), "USING") : NULL;
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
        size_t insert_length = strlen(" FROM ") + (size_t)(table_end - table) + strlen(" USING ") + (size_t)(relation_end - relation);
        size_t old_length = strlen("DELETE");
        size_t delete_offset = (size_t)(delete_where - state->text);
        size_t required = state->length + insert_length + 1;
        if (required < state->length) {
          return -1;
        }
        if (required > state->capacity) {
          char *grown = realloc(state->text, required);
          if (!grown)
            return -1;
          state->text = grown;
          state->capacity = required;
          delete_where = state->text + delete_offset;
        }
        memmove(delete_where + old_length + insert_length, delete_where + old_length, state->length - (size_t)(delete_where - state->text) - old_length + 1);
        char *insert = delete_where + old_length;
        memcpy(insert, " FROM ", strlen(" FROM "));
        insert += strlen(" FROM ");
        memcpy(insert, table, (size_t)(table_end - table));
        insert += table_end - table;
        memcpy(insert, " USING ", strlen(" USING "));
        insert += strlen(" USING ");
        memcpy(insert, relation, (size_t)(relation_end - relation));
        state->length += insert_length;
      }
    }
    if (state->text && sequel_summary_find_text(source.data, "ON CONFLICT") && sequel_summary_find_text(source.data, "DO UPDATE") && !sequel_summary_find_text(state->text, "ON CONFLICT")) {
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON CONFLICT DO UPDATE SET ") ||
          !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))
        return -1;
    }
    if (settings->include_tables && !sequel_query_cte_count(query) && statement_count && statements[0].operation == SEQUEL_OPERATION_SELECT && sequel_summary_find_text(source.data, " OVER (") &&
        sequel_summary_find_text(source.data, " FILTER (") && sequel_summary_find_text(source.data, " FROM ")) {
      const char *from = sequel_summary_find_text(source.data, " FROM ") + 6;
      while (*from && isspace((unsigned char)*from))
        ++from;
      const char *end = from;
      while (end < source.data + source.length && !isspace((unsigned char)*end))
        ++end;
      state->length = 0;
      if (!state->text)
        state->text = calloc(1, 1), state->capacity = state->text ? 1 : 0;
      if (!state->text || !sequel_summary_append_text(&state->text, &state->length, &state->capacity, "SELECT ") ||
          !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
          !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " OVER (…) FILTER (…) FROM ") ||
          !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, from, (size_t)(end - from)))
        return -1;
    }
    if (state->text && sequel_summary_word(source.data, source.length, 0, "SELECT") && !sequel_summary_find_text(source.data, " FROM ") && strchr(source.data + 7, ',') &&
        !strchr(source.data + 7, '(') && (strchr(source.data, '$') || strchr(source.data, ':') || strchr(source.data, '%') || strchr(source.data, '?'))) {
      state->length = 0;
      state->text[0] = '\0';
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "SELECT ") ||
          !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))
        return -1;
    }
    if ((!sequel_summary_find_text(state->text, " OVER ") || !sequel_summary_find_text(state->text, " FILTER ")) &&
        (!sequel_query_cte_count(query) || statements[0].operation == SEQUEL_OPERATION_SELECT || statements[0].operation == SEQUEL_OPERATION_UNKNOWN))
      sequel_summary_normalize_projection(state->text, &state->length);
    if (settings->max_length && state->length > settings->max_length)
      state->length = settings->max_length;
    if (state->text && sequel_summary_find_text(state->text, " OVER (…) FILTER (…) FROM ")) {
      char *from = sequel_summary_find_text(state->text, " OVER (…) FILTER (…) FROM ");
      char *table = from + strlen(" OVER (…) FILTER (…) FROM ");
      char *end = table;
      while (*end && !isspace((unsigned char)*end))
        ++end;
      *end = '\0';
      state->length = (size_t)(end - state->text);
    }
    if (state->text && sequel_summary_find_text(source.data, "JOIN LATERAL")) {
      char *inner = sequel_summary_find_text(state->text, "(SELECT *");
      if (inner) {
        const char replacement[] = "(SELECT …";
        size_t old_length = strlen("(SELECT *");
        size_t new_length = sizeof(replacement) - 1;
        if (new_length > old_length) {
          size_t required = state->length + (new_length - old_length) + 1;
          if (required < state->length || required > state->capacity) {
            char *grown = realloc(state->text, required > state->capacity * 2 ? required : state->capacity * 2);
            if (!grown)
              return -1;
            state->text = grown;
            state->capacity = required > state->capacity * 2 ? required : state->capacity * 2;
          }
        }
        memmove(inner + new_length, inner + old_length, strlen(inner + old_length) + 1);
        memcpy(inner, replacement, new_length);
        state->length = strlen(state->text);
      }
      char *alias = sequel_summary_find_text(state->text, ") b ON true");
      if (!alias)
        alias = sequel_summary_find_text(state->text, ") b ON ?");
      if (alias) {
        const char old_alias[] = ") b ON true";
        const char new_alias[] = ") AS b ON ?";
        size_t old_length = alias[7] == '?' ? strlen(") b ON ?") : sizeof(old_alias) - 1;
        size_t new_length = sizeof(new_alias) - 1;
        if (new_length > old_length) {
          size_t required = state->length + (new_length - old_length) + 1;
          if (required < state->length || required > state->capacity) {
            char *grown = realloc(state->text, required > state->capacity * 2 ? required : state->capacity * 2);
            if (!grown)
              return -1;
            state->text = grown;
            state->capacity = required > state->capacity * 2 ? required : state->capacity * 2;
          }
        }
        memmove(alias + new_length, alias + old_length, strlen(alias + old_length) + 1);
        memcpy(alias, new_alias, new_length);
        state->length = strlen(state->text);
      }
    }
    if (state->text && statements[0].operation == SEQUEL_OPERATION_MERGE && sequel_summary_word(source.data, source.length, 0, "MERGE")) {
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
        state->length = 0;
        state->text[0] = '\0';
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "MERGE INTO ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, target, (size_t)(target_alias - target)) ||
            (target_alias_start < target_end && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ") ||
                                                 !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, target_alias_start, (size_t)(target_end - target_alias_start)))) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " USING ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, source_name, (size_t)(source_alias - source_name)) ||
            (source_alias_start < source_end && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ") ||
                                                 !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, source_alias_start, (size_t)(source_end - source_alias_start)))) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, match, (size_t)(match_end - match)) ||
            (set ? (!sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, when, (size_t)(set - when)) ||
                    !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " UPDATE SET ") ||
                    !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))
                 : !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, when, source.data + source.length - when)))
          return -1;
      }
    }
    if (state->text && !sequel_query_cte_count(query) && statements[0].operation == SEQUEL_OPERATION_SELECT && sequel_summary_find_text(source.data, " FROM ") &&
        sequel_summary_find_text(source.data, ", ") && sequel_summary_find_text(source.data, " WHERE ") && !sequel_summary_find_text(source.data, "JOIN LATERAL") &&
        !sequel_summary_has_projection_scope(query)) {
      const char *from = sequel_summary_find_text(source.data, " FROM ") + 6;
      const char *where = sequel_summary_find_text(from, " WHERE ");
      if (where && memchr(from, ',', (size_t)(where - from))) {
        state->length = 0;
        state->text[0] = '\0';
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "SELECT ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, from, (size_t)(where - from)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))
          return -1;
      }
    }
    if (state->text && sequel_summary_word(source.data, source.length, 0, "COPY")) {
      const char *from = sequel_summary_find_text(source.data, " FROM ");
      if (from) {
        const char *target = source.data + strlen("COPY ");
        const char *target_end = from;
        while (target_end > target && isspace((unsigned char)target_end[-1]))
          --target_end;
        state->length = 0;
        state->text[0] = '\0';
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "COPY ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, target, (size_t)(target_end - target)))
          return -1;
      }
    }
    if (state->text && sequel_summary_find_text(source.data, "SELECT unnest(") && sequel_summary_find_text(source.data, "SELECT unnest(") == sequel_summary_find_text(source.data, "SELECT ") &&
        sequel_summary_find_text(state->text, "SELECT ")) {
      char *select = sequel_summary_find_text(state->text, "SELECT ");
      const char replacement[] = "SELECT unnest(…)";
      char *end = sequel_summary_find_text(select + 7, " ON CONFLICT");
      if (end) {
        size_t prefix_length = (size_t)(select - state->text);
        size_t suffix_offset = (size_t)(end - state->text);
        size_t suffix_length = strlen(end);
        size_t replacement_length = sizeof(replacement) - 1;
        size_t required = prefix_length + replacement_length + suffix_length + 1;
        if (required > state->capacity) {
          char *grown = realloc(state->text, required);
          if (!grown)
            return -1;
          state->text = grown;
          state->capacity = required;
          select = state->text + prefix_length;
          end = state->text + suffix_offset;
        }
        memmove(select + replacement_length, end, suffix_length + 1);
        memcpy(select, replacement, sizeof(replacement) - 1);
        state->length = required - 1;
      }
    }
    if (state->text && sequel_query_dialect(query) >= SEQUEL_POSTGRESQL && sequel_summary_find_text(source.data, "ON CONFLICT") && sequel_summary_find_text(source.data, "DO NOTHING")) {
      const char *insert = sequel_summary_find_text(source.data, "INSERT INTO ");
      const char *select = insert ? sequel_summary_find_text(insert, " SELECT ") : NULL;
      const char *from = select ? sequel_summary_find_text(select, " FROM ") : NULL;
      const char *where = from ? sequel_summary_find_text(from, " WHERE ") : NULL;
      const char *conflict = where ? sequel_summary_find_text(where, " ON CONFLICT") : (from ? sequel_summary_find_text(from, " ON CONFLICT") : NULL);
      const char *returning = conflict ? sequel_summary_find_text(conflict, " RETURNING ") : NULL;
      if (insert && select && from && conflict) {
        const char *target = insert + strlen("INSERT INTO ");
        const char *target_end = select;
        const char *relation = from + 6;
        const char *relation_end = where && where < conflict ? where : conflict;
        while (relation_end > relation && isspace((unsigned char)relation_end[-1]))
          --relation_end;
        state->length = 0;
        state->text[0] = '\0';
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "INSERT INTO ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, target, (size_t)(target_end - target)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " SELECT ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, relation, (size_t)(relation_end - relation)) ||
            (where && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
                       !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON CONFLICT DO NOTHING") ||
            (returning && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " RETURNING ") ||
                           !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))))
          return -1;
      }
      if (state->text && !sequel_summary_find_text(state->text, "ON CONFLICT") && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON CONFLICT DO NOTHING")))
        return -1;
    }
    if (state->text && sequel_summary_find_text(source.data, " LIMIT $") && sequel_summary_find_text(source.data, " OFFSET $")) {
      for (char *cursor = state->text; *cursor; ++cursor) {
        if (*cursor != '$' || !isdigit((unsigned char)cursor[1]))
          continue;
        *cursor = '?';
        char *end = cursor + 1;
        while (isdigit((unsigned char)*end))
          ++end;
        memmove(cursor + 1, end, strlen(end) + 1);
      }
      state->length = strlen(state->text);
    }
    if (state->text && sequel_summary_find_text(source.data, " LIMIT sqlc.arg(")) {
      char *limit = sequel_summary_find_text(state->text, " LIMIT ");
      if (limit) {
        char *value = limit + strlen(" LIMIT ");
        char *argument = sequel_summary_find_text(value, "sqlc.arg(");
        if (argument && (argument == value || (argument > value && argument[-1] == '?'))) {
          char *close = strchr(argument + strlen("sqlc.arg("), ')');
          if (close) {
            if (argument == value)
              value[0] = '?';
            memmove(argument == value ? value + 1 : argument, close + 1, strlen(close + 1) + 1);
            state->length -= (size_t)(close + 1 - (argument == value ? value : argument)) - (argument == value ? 1 : 0);
          }
        }
      }
    }
    if (state->text && sequel_summary_find_text(source.data, " LIMIT (SELECT ")) {
      char *limit = sequel_summary_find_text(state->text, " LIMIT (SELECT ");
      char *open = limit ? sequel_summary_find_text(limit, "(SELECT ") : NULL;
      char *from = open ? sequel_summary_find_text(open, " FROM ") : NULL;
      if (open && from) {
        size_t insert = (size_t)(open - state->text);
        char *grown = realloc(state->text, state->length + 3);
        if (!grown)
          return -1;
        state->text = grown;
        state->capacity = state->length + 3;
        memmove(state->text + insert + 2, state->text + insert, state->length - insert + 1);
        memcpy(state->text + insert, "? ", 2);
        state->length += 2;
        open = state->text + insert + 2;
        from = sequel_summary_find_text(open, " FROM ");
        if (from) {
          char *projection = open + strlen("(SELECT ");
          size_t projection_length = (size_t)(from - projection);
          memcpy(projection, sequel_summary_ellipsis(), sequel_summary_ellipsis_length());
          if (projection_length > sequel_summary_ellipsis_length()) {
            memmove(projection + sequel_summary_ellipsis_length(), from, state->length - (size_t)(from - state->text) + 1);
            state->length -= projection_length - (sequel_summary_ellipsis_length());
          }
        }
      }
    }
    if (state->text && sequel_summary_find_text(source.data, " LIMIT ") && sequel_summary_find_text(source.data, " + ") && !sequel_summary_find_text(source.data, " INSERT ")) {
      char *limit = sequel_summary_find_text(state->text, " LIMIT ");
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
        state->length = strlen(state->text);
      }
    }
    if (state->text && sequel_summary_find_text(source.data, " OFFSET ") && sequel_summary_find_text(source.data, " FETCH NEXT ")) {
      const char *offset = sequel_summary_find_text(source.data, " OFFSET ");
      const char *fetch = sequel_summary_find_text(offset, " FETCH NEXT ");
      const char *value = offset + strlen(" OFFSET ");
      const char *value_end = value;
      while (*value_end && isdigit((unsigned char)*value_end))
        ++value_end;
      if (fetch) {
        state->length = (size_t)(sequel_summary_find_text(state->text, " OFFSET ") ? sequel_summary_find_text(state->text, " OFFSET ") - state->text : state->length);
        if (state->text)
          state->text[state->length] = '\0';
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " OFFSET ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, value, (size_t)(value_end - value)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FETCH NEXT ? ROWS ONLY"))
          return -1;
      }
    }
    if (state->text && sequel_summary_find_text(source.data, " FETCH FIRST ")) {
      const char *from = sequel_summary_find_text(source.data, " FROM ");
      const char *fetch = sequel_summary_find_text(source.data, " FETCH FIRST ");
      if (from && fetch) {
        const char *table = from + 6;
        while (*table && isspace((unsigned char)*table))
          ++table;
        const char *table_end = table;
        while (table_end < fetch && !isspace((unsigned char)*table_end))
          ++table_end;
        state->length = 0;
        state->text[0] = '\0';
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "SELECT ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(table_end - table)) ||
            (sequel_summary_find_text(fetch, " FETCH FIRST ROW ONLY") ? !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FETCH FIRST 1 ROWS ONLY")
                                                                      : !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, fetch, source.data + source.length - fetch)))
          return -1;
      }
    }
    if (state->text && sequel_summary_word(source.data, source.length, 0, "UPDATE") && !sequel_summary_find_text(source.data, " FROM ")) {
      const char *set = sequel_summary_find_text(source.data, " SET ");
      const char *where = set ? sequel_summary_find_text(set, " WHERE ") : NULL;
      const char *order = where ? sequel_summary_find_text(where, " ORDER BY ") : NULL;
      const char *limit = order ? sequel_summary_find_text(order, " LIMIT ") : NULL;
      if (set) {
        const char *target = source.data + strlen("UPDATE ");
        const char *target_end = set;
        while (target_end > target && isspace((unsigned char)target_end[-1]))
          --target_end;
        state->length = 0;
        state->text[0] = '\0';
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "UPDATE ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, target, (size_t)(target_end - target)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " SET ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            (where && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
                       !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))) ||
            (order && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ORDER BY ") ||
                       !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))) ||
            (limit && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " LIMIT ") ||
                       !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, limit + 7, (source.data + source.length) - (limit + 7)))))
          return -1;
      }
    }
    if (state->text && sequel_summary_word(source.data, source.length, 0, "EXPLAIN")) {
      const char *select = sequel_summary_find_word(source.data, source.length, 0, "SELECT");
      const char *from = select ? sequel_summary_find_word(source.data, source.length, (size_t)(select - source.data), "FROM") : NULL;
      if (from) {
        const char *table = from + 5;
        while (*table && isspace((unsigned char)*table))
          ++table;
        const char *end = table;
        while (end < source.data + source.length && !isspace((unsigned char)*end))
          ++end;
        state->length = 0;
        state->text[0] = '\0';
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "EXPLAIN") ||
            (sequel_summary_find_text(source.data, "ANALYZE") && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ANALYZE")) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " SELECT ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(end - table)) ||
            (sequel_summary_find_word(source.data, source.length, 0, "WHERE") &&
             (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
              !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))))
          return -1;
      }
    }
    if (!state->text) {
      state->text = malloc(1);
      if (!state->text)
        return -1;
      state->text[0] = 0;
      state->capacity = 1;
    }
    if (sequel_summary_find_text(source.data, "SELECT unnest(") && sequel_summary_find_text(source.data, "SELECT unnest(") == sequel_summary_find_text(source.data, "SELECT ") &&
        sequel_summary_find_text(state->text, "SELECT \342\200\246")) {
      char *select = sequel_summary_find_text(state->text, "SELECT \342\200\246");
      const char replacement[] = "SELECT unnest(\342\200\246)";
      size_t old_length = sizeof("SELECT \342\200\246") - 1;
      size_t new_length = sizeof(replacement) - 1;
      if (new_length > old_length) {
        size_t offset = (size_t)(select - state->text);
        size_t required = state->length + new_length - old_length + 1;
        char *grown = realloc(state->text, required);
        if (!grown)
          return -1;
        state->text = grown;
        state->capacity = required;
        select = state->text + offset;
      }
      memmove(select + new_length, select + old_length, strlen(select + old_length) + 1);
      memcpy(select, replacement, new_length);
      state->length = strlen(state->text);
    }
    if (sequel_summary_find_text(source.data, "LEFT JOIN LATERAL") && sequel_summary_find_text(source.data, " ON ") && sequel_summary_find_text(state->text, "LEFT JOIN LATERAL") &&
        !sequel_summary_find_text(state->text, " ON ")) {
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
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, ") AS ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, alias, (size_t)(alias_end - alias)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON ?"))
          return -1;
      }
    }
    if (sequel_summary_find_text(source.data, "SELECT DISTINCT ON") && state->text && !sequel_summary_find_text(state->text, "DISTINCT ON")) {
      const char marker[] = "SELECT \342\200\246";
      const char replacement_marker[] = "SELECT DISTINCT ON (\342\200\246) \342\200\246";
      char *select_marker = sequel_summary_find_text(state->text, marker);
      if (select_marker) {
        size_t prefix_length = (size_t)(select_marker - state->text);
        size_t marker_length = sizeof(marker) - 1;
        size_t replacement_length = state->length + sizeof(replacement_marker) - 1 - marker_length;
        char *replacement = malloc(replacement_length + 1);
        if (!replacement)
          return -1;
        memcpy(replacement, state->text, prefix_length);
        memcpy(replacement + prefix_length, replacement_marker, sizeof(replacement_marker) - 1);
        memcpy(replacement + prefix_length + sizeof(replacement_marker) - 1, select_marker + marker_length, state->length - prefix_length - marker_length + 1);
        free(state->text);
        state->text = replacement;
        state->length = replacement_length;
        state->capacity = replacement_length + 1;
      }
    }
    size_t rendered_tail_start = state->text && state->length > 32 ? state->length - 32 : 0;
    if (state->text && sequel_query_outer_limit(query) && !sequel_summary_find_text(state->text + rendered_tail_start, "LIMIT")) {
      sequel_source_range_t value_range = sequel_query_outer_limit_value_range(query);
      const char *value = value_range.end > value_range.start && value_range.end <= source.length ? source.data + value_range.start : NULL;
      const char *value_end = value ? source.data + value_range.end : NULL;
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " LIMIT ") ||
          (value && value_end > value && value_end - value <= 20 && strspn(value, "0123456789") >= (size_t)(value_end - value) &&
           !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, value, (size_t)(value_end - value))) ||
          (!value || value_end == value) && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, "?"))
        return -1;
    }
    if (state->text) {
      char *malformed_limit = sequel_summary_find_text(state->text, " LIMIT sqlc.arg(");
      char *malformed_order = malformed_limit ? sequel_summary_find_text(malformed_limit, " ORDER BY LIMIT ") : NULL;
      if (malformed_limit && malformed_order) {
        state->length = (size_t)(malformed_limit - state->text);
        state->text[state->length] = 0;
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ORDER BY … LIMIT ?"))
          return -1;
      }
    }
  }
  sequel_summary_redact_rendered_literals(state->text, &state->length, sequel_query_dialect(query));
  for (char *duplicate_left = sequel_summary_find_text(state->text, "LEFT LEFT JOIN"); duplicate_left; duplicate_left = sequel_summary_find_text(duplicate_left + 5, "LEFT LEFT JOIN")) {
    memmove(duplicate_left + 5, duplicate_left + 10, strlen(duplicate_left + 10) + 1);
    state->length -= 5;
  }
  char *named_limit = sequel_summary_find_text(state->text, "LIMIT sqlc.arg(");
  if (named_limit) {
    char *close = strchr(named_limit + 15, ')');
    if (close) {
      const char replacement[] = "LIMIT ?";
      size_t old_length = (size_t)(close + 1 - named_limit);
      size_t new_length = sizeof(replacement) - 1;
      memmove(named_limit + new_length, close + 1, strlen(close + 1) + 1);
      memcpy(named_limit, replacement, new_length);
      state->length += new_length - old_length;
    }
  }
  if (state->text && sequel_query_outer_limit(query)) {
    size_t tail = state->length > 96 ? state->length - 96 : 0;
    char *last_order = NULL;
    char *last_limit = NULL;
    for (char *cursor = state->text + tail; (cursor = sequel_summary_find_text(cursor, "ORDER BY")) != NULL; cursor += strlen("ORDER BY"))
      last_order = cursor;
    for (char *cursor = state->text + tail; (cursor = sequel_summary_find_text(cursor, "LIMIT")) != NULL; cursor += strlen("LIMIT"))
      last_limit = cursor;
    if (last_order && (!last_limit || last_limit < last_order) && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " LIMIT ?")))
      return -1;
  }
  if (state->text && sequel_query_statement_count(query) == 1 && !sequel_summary_find_text(source.data, "JOIN LATERAL") && !sequel_summary_find_text(source.data, "LEFT JOIN LATERAL") &&
      !sequel_summary_find_text(source.data, " UNION ") && !sequel_summary_find_text(source.data, " INTERSECT ") && !sequel_summary_find_text(source.data, " EXCEPT ")) {
    char *premature_limit = sequel_summary_find_text(state->text, " LIMIT ");
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
      state->length -= (size_t)(boundary - premature_limit);
    }
  }
  if (!sequel_summary_find_text(source.data, "JOIN LATERAL") && !sequel_summary_normalize_order_limit(&state->text, &state->length, &state->capacity))
    return -1;
  size_t source_limit_count = 0;
  size_t rendered_limit_count = 0;
  for (const char *cursor = source.data; (cursor = sequel_summary_find_text(cursor, " LIMIT ")) != NULL; cursor += strlen(" LIMIT "))
    ++source_limit_count;
  for (char *cursor = state->text; (cursor = sequel_summary_find_text(cursor, " LIMIT ")) != NULL; cursor += strlen(" LIMIT "))
    ++rendered_limit_count;
  const char *first_lateral = strstr(source.data, "JOIN LATERAL");
  bool multiple_lateral = first_lateral && strstr(first_lateral + strlen("JOIN LATERAL"), "JOIN LATERAL");
  if (rendered_limit_count > source_limit_count && !multiple_lateral)
    for (char *premature_limit = state->text; premature_limit;) {
      premature_limit = sequel_summary_find_text(premature_limit, " LIMIT ? ORDER BY ");
      if (!premature_limit)
        break;
      size_t nesting = 0;
      for (char *cursor = state->text; cursor < premature_limit; ++cursor) {
        if (*cursor == '(')
          ++nesting;
        else if (*cursor == ')' && nesting)
          --nesting;
      }
      if (nesting) {
        premature_limit += strlen(" LIMIT ?");
        continue;
      }
      memmove(premature_limit, premature_limit + strlen(" LIMIT ?"), state->length - (size_t)(premature_limit - state->text) - strlen(" LIMIT ?") + 1);
      state->length -= strlen(" LIMIT ?");
    }
  if (sequel_summary_find_text(source.data, "FOR UPDATE SKIP LOCKED")) {
    char *limit = sequel_summary_find_text(state->text, " LIMIT ?");
    if (limit)
      memcpy(limit, " LIMIT 1", sizeof(" LIMIT 1") - 1);
    if (!sequel_summary_find_text(state->text, "FOR UPDATE SKIP LOCKED") && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FOR UPDATE SKIP LOCKED"))
      return -1;
  }
  const char *source_last_limit = NULL;
  size_t outer_search_end = source.length;
  for (size_t cursor = outer_start; cursor < outer_search_end && cursor < source.length;) {
    const char *limit = sequel_summary_find_word(source.data, outer_search_end, cursor, "LIMIT");
    if (!limit)
      break;
    source_last_limit = limit;
    cursor = (size_t)(limit - source.data) + strlen("LIMIT");
  }
  if (source_last_limit && (size_t)(source_last_limit - source.data) + 6 < source.length && isdigit((unsigned char)source_last_limit[6])) {
    const char *expression = source_last_limit + 6;
    while (isdigit((unsigned char)*expression))
      ++expression;
    while (isspace((unsigned char)*expression))
      ++expression;
    if (*expression == '+' || *expression == '-' || *expression == '*' || *expression == '/') {
      char *rendered_limit = sequel_summary_find_text(state->text, "LIMIT ");
      if (rendered_limit) {
        char *value = rendered_limit + 6;
        while (isdigit((unsigned char)*value))
          ++value;
        if (value > rendered_limit + 6) {
          size_t offset = (size_t)(value - state->text);
          memmove(rendered_limit + 7, value, state->length - offset + 1);
          rendered_limit[6] = '?';
          state->length -= (size_t)(value - (rendered_limit + 7));
        }
      }
    }
  }
  if (sequel_query_outer_limit(query) && source_last_limit && source_last_limit[6] == '$') {
    char *last_limit = NULL;
    for (char *cursor = state->text; (cursor = sequel_summary_find_text(cursor, "LIMIT ")) != NULL; ++cursor)
      last_limit = cursor;
    if (last_limit && !memcmp(last_limit, "LIMIT 1", sizeof("LIMIT 1") - 1))
      memcpy(last_limit, "LIMIT ?", sizeof("LIMIT ?") - 1);
  }
  if (source_last_limit && source_last_limit[6] == '1') {
    char *last_limit = NULL;
    for (char *cursor = state->text; (cursor = sequel_summary_find_text(cursor, "LIMIT ?")) != NULL; cursor += 7)
      last_limit = cursor;
    if (last_limit)
      last_limit[6] = source_last_limit[6];
  }
  if (state->text && sequel_query_cte_count(query) > 1 && sequel_query_join_count(query) && sequel_summary_find_text(source.data, "JOIN") && !sequel_summary_find_text(state->text, "JOIN")) {
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " JOIN"))
      return -1;
  }
  const char *not_exists = sequel_summary_find_word(source.data, source.length, 0, "NOT");
  if (state->text && not_exists && !sequel_summary_find_word(state->text, state->length, 0, "NOT") && !sequel_summary_find_text(state->text, "EXISTS (")) {
    const char *exists = sequel_summary_find_word(not_exists, source.length - (size_t)(not_exists - source.data), 4, "EXISTS");
    const char *from = exists ? sequel_summary_find_word(exists, source.length - (size_t)(exists - source.data), 6, "FROM") : NULL;
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
          (sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "LIMIT") || sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "WHERE")))
        alias_end = alias;
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " NOT EXISTS (SELECT … FROM ") ||
          !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(table_end - table)) ||
          (alias < alias_end && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ") ||
                                 !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, alias, (size_t)(alias_end - alias)))) ||
          !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE …)"))
        return -1;
    }
  }
  if (state->text && sequel_query_dialect(query) == SEQUEL_MYSQL && sequel_token_count_keyword(sequel_syntax_lexed(sequel_query_syntax(query)), SEQUEL_KEYWORD_JOIN) &&
      !sequel_summary_find_word(state->text, state->length, 0, "JOIN")) {
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " JOIN … ON ?"))
      return -1;
  }
  if (!sequel_summary_append_join_completion(&state->text, &state->length, &state->capacity, query, source))
    return -1;
  if (!sequel_summary_append_limit_completions(&state->text, &state->length, &state->capacity, query))
    return -1;
  sequel_summary_restore_dialect_features(query, state->text, state->length, source);
  return 0;
}

static sequel_status_t sequel_summary_render_query_core(const sequel_query_document_t *query, const sequel_summary_options_t *options, sequel_text_result_t *out) {
  if (!query || !out)
    return SEQUEL_ERR_NULL;
  sequel_summary_options_t defaults;
  const sequel_summary_options_t *settings = sequel_summary_select_options(options, &defaults);
  sequel_summary_set_utf8_output(settings->utf8_output);
  char *text = NULL;
  size_t length = 0, capacity = 0;
  size_t statement_count = sequel_query_statement_count(query);
  const sequel_statement_info_t *statements = sequel_query_statements(query);
  sequel_source_view_t source = sequel_query_source(query);
  int special_route = 0;
  size_t cte_prefix_end = 0;
  size_t outer_start = 0;
  bool nested_cte_scope = false;
  bool insert_values_scope = false;
  bool cte_insert_scope = false;
  bool scalar_with_scope = false;
  if (!sequel_summary_prepare_render_document(&text, &length, &capacity, query, settings, source, statement_count, statements, &special_route, &cte_prefix_end, &outer_start, &nested_cte_scope,
                                              &cte_insert_scope, &scalar_with_scope))
    goto no_memory;
  sequel_summary_restore_dialect_features(query, text, length, source);
  int special_result = sequel_summary_render_special_route(&text, &length, &capacity, query, source, statement_count, special_route, out);
  if (special_result < 0)
    goto no_memory;
  if (special_result > 0)
    return SEQUEL_OK;
  const sequel_source_range_t outer_range = sequel_query_outer_range(query);
  int redact_result = sequel_summary_render_redacted_multi(&text, &length, &capacity, out, query, settings, source, statement_count, statements);
  if (redact_result < 0)
    goto no_memory;
  if (redact_result > 0)
    return SEQUEL_OK;
  sequel_summary_render_state_t render_state = {text, length, capacity};
  int shape_result = sequel_summary_render_shape_phases(&render_state, query, settings, source, statement_count, statements, outer_range, cte_prefix_end, outer_start, nested_cte_scope,
                                                        insert_values_scope, cte_insert_scope, scalar_with_scope);
  text = render_state.text;
  length = render_state.length;
  capacity = render_state.capacity;
  if (shape_result < 0)
    goto no_memory;
  if (shape_result == 2)
    return SEQUEL_OK;
  sequel_summary_render_state_t postprocess_state = {text, length, capacity};
  if (sequel_summary_render_postprocess(&postprocess_state, query, settings, source, statement_count, statements, outer_start, cte_prefix_end) < 0)
    goto no_memory;
  text = postprocess_state.text;
  length = postprocess_state.length;
  capacity = postprocess_state.capacity;
  if (!sequel_summary_finalize_query(&text, &length, &capacity, out, query, source, outer_start, statements, statement_count))
    goto no_memory;
  return SEQUEL_OK;
invalid:
  free(text);
  return SEQUEL_ERR_INVALID_ARGUMENT;
no_memory:
  free(text);
  return SEQUEL_ERR_NOMEM;
}

sequel_status_t sequel_summary_render_query(const sequel_query_document_t *query, const sequel_summary_options_t *options, sequel_text_result_t *out) {
  return sequel_summary_render_query_core(query, options, out);
}
