#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <sequel/parser.h>

#include "inspection.h"
#include "inspection_document.h"
#include "inspection_text.h"
#include "model.h"
#include "tokens.h"
#include "util/utf8.h"
#include "util/text.h"

static void sequel_inspection_document_normalize_parameters(sequel_text_result_t *summary) {
  if (!summary || !summary->data)
    return;
  for (size_t i = 0; i + 1 < summary->length; ++i) {
    if (summary->data[i] != '$' || summary->data[i + 1] < '0' || summary->data[i + 1] > '9')
      continue;
    size_t end = i + 2;
    while (end < summary->length && summary->data[end] >= '0' && summary->data[end] <= '9')
      ++end;
    summary->data[i] = '?';
    memmove(summary->data + i + 1, summary->data + end, summary->length - end + 1);
    summary->length -= end - i - 1;
  }
}

static void sequel_inspection_document_restore_array_literals(sequel_text_result_t *summary,
                                                              const sequel_text_result_t *sanitized) {
  if (!summary || !summary->data || !sanitized || !sanitized->data)
    return;
  const char *source = strstr(sanitized->data, "ARRAY[");
  char *target = strstr(summary->data, "ARRAY[");
  while (source && target) {
    const char *source_end = strchr(source, ']');
    char *target_end = strchr(target, ']');
    if (!source_end || !target_end)
      break;
    size_t source_length = (size_t)(source_end - source) + 1;
    size_t target_length = (size_t)(target_end - target) + 1;
    if (source_length > target_length) {
      size_t growth = source_length - target_length;
      size_t target_offset = (size_t)(target - summary->data);
      char *grown = realloc(summary->data, summary->capacity + growth);
      if (!grown)
        return;
      summary->data = grown;
      summary->capacity += growth;
      target = summary->data + target_offset;
      memmove(target + source_length, target + target_length, summary->length - target_offset - target_length + 1);
    } else if (target_length > source_length) {
      memmove(target + source_length, target + target_length,
              summary->length - (size_t)(target - summary->data) - target_length + 1);
    }
    memcpy(target, source, source_length);
    summary->length += source_length - target_length;
    source = strstr(source + source_length, "ARRAY[");
    target = strstr(target + source_length, "ARRAY[");
  }
}

static void sequel_inspection_document_restore_array_indexes(sequel_text_result_t *summary,
                                                             const sequel_text_result_t *sanitized) {
  if (!summary || !summary->data || !sanitized || !sanitized->data)
    return;
  const char *source = sanitized->data;
  char *target = summary->data;
  while ((source = strchr(source, '[')) != NULL && (target = strchr(target, '[')) != NULL) {
    const char *source_end = source + 1;
    char *target_end = target + 1;
    while (source_end < sanitized->data + sanitized->length && *source_end >= '0' && *source_end <= '9')
      ++source_end;
    if (source_end == source + 1 || source_end >= sanitized->data + sanitized->length || *source_end != ']') {
      source += 1;
      continue;
    }
    while (target_end < summary->data + summary->length && *target_end != ']')
      ++target_end;
    if (target_end >= summary->data + summary->length || target_end != target + 2 || *target_end != ']') {
      target += 1;
      continue;
    }
    size_t source_length = (size_t)(source_end - source) + 1;
    size_t target_offset = (size_t)(target - summary->data);
    size_t target_length = 3;
    if (source_length > target_length) {
      size_t growth = source_length - target_length;
      char *grown = realloc(summary->data, summary->capacity + growth);
      if (!grown)
        return;
      summary->data = grown;
      summary->capacity += growth;
      target = summary->data + target_offset;
      memmove(target + source_length, target + target_length, summary->length - target_offset - target_length + 1);
    } else if (target_length > source_length) {
      memmove(target + source_length, target + target_length, summary->length - target_offset - target_length + 1);
    }
    memcpy(target, source, source_length);
    summary->length += source_length - target_length;
    source += source_length;
    target += source_length;
  }
}

static bool sequel_inspection_document_query_is_safe(const sequel_query_document_t *query, size_t source_length) {
  if (!query)
    return false;
  size_t node_count = 0;
  (void)sequel_syntax_nodes(query->syntax, &node_count);
#define VALID_RANGE(range) ((range).start <= (range).end && (range).end <= source_length)
  for (size_t i = 0; i < query->join_count; ++i)
    if (!VALID_RANGE(query->joins[i].range) || !VALID_RANGE(query->joins[i].condition_range) ||
        query->joins[i].left_relation >= query->relation_count ||
        query->joins[i].right_relation >= query->relation_count ||
        (query->joins[i].condition_node != SIZE_MAX && query->joins[i].condition_node >= node_count))
      return false;
#undef VALID_RANGE
  return true;
}

