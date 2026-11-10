#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <sequel/summary.h>

void sequel_summary_options_init(sequel_summary_options_t *options) {
  if (options)
    *options = (sequel_summary_options_t){.include_operation = true,
                                          .include_tables = true,
                                          .include_joins = true,
                                          .include_predicates = true,
                                          .include_bind_count = true,
                                          .redact_literals = true,
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

static const char summary_ellipsis[] = "\xE2\x80\xA6";

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
    return "TRANSACTION";
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
  if (settings->include_operation && statement_count &&
      !append_text(&text, &length, &capacity, operation_name(statements[0].operation)))
    goto no_memory;
  if (settings->include_operation && statement_count && statements[0].operation == SEQUEL_OPERATION_SELECT &&
      sequel_query_projection_count(query) &&
      (!append_text(&text, &length, &capacity, " ") ||
       !append_bytes(&text, &length, &capacity, summary_ellipsis, sizeof(summary_ellipsis) - 1)))
    goto no_memory;
  sequel_source_view_t source = sequel_query_source(query);
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
      if (operation == SEQUEL_OPERATION_CREATE) {
        if (text && strstr(text, " INDEX ") && !append_text(&text, &length, &capacity, " ON ..."))
          goto no_memory;
        break;
      }
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
      if (!append_text(&text, &length, &capacity,
                       rendered_relations == 0 ? " FROM " : (join_index != SIZE_MAX ? " JOIN " : ",")))
        goto no_memory;
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
      if (okay && join_index != SIZE_MAX && joins[join_index].has_condition) {
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
        if (condition_end > condition_start)
          okay =
              append_bytes(&text, &length, &capacity, source.data + condition_start, condition_end - condition_start);
      }
      if (okay)
        ++rendered_relations;
      free(name);
      if (!okay)
        goto no_memory;
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
