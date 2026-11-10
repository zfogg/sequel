#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  if (settings->include_tables) {
    size_t relation_count = sequel_query_relation_count(query);
    const sequel_relation_info_t *relations = sequel_query_relations(query);
    sequel_source_view_t source = sequel_query_source(query);
    for (size_t i = 0; i < relation_count; ++i) {
      if (!append_text(&text, &length, &capacity, i ? "," : " FROM "))
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
      free(name);
      if (!okay)
        goto no_memory;
    }
  }
  if (settings->include_joins && sequel_query_join_count(query) && !append_text(&text, &length, &capacity, " JOIN"))
    goto no_memory;
  if (settings->include_predicates && sequel_query_predicate_count(query) &&
      !append_text(&text, &length, &capacity, " WHERE …"))
    goto no_memory;
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