static bool sequel_inspection_document_has_query_body(const sequel_syntax_document_t *syntax) {
  size_t count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(sequel_syntax_lexed(syntax), &count);
  sequel_source_view_t source = sequel_lexed_source(sequel_syntax_lexed(syntax));
  size_t significant = 0;
  size_t first = SIZE_MAX;
  for (size_t i = 0; i < count; ++i)
    if (!sequel_token_is_trivia(&tokens[i]) && tokens[i].kind != SEQUEL_TOKEN_EOF) {
      if (first == SIZE_MAX)
        first = i;
      ++significant;
    }
  if (significant > 1)
    return true;
  if (first == SIZE_MAX)
    return false;
  size_t length = tokens[first].range.end - tokens[first].range.start;
  const char *word = source.data + tokens[first].range.start;
  return sequel_text_equal_ci(word, length, "BEGIN") || sequel_text_equal_ci(word, length, "COMMIT") ||
         sequel_text_equal_ci(word, length, "ROLLBACK");
}

static bool sequel_inspection_document_has_recoverable_join_shape(const sequel_syntax_document_t *syntax) {
  size_t count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(sequel_syntax_lexed(syntax), &count);
  for (size_t i = 0; i < count; ++i) {
    if (tokens[i].keyword_id != SEQUEL_KEYWORD_JOIN)
      continue;
    size_t next = i + 1;
    while (next < count && sequel_token_is_trivia(&tokens[next]))
      ++next;
    if (next >= count || tokens[next].kind != SEQUEL_TOKEN_PUNCTUATION)
      continue;
    size_t body = next + 1;
    while (body < count && sequel_token_is_trivia(&tokens[body]))
      ++body;
    if (body >= count ||
        (tokens[body].keyword_id != SEQUEL_KEYWORD_SELECT && tokens[body].keyword_id != SEQUEL_KEYWORD_VALUES &&
         tokens[body].keyword_id != SEQUEL_KEYWORD_WITH))
      return false;
  }
  return true;
}

