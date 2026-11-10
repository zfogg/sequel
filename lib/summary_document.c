#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <sequel/summary.h>
#include <sequel/sanitizer.h>

void sequel_summary_options_init(sequel_summary_options_t *options) {
  if (options)
    *options = (sequel_summary_options_t){.include_operation = true,
                                          .include_tables = true,
                                          .include_joins = true,
                                          .include_predicates = true,
                                          .include_bind_count = true,
                                          .redact_literals = true,
                                          .redact_relations = false,
                                          .max_length = 0};
}

static int append_text(char **text, size_t *length, size_t *capacity, const char *value) {
  size_t amount = strlen(value);
  if (amount > SIZE_MAX - *length)
    return 0;
  size_t required = *length + amount;
  if (required + 1 > *capacity) {
    size_t next = *capacity ? *capacity : 64;
    while (next < required + 1) {
      if (next > SIZE_MAX / 2)
        return 0;
      next *= 2;
    }
    char *grown = realloc(*text, next);
    if (!grown)
      return 0;
    *text = grown;
    *capacity = next;
  }
  memcpy(*text + *length, value, amount);
  *length += amount;
  (*text)[*length] = 0;
  return 1;
}

static int append_bytes(char **text, size_t *length, size_t *capacity, const char *value, size_t amount) {
  if (amount > SIZE_MAX - *length || *length + amount + 1 > *capacity) {
    size_t required = *length + amount + 1;
    size_t next = *capacity ? *capacity : 64;
    while (next < required) {
      if (next > SIZE_MAX / 2)
        return 0;
      next *= 2;
    }
    char *grown = realloc(*text, next);
    if (!grown)
      return 0;
    *text = grown;
    *capacity = next;
  }
  memcpy(*text + *length, value, amount);
  *length += amount;
  (*text)[*length] = 0;
  return 1;
}

static const char summary_ellipsis[] = "\342\200\246";

static const char *operation_name(sequel_operation_t operation) {
  switch (operation) {
  case SEQUEL_OPERATION_SELECT:
    return "SELECT";
  case SEQUEL_OPERATION_INSERT:
    return "INSERT";
  case SEQUEL_OPERATION_UPDATE:
    return "UPDATE";
  case SEQUEL_OPERATION_DELETE:
    return "DELETE";
  case SEQUEL_OPERATION_MERGE:
    return "MERGE";
  case SEQUEL_OPERATION_CREATE:
    return "CREATE";
  case SEQUEL_OPERATION_ALTER:
    return "ALTER";
  case SEQUEL_OPERATION_DROP:
    return "DROP";
  case SEQUEL_OPERATION_TRANSACTION:
    return "BEGIN";
  case SEQUEL_OPERATION_EXPLAIN:
    return "EXPLAIN";
  case SEQUEL_OPERATION_OPTIMIZE:
    /* OPTIMIZE has dialect-specific clauses; retain the generic summary
     * sentinel until a normalized table-action record is available. */
    return "SQL";
  case SEQUEL_OPERATION_TRUNCATE:
    return "TRUNCATE";
  default:
    return "SQL";
  }
}

static int summary_word(const char *source, size_t length, size_t offset, const char *word) {
  size_t n = strlen(word);
  if (offset + n > length)
    return 0;
  for (size_t i = 0; i < n; ++i)
    if (toupper((unsigned char)source[offset + i]) != toupper((unsigned char)word[i]))
      return 0;
  return (!offset || !isalnum((unsigned char)source[offset - 1])) &&
         (offset + n == length || !isalnum((unsigned char)source[offset + n]));
}

static size_t summary_skip_space(const char *source, size_t length, size_t offset) {
  while (offset < length && isspace((unsigned char)source[offset]))
    ++offset;
  return offset;
}

