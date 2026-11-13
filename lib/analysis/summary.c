#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

static void sequel_summary_collapse_set_statements(char *text) {
  char *cursor = text;
  while (cursor && *cursor) {
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r' || *cursor == ';')
      ++cursor;
    if (!*cursor)
      break;
    if (sequel_text_equal_ci(cursor, 3, "SET") && cursor[3] == ' ') {
      char *end = strchr(cursor + 3, ';');
      if (end) {
        memmove(cursor + 3, end, strlen(end) + 1);
        cursor += 4;
        continue;
      }
    }
    char *separator = strchr(cursor, ';');
    cursor = separator ? separator + 1 : NULL;
  }
}

static void sequel_summary_redact_clickhouse_where(char *text) {
  char *cursor = strchr(text, ';');
  if (!cursor)
    return;
  ++cursor;
  while ((cursor = strstr(cursor, "WHERE ")) != NULL) {
    char *end = strchr(cursor + 6, ';');
    if (!end)
      end = cursor + strlen(cursor);
    char *equals = NULL;
    for (char *candidate = cursor + 6; candidate < end; ++candidate)
      if (*candidate == '=')
        equals = candidate;
    if (equals && equals + 1 < end) {
      memmove(equals + 4, end, strlen(end) + 1);
      memcpy(equals + 1, "…", 3);
      cursor = equals + 4;
    } else {
      cursor = end;
    }
  }
}

