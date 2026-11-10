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
    if (from >= range.end || !append_text(text, length, capacity, "SELECT ") ||
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
    return 1;
  }
  if (summary_word(source, range.end, start, "DELETE")) {
    size_t from = start;
    while (from < range.end && !summary_word(source, range.end, from, "FROM"))
      ++from;
    size_t table = summary_skip_space(source, range.end, from + 4), end = table;
    while (end < range.end && !isspace((unsigned char)source[end]) && source[end] != ')')
      ++end;
    return append_text(text, length, capacity, "DELETE FROM ") &&
           append_bytes(text, length, capacity, source + table, end - table);
  }
  if (summary_word(source, range.end, start, "UPDATE")) {
    size_t table = summary_skip_space(source, range.end, start + 6), end = table;
    while (end < range.end && !isspace((unsigned char)source[end]))
      ++end;
    return append_text(text, length, capacity, "UPDATE ") &&
           append_bytes(text, length, capacity, source + table, end - table) &&
           append_text(text, length, capacity, " SET ") &&
           append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1);
  }
  if (summary_word(source, range.end, start, "INSERT"))
    return append_text(text, length, capacity, "INSERT ") &&
           append_bytes(text, length, capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1);
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
  }
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
  if (settings->include_operation && statement_count) {
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
        size_t from = end;
        while (from < source.length && !summary_word(source.data, source.length, from, "FROM"))
          ++from;
        size_t table = summary_skip_space(source.data, source.length, from + 4), table_end = table;
        while (table_end < source.length && !isspace((unsigned char)source.data[table_end]) &&
               source.data[table_end] != ';')
          ++table_end;
        if (!append_text(&text, &length, &capacity, "SELECT ") ||
            !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1) ||
            !append_text(&text, &length, &capacity, " FROM ") ||
            !append_bytes(&text, &length, &capacity, source.data + table, table_end - table))
          goto no_memory;
      } else if (end == start || !append_bytes(&text, &length, &capacity, source.data + start, end - start))
        goto no_memory;
    }
  }
  if (settings->include_operation && statement_count && statements[0].operation == SEQUEL_OPERATION_SELECT &&
      sequel_query_projection_count(query) && !append_text(&text, &length, &capacity, " "))
    goto no_memory;
  if (settings->include_operation && statement_count && statements[0].operation == SEQUEL_OPERATION_SELECT &&
      sequel_query_projection_count(query)) {
    const sequel_projection_info_t *projection = sequel_query_projections(query);
    char first = projection[0].range.start < source.length ? source.data[projection[0].range.start] : 0;
    bool literal =
        strchr("'?0123456789", first) != NULL || (first == '$' && projection[0].range.start + 1 < source.length &&
                                                  isdigit((unsigned char)source.data[projection[0].range.start + 1]));
    if (literal ? !append_text(&text, &length, &capacity, "?")
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
      while (end < source.length && !isspace((unsigned char)source.data[end]) && source.data[end] != ';')
        ++end;
      if (end > name && (!append_text(&text, &length, &capacity, " INTO ") ||
                         !append_bytes(&text, &length, &capacity, source.data + name, end - name)))
        goto no_memory;
    }
    for (size_t i = 0; i < source.length; ++i)
      if (summary_word(source.data, source.length, i, "VALUES")) {
        if (!append_text(&text, &length, &capacity, " VALUES"))
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
  if (settings->include_tables) {
    size_t relation_count = sequel_query_relation_count(query);
    const sequel_relation_info_t *relations = sequel_query_relations(query);
    sequel_operation_t operation = statement_count ? statements[0].operation : SEQUEL_OPERATION_UNKNOWN;
    const sequel_join_info_t *joins = sequel_query_joins(query);
    size_t rendered_relations = 0;
    for (size_t i = 0; i < relation_count; ++i) {
      if (sequel_query_cte_count(query) && statements[0].operation == SEQUEL_OPERATION_UNKNOWN) {
        size_t name_length = relations[i].name_range.end - relations[i].name_range.start;
        if (name_length && text) {
          bool present = false;
          for (size_t offset = 0; text[offset]; ++offset)
            if (!memcmp(text + offset, source.data + relations[i].name_range.start, name_length)) {
              present = true;
              break;
            }
          if (present) {
            if (!rendered_relations)
              rendered_relations = 1;
            continue;
          }
        }
      }
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
          summary_word(source.data, source.length, relations[i].name_range.start, "LATERAL")) {
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
      if (okay && relations[i].alias_range.end > relations[i].alias_range.start) {
        size_t alias_start = relations[i].alias_range.start;
        size_t alias_end = relations[i].alias_range.end;
        if (alias_end > source.length || alias_start > alias_end)
          okay = 0;
        else
          okay = append_text(&text, &length, &capacity, " AS ") &&
                 append_bytes(&text, &length, &capacity, source.data + alias_start, alias_end - alias_start);
      }
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
  }
  size_t outer_start =
      sequel_query_cte_count(query) ? sequel_query_ctes(query)[sequel_query_cte_count(query) - 1].range.end : 0;
  while (outer_start < source.length &&
         (isspace((unsigned char)source.data[outer_start]) || source.data[outer_start] == ')'))
    ++outer_start;
  bool outer_write =
      statements[0].operation == SEQUEL_OPERATION_DELETE || statements[0].operation == SEQUEL_OPERATION_UPDATE ||
      (sequel_query_cte_count(query) && (summary_word(source.data, source.length, outer_start, "DELETE") ||
                                         summary_word(source.data, source.length, outer_start, "UPDATE")));
  if (settings->include_tables && statement_count && outer_write) {
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
    const sequel_predicate_info_t *predicate = sequel_query_predicates(query);
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
  } else if (settings->include_predicates && statement_count && statements[0].operation != SEQUEL_OPERATION_CREATE) {
    for (size_t i = 0; i < source.length; ++i)
      if (summary_word(source.data, source.length, i, "WHERE")) {
        if (!append_text(&text, &length, &capacity, " WHERE ") ||
            !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1))
          goto no_memory;
        break;
      }
  }
  if (settings->include_tables && statement_count) {
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
  if (settings->include_bind_count) {
    char binds[32];
    snprintf(binds, sizeof(binds), " [binds=%zu]", sequel_query_parameter_count(query));
    if (!append_text(&text, &length, &capacity, binds))
      goto no_memory;
  }
  if (settings->max_length && length > settings->max_length)
    length = settings->max_length;
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