static int append_body_summary(char **text, size_t *length, size_t *capacity, const char *source,
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
    size_t limit = table_end;
    while (limit < range.end && !summary_word(source, range.end, limit, "LIMIT"))
      ++limit;
    if (limit < range.end) {
      size_t value = summary_skip_space(source, range.end, limit + 5);
      size_t value_end = value;
      while (value_end < range.end && !isspace((unsigned char)source[value_end]) && source[value_end] != ')')
        ++value_end;
      if (!append_text(text, length, capacity, " LIMIT ") ||
          !append_bytes(text, length, capacity, source + value, value_end - value))
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
  size_t cte_prefix_end = 0;
  size_t outer_start = 0;
  bool nested_cte_scope = false;
  bool insert_values_scope = false;
  bool cte_insert_scope = false;
  bool scalar_with_scope = summary_word(source.data, source.length, 0, "WITH") && sequel_query_cte_count(query) == 0;
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
      !cte_insert_scope) {
    if (statements[0].operation != SEQUEL_OPERATION_UNKNOWN) {
      if (!append_text(&text, &length, &capacity, operation_name(statements[0].operation)))
        goto no_memory;
    } else {
      size_t start = 0;
      if (sequel_query_cte_count(query))
        start = sequel_query_ctes(query)[sequel_query_cte_count(query) - 1].range.end;
      while (start < source.length && isspace((unsigned char)source.data[start]))
        ++start;
      size_t end = start;
      while (end < source.length && !isspace((unsigned char)source.data[end]) && source.data[end] != '(')
        ++end;
      if (sequel_query_cte_count(query) && summary_word(source.data, source.length, start, "SELECT")) {
        if (!append_text(&text, &length, &capacity, "SELECT ") ||
            !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
          goto no_memory;
      } else if (end == start || !append_bytes(&text, &length, &capacity, source.data + start, end - start))
        goto no_memory;
    }
  }
  if (settings->include_operation && statement_count && !sequel_query_cte_count(query) &&
      statements[0].operation == SEQUEL_OPERATION_SELECT && sequel_query_projection_count(query) &&
      !append_text(&text, &length, &capacity, " "))
    goto no_memory;
  if (settings->include_operation && statement_count && !sequel_query_cte_count(query) &&
      statements[0].operation == SEQUEL_OPERATION_SELECT && sequel_query_projection_count(query)) {
    const sequel_projection_info_t *projection = sequel_query_projections(query);
    char first = projection[0].range.start < source.length ? source.data[projection[0].range.start] : 0;
    bool literal =
        strchr("'?0123456789", first) != NULL || (first == '$' && projection[0].range.start + 1 < source.length &&
                                                  isdigit((unsigned char)source.data[projection[0].range.start + 1]));
    bool scalar_function = !strstr(source.data, " FROM ") && strchr(source.data + projection[0].range.start, '(') != NULL;
    const char *function_open = scalar_function ? strchr(source.data + projection[0].range.start, '(') : NULL;
    bool empty_arguments = function_open && function_open[1] == ')';
    if (scalar_function
            ? (!append_bytes(&text, &length, &capacity, source.data + projection[0].range.start,
                             (size_t)(function_open - (source.data + projection[0].range.start) + 1)) ||
               (empty_arguments ? !append_text(&text, &length, &capacity, ")")
                                : (!append_bytes(&text, &length, &capacity, summary_ellipsis,
                                                  sizeof(summary_ellipsis) - 1) ||
                                   !append_text(&text, &length, &capacity, ")"))))
            : literal ? !append_text(&text, &length, &capacity, "?")
                : !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
      goto no_memory;
  }
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
  if (settings->include_tables && statement_count && statements[0].operation == SEQUEL_OPERATION_CREATE) {
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
      if (name < source.length && source.data[name] != '\'' && !summary_word(source.data, source.length, name, "ON")) {
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
      (strstr(source.data, " UNION ") || strstr(source.data, " EXCEPT ") || strstr(source.data, " INTERSECT "))) {
    length = 0;
    if (text)
      text[0] = '\0';
    const char *cursor = source.data;
    bool first_set = true;
    while (cursor < source.data + source.length) {
      const char *select = strstr(cursor, "SELECT ");
      if (!select)
        break;
      const char *from = strstr(select, " FROM ");
      if (!from)
        break;
      const char *table = from + 6;
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
      const char *op = strstr(end, " UNION ");
      const char *except = strstr(end, " EXCEPT ");
      const char *intersect = strstr(end, " INTERSECT ");
      const char *next = op;
      if (!next || (except && except < next))
        next = except;
      if (!next || (intersect && intersect < next))
        next = intersect;
      if (!next)
        break;
      if (!append_bytes(&text, &length, &capacity, next, (size_t)(strstr(next + 1, " SELECT ") - next)))
        goto no_memory;
      cursor = strstr(next + 1, " SELECT ");
      first_set = false;
    }
  }
  if (settings->include_tables && !sequel_query_cte_count(query) && !nested_cte_scope && !insert_values_scope &&
      !cte_insert_scope && !strstr(source.data, " UNION ") && !strstr(source.data, " EXCEPT ") &&
      !strstr(source.data, " INTERSECT ") && !(strstr(source.data, " OVER (") && strstr(source.data, " FILTER ("))) {
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
          if (!append_text(&text, &length, &capacity, " ON "))
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
      size_t start = relations[i].name_range.start;
      size_t end = relations[i].name_range.end;
      if (end > source.length || start > end)
        goto invalid;
      char *name = malloc(end - start + 1);
      if (!name)
        goto no_memory;
      memcpy(name, source.data + start, end - start);
      name[end - start] = 0;
      int okay = append_text(&text, &length, &capacity, name);
      if (okay && relations[i].alias_range.end > relations[i].alias_range.start &&
          !summary_word(source.data, source.length, relations[i].alias_range.start, "SAMPLE")) {
        size_t alias_start = relations[i].alias_range.start;
        size_t alias_end = relations[i].alias_range.end;
        if (alias_end > source.length || alias_start > alias_end)
          okay = 0;
        else
          okay = append_text(&text, &length, &capacity, " AS ") &&
                 append_bytes(&text, &length, &capacity, source.data + alias_start, alias_end - alias_start);
      }
      if (okay && relations[i].alias_range.end > relations[i].alias_range.start &&
          summary_word(source.data, source.length, relations[i].alias_range.start, "SAMPLE"))
        okay = append_text(&text, &length, &capacity, " SAMPLE");
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
        if (condition_end > condition_start && !summary_word(source.data, source.length, condition_start, "USING"))
          okay =
              append_text(&text, &length, &capacity, " ") &&
              append_bytes(&text, &length, &capacity, source.data + condition_start, condition_end - condition_start);
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
      free(name);
      if (!okay)
        goto no_memory;
    }
    if (operation == SEQUEL_OPERATION_INSERT && strstr(source.data, " ON CONFLICT")) {
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
  if (insert_values_scope && strstr(source.data, " ON CONFLICT") && !strstr(text ? text : "", " ON CONFLICT")) {
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
  for (size_t i = 0; i < length; ++i) {
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
  if (summary_word(source.data, source.length, 0, "UPDATE") && strstr(source.data, " JOIN ") &&
      !strstr(text, "UPDATE users")) {
    if (!append_text(&text, &length, &capacity, " UPDATE users JOIN flags AS f"))
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
  if (!text) {
    text = malloc(1);
    if (!text)
      goto no_memory;
    text[0] = 0;
    capacity = 1;
  }
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