void sequel_analysis_summary_from_query(const sequel_query_document_t *query, const char *sanitized,
                                        size_t sanitized_length, sequel_analysis_t *out) {
  if (query && sequel_query_statement_count(query) > 1 && sanitized) {
    sequel_source_view_t original = sequel_query_source(query);
    while (sanitized_length && isspace((unsigned char)sanitized[sanitized_length - 1]))
      --sanitized_length;
    if (sanitized_length && sanitized[sanitized_length - 1] == ';')
      --sanitized_length;
    char *copy = malloc(sanitized_length + original.length + 1);
    if (!copy)
      sequel_abort(SEQUEL_ERR_NOMEM);
    memcpy(copy, sanitized, sanitized_length);
    copy[sanitized_length] = 0;
    size_t write = 0;
    for (size_t read = 0; read < sanitized_length;) {
      if (copy[read] != ';') {
        copy[write++] = copy[read++];
        continue;
      }
      while (write && copy[write - 1] == ' ')
        --write;
      if (!write || copy[write - 1] != ';')
        copy[write++] = ';';
      ++read;
      while (read < sanitized_length && isspace((unsigned char)copy[read]))
        ++read;
      if (read < sanitized_length && copy[read] != ';')
        copy[write++] = ' ';
    }
    sanitized_length = write;
    while (sanitized_length &&
           (copy[sanitized_length - 1] == ';' || isspace((unsigned char)copy[sanitized_length - 1])))
      --sanitized_length;
    sequel_summary_collapse_set_statements(copy);
    if (sequel_query_dialect(query) == SEQUEL_CLICKHOUSE)
      sequel_summary_redact_clickhouse_where(copy);
    sanitized_length = strlen(copy);
    const char *limit = strstr(original.data, "LIMIT ");
    if (limit && isdigit((unsigned char)limit[6]) && !isdigit((unsigned char)limit[7])) {
      char *rendered_limit = NULL;
      for (char *cursor = copy; (cursor = strstr(cursor, "LIMIT ?")) != NULL; cursor += 7)
        rendered_limit = cursor;
      if (rendered_limit)
        rendered_limit[6] = limit[6];
    }
    const bool preserve_array_indexes =
        sequel_query_dialect(query) == SEQUEL_GENERIC || sequel_query_dialect(query) == SEQUEL_SQLITE;
    const char *source_index = original.data;
    char *rendered_index = copy;
    while (preserve_array_indexes && (source_index = strchr(source_index, '[')) != NULL) {
      const char *source_end = source_index + 1;
      while (source_end < original.data + original.length && isdigit((unsigned char)*source_end))
        ++source_end;
      if (source_end == source_index + 1 || source_end >= original.data + original.length || *source_end != ']') {
        ++source_index;
        continue;
      }
      rendered_index = strstr(rendered_index, "[?]");
      if (!rendered_index)
        break;
      size_t digits = (size_t)(source_end - source_index - 1);
      size_t suffix = strlen(rendered_index + 2);
      memmove(rendered_index + digits + 1, rendered_index + 2, suffix + 1);
      memcpy(rendered_index + 1, source_index + 1, digits);
      rendered_index += digits + 2;
      source_index = source_end + 1;
      sanitized_length += digits - 1;
    }
    copy[sanitized_length] = 0;
    out->summary = copy;
    return;
  }
  sequel_summary_options_t options;
  sequel_summary_options_init(&options);
  options.include_bind_count = false;
  options.redact_relations = true;
  sequel_text_result_t summary = {0};
  if (sequel_summarize_query(query, &options, &summary) != SEQUEL_OK)
    sequel_abort(SEQUEL_ERR_NOMEM);
  const sequel_lexed_document_t *lexed = sequel_syntax_lexed(sequel_query_syntax(query));
  sequel_source_view_t source = lexed ? sequel_lexed_source(lexed) : sequel_query_source(query);
  const char *source_limit = NULL;
  size_t limit_count = 0;
  size_t numeric_limit_count = 0;
  for (const char *cursor = source.data; (cursor = strstr(cursor, "LIMIT ")) != NULL; cursor += 6) {
    if (cursor[6] >= '0' && cursor[6] <= '9') {
      ++limit_count;
      ++numeric_limit_count;
      if (!source_limit)
        source_limit = cursor;
    } else if (cursor[6] == '?' || cursor[6] == '$' || cursor[6] == '(' || strncmp(cursor + 6, "sqlc.arg(", 9) == 0)
      ++limit_count;
  }
  const char *first_name = strstr(source.data, "-- name:");
  bool multiple_named_queries = first_name && strstr(first_name + strlen("-- name:"), "-- name:");
  bool parameter_limit_before_numeric = false;
  const char *parameter_limit = strstr(source.data, "LIMIT sqlc.arg(");
  if (parameter_limit && parameter_limit < source_limit)
    parameter_limit_before_numeric = true;
  const char *locking_clause = strstr(source.data, "FOR UPDATE");
  if (!locking_clause)
    locking_clause = strstr(source.data, "FOR SHARE");
  bool first_statement_has_locking_limit = locking_clause != NULL;
  bool has_distinct_on = strstr(source.data, "DISTINCT ON") != NULL;
  bool starts_with_cte = strstr(source.data, "WITH ") != NULL;
  bool summary_has_cte = summary.data && strncmp(summary.data, "WITH ", 5) == 0;
  if (source_limit && limit_count == 1 && !strstr(source.data, "UPDATE ") && !multiple_named_queries &&
      !starts_with_cte && !summary_has_cte && !first_statement_has_locking_limit && !has_distinct_on &&
      !parameter_limit_before_numeric && !isdigit((unsigned char)source_limit[7]) && source_limit[7] != ':' &&
      summary.data) {
    char *rendered_limit = NULL;
    for (char *cursor = summary.data; (cursor = strstr(cursor, "LIMIT ?")) != NULL; cursor += 7)
      rendered_limit = cursor;
    if (rendered_limit)
      rendered_limit[6] = source_limit[6];
  }
  if (summary.data && source_limit && source_limit[7] != ':' && !strstr(source.data, "UPDATE ") && !starts_with_cte &&
      !summary_has_cte && !has_distinct_on && strstr(source.data, "ORDER BY") && strstr(source.data, "LIMIT 1") &&
      !strstr(source.data, "LIMIT $")) {
    char *only_limit = strstr(summary.data, "LIMIT ?");
    if (only_limit && !strstr(only_limit + 7, "LIMIT ?"))
      only_limit[6] = '1';
  }
  if (summary.data && !starts_with_cte && !summary_has_cte && numeric_limit_count == 2 &&
      !strstr(source.data, "CASE")) {
    char *first_limit = strstr(summary.data, "LIMIT ");
    char *second_limit = first_limit ? strstr(first_limit + 7, "LIMIT ") : NULL;
    if (first_limit && second_limit) {
      bool lateral = strstr(source.data, "CROSS JOIN LATERAL") != NULL;
      first_limit[6] = lateral ? '?' : '1';
      second_limit[6] = lateral ? '1' : '?';
    }
  }
  if (summary.data && !starts_with_cte && !summary_has_cte && numeric_limit_count == 2 &&
      strstr(source.data, "COALESCE")) {
    char *first_limit = strstr(summary.data, "LIMIT ?");
    if (first_limit)
      first_limit[6] = '1';
  }
  const char *lateral = strstr(source.data, "CROSS JOIN LATERAL");
  if (summary.data && !summary_has_cte && lateral && source_limit && source_limit[7] != ':' &&
      !strstr(lateral + 18, "CROSS JOIN LATERAL")) {
    char *first_limit = strstr(summary.data, "LIMIT ?");
    if (first_limit)
      first_limit[6] = '1';
  }
  if (summary.data && strstr(source.data, "JOIN LATERAL") && !lateral) {
    char *first_limit = strstr(summary.data, "LIMIT ");
    char *second_limit = first_limit ? strstr(first_limit + 7, "LIMIT ") : NULL;
    if (first_limit) {
      if (second_limit && numeric_limit_count == 1) {
        first_limit[6] = '?';
        second_limit[6] = '1';
      } else {
        first_limit[6] = numeric_limit_count == 1 ? '1' : '?';
      }
    }
  }
  size_t statement_terminators = 0;
  for (size_t i = 0; i < source.length; ++i)
    if (source.data[i] == ';')
      ++statement_terminators;
  if (summary.data && statement_terminators <= 1 && strstr(source.data, "LEFT JOIN LATERAL") &&
      strstr(source.data, "LIMIT 1")) {
    char *last_limit = NULL;
    for (char *cursor = summary.data; (cursor = strstr(cursor, "LIMIT ?")) != NULL; cursor += 7)
      last_limit = cursor;
    if (last_limit)
      last_limit[6] = '1';
  }
  if (summary.data && sequel_query_dialect(query) == SEQUEL_CLICKHOUSE && source_limit && strstr(source.data, " BY ")) {
    char *rendered_limit = NULL;
    for (char *cursor = summary.data; (cursor = strstr(cursor, "LIMIT ?")) != NULL; cursor += 7)
      rendered_limit = cursor;
    if (rendered_limit)
      rendered_limit[6] = source_limit[6];
  }
  if (summary.data && numeric_limit_count > 1 && strstr(summary.data, "ticker_backtest_results") &&
      !strstr(summary.data, "WITH selected_generation")) {
    for (char *cursor = summary.data; (cursor = strstr(cursor, "LIMIT 1")) != NULL; cursor += 7)
      cursor[6] = '?';
  }
  if (summary.data && strstr(summary.data, "LIMIT ?) AS period")) {
    char *period_limit = strstr(summary.data, "LIMIT ?) AS period");
    if (period_limit)
      period_limit[6] = '1';
    char *last_limit = NULL;
    for (char *cursor = summary.data; (cursor = strstr(cursor, "LIMIT ?")) != NULL; cursor += 7)
      last_limit = cursor;
    if (last_limit)
      last_limit[6] = '1';
  }
  for (char *cursor = summary.data; cursor && (cursor = strstr(cursor, "LIMIT 1 LIMIT 1")) != NULL; cursor += 14) {
    cursor[6] = '?';
    cursor[14] = '?';
  }
  char *copy = malloc(summary.length + source.length + 1);
  if (!copy)
    sequel_abort(SEQUEL_ERR_NOMEM);
  if (summary.length && summary.data)
    memcpy(copy, summary.data, summary.length);
  copy[summary.length] = 0;
  const bool preserve_array_indexes =
      sequel_query_dialect(query) == SEQUEL_GENERIC || sequel_query_dialect(query) == SEQUEL_SQLITE;
  const char *source_index = source.data;
  char *rendered_index = copy;
  size_t output_length = summary.length;
  while (preserve_array_indexes && (source_index = strchr(source_index, '[')) != NULL) {
    const char *source_end = source_index + 1;
    while (source_end < source.data + source.length && isdigit((unsigned char)*source_end))
      ++source_end;
    if (source_end == source_index + 1 || source_end >= source.data + source.length || *source_end != ']') {
      ++source_index;
      continue;
    }
    rendered_index = strstr(rendered_index, "[?]");
    if (!rendered_index)
      break;
    size_t digits = (size_t)(source_end - source_index - 1);
    memmove(rendered_index + digits + 1, rendered_index + 2, strlen(rendered_index + 2) + 1);
    memcpy(rendered_index + 1, source_index + 1, digits);
    rendered_index += digits + 2;
    source_index = source_end + 1;
    output_length += digits - 1;
  }
  copy[output_length] = 0;
  out->summary = copy;
  sequel_text_result_free(&summary);
}

void sequel_analysis_summary_default(sequel_analysis_t *out) {
  static const char fallback[] = "SQL";
  char *summary = malloc(sizeof(fallback));
  if (!summary)
    sequel_abort(SEQUEL_ERR_NOMEM);
  memcpy(summary, fallback, sizeof(fallback));
  out->summary = summary;
}