sequel_status_t sequel_inspection_document(const char *sql, size_t length, const sequel_options_t *options,
                                           sequel_metadata_t *out) {
  size_t first_nonspace = 0;
  while (first_nonspace < length && isspace((unsigned char)sql[first_nonspace]))
    ++first_nonspace;
  if (first_nonspace == length)
    return SEQUEL_OK;
  sequel_parse_options_t parse_options;
  sequel_parse_options_init(&parse_options);
  parse_options.dialect = options ? options->dialect : SEQUEL_GENERIC;
  parse_options.retain_comments = true;
  parse_options.retain_whitespace = true;
  parse_options.tolerant = true;
  parse_options.recover_errors = true;
  if (parse_options.dialect == SEQUEL_CLICKHOUSE) {
    sequel_diagnostic_t diagnostic = {0};
    sequel_status_t diagnostic_status = sequel_diagnose_n(sql, length, parse_options.dialect, &diagnostic);
    if (diagnostic_status != SEQUEL_OK && diagnostic.code == SEQUEL_DIAG_UNSUPPORTED_PAYLOAD)
      return diagnostic_status;
  }
  sequel_source_view_t source = {sql, length};
  sequel_syntax_document_t *syntax = NULL;
  sequel_query_document_t *query = NULL;
  sequel_text_result_t sanitized = {0}, summary = {0};
  sequel_status_t status = sequel_parse_sql(&source, &parse_options, NULL, &syntax);
  if (status != SEQUEL_OK)
    goto done;
  size_t issue_count = sequel_syntax_issue_count(syntax);
  if (issue_count && !parse_options.recover_errors) {
    status = SEQUEL_ERR_INVALID_ARGUMENT;
    goto done;
  }
  if (issue_count) {
    const sequel_parse_issue_t *issues = sequel_syntax_issues(syntax, NULL);
    size_t token_count = 0;
    const sequel_token_t *tokens = sequel_lexed_tokens(sequel_syntax_lexed(syntax), &token_count);
    for (size_t i = 0; i < issue_count; ++i) {
      bool unterminated_token = false;
      if (issues[i].code == SEQUEL_PARSE_ISSUE_UNTERMINATED_TOKEN)
        for (size_t t = 0; t < token_count; ++t)
          if (tokens[t].range.start == issues[i].range.start && (tokens[t].flags & SEQUEL_TOKEN_FLAG_UNTERMINATED))
            unterminated_token = true;
      if (unterminated_token ||
          (issues[i].code == SEQUEL_PARSE_ISSUE_UNMATCHED_CLOSE &&
           (issues[i].range.start >= source.length || source.data[issues[i].range.start] != ']'))) {
        status = SEQUEL_ERR_INVALID_ARGUMENT;
        goto done;
      }
    }
  }
  if (!sequel_inspection_document_has_query_body(syntax)) {
    status = SEQUEL_OK;
    goto done;
  }
  if (!sequel_inspection_document_has_recoverable_join_shape(syntax)) {
    status = SEQUEL_OK;
    goto done;
  }
  status = sequel_build_query_document(syntax, &parse_options, &query);
  if (status != SEQUEL_OK)
    goto done;
  if (sequel_query_statement_count(query) == 0) {
    status = SEQUEL_OK;
    goto done;
  }
  if (!sequel_inspection_document_query_is_safe(query, source.length)) {
    status = SEQUEL_OK;
    goto done;
  }
  status = sequel_sanitize_tokens(sequel_syntax_lexed(syntax),
                                  SEQUEL_SANITIZE_REMOVE_COMMENTS | SEQUEL_SANITIZE_REPLACE_LITERALS, &sanitized);
  if (status != SEQUEL_OK)
    goto done;
  sequel_inspection_trim_prefix(&sanitized);
  if (options && !options->keep_in_lists)
    status = sequel_tokens_collapse_in_lists(&sanitized, parse_options.dialect);
  if (status != SEQUEL_OK)
    goto done;
  sequel_inspection_normalize_text(sanitized.data);
  sanitized.length = strlen(sanitized.data);
  sequel_summary_options_t summary_options;
  sequel_summary_options_init(&summary_options);
  summary_options.include_bind_count = false;
  summary_options.redact_relations = true;
  status = sequel_summarize_query(query, &summary_options, &summary);
  if (status != SEQUEL_OK)
    goto done;
  size_t first_sql = 0;
  while (first_sql < source.length && isspace((unsigned char)source.data[first_sql]))
    ++first_sql;
  if (first_sql + 3 <= source.length && sequel_text_equal_ci(source.data + first_sql, 3, "SET") &&
      (first_sql + 3 == source.length || !isalpha((unsigned char)source.data[first_sql + 3])) &&
      memchr(source.data + first_sql, ';', source.length - first_sql) && summary.data &&
      sequel_text_equal_ci(summary.data, 6, "SELECT")) {
    const char prefix[] = "SET; ";
    size_t prefix_length = sizeof(prefix) - 1;
    char *expanded = realloc(summary.data, summary.length + prefix_length + 1);
    if (!expanded) {
      status = SEQUEL_ERR_NOMEM;
      goto done;
    }
    memmove(expanded + prefix_length, expanded, summary.length + 1);
    memcpy(expanded, prefix, prefix_length);
    summary.data = expanded;
    summary.length += prefix_length;
    summary.capacity = summary.length + 1;
    char *ellipsis = strstr(summary.data, "\xE2\x80\xA6");
    if (ellipsis) {
      *ellipsis = '?';
      memmove(ellipsis + 1, ellipsis + 3, summary.length - (size_t)(ellipsis - summary.data) - 3 + 1);
      summary.length -= 2;
    }
  }
  sequel_inspection_document_normalize_parameters(&summary);
  sequel_inspection_document_restore_array_literals(&summary, &sanitized);
  sequel_inspection_document_restore_array_indexes(&summary, &sanitized);
  if (summary.length > SEQUEL_SUMMARY_MAX) {
    size_t limit = sequel_utf8_prefix(summary.data, summary.length, SEQUEL_SUMMARY_MAX - 3);
    memcpy(summary.data + limit, "…", 3);
    summary.data[limit + 3] = 0;
    summary.length = limit + 3;
  }
  size_t text_length = options && options->omit_query_text ? 0 : sanitized.length;
  size_t summary_length = summary.length;
  char *storage = malloc(text_length + summary_length + 2);
  if (!storage) {
    status = SEQUEL_ERR_NOMEM;
    goto done;
  }
  memcpy(storage, sanitized.data, text_length);
  storage[text_length] = 0;
  memcpy(storage + text_length + 1, summary.data, summary_length + 1);
  out->_storage = storage;
  out->text = storage;
  out->summary = storage + text_length + 1;
  out->statement_count = sequel_query_statement_count(query);
  const sequel_statement_info_t *statements = sequel_query_statements(query);
  sequel_inspection_set_operation(&source, statements, out->statement_count, out->operation, sizeof(out->operation));
  out->truncated =
      text_length > (options && options->max_query_text_bytes ? options->max_query_text_bytes : SEQUEL_TEXT_MAX);
  if (out->truncated) {
    size_t limit = options->max_query_text_bytes ? options->max_query_text_bytes : SEQUEL_TEXT_MAX;
    sequel_inspection_cut_utf8((char *)out->text, limit);
  }
  status = SEQUEL_OK;
done:
  sequel_text_result_free(&sanitized);
  sequel_text_result_free(&summary);
  sequel_query_document_free(query);
  sequel_syntax_document_free(syntax);
  return status;
}
