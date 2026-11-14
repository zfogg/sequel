#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#include <sequel/summary.h>
#include <sequel/sanitizer.h>

#include "../internal.h"
#include "../../tokens.h"

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

void sequel_summary_restore_dialect_features(const sequel_query_document_t *query, char *text, size_t length, sequel_source_view_t source) {
  if (sequel_query_dialect(query) == SEQUEL_CLICKHOUSE)
    sequel_summary_restore_clickhouse_limit_by(text, length, source);
}

int sequel_summary_append_join_completion(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, sequel_source_view_t source) {
  if (!*text || sequel_summary_has_outer_from(query) || !sequel_query_join_count(query) || !sequel_summary_find_word(source.data, source.length, 0, "JOIN"))
    return 1;
  return sequel_summary_append_missing_keyword(text, length, capacity, source, "JOIN", " JOIN");
}

int sequel_summary_append_limit_completions(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query) {
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
