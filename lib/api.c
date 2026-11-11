#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sequel.h>
#include <sequel/parser.h>
#include "inspection_collapse.h"
#include "inspection.h"
#include "inspection_text.h"
#include "util/text.h"
#include "util/utf8.h"
sequel_status_t sequel_sanitize_with_flags(const char *sql, sequel_dialect_t dialect, unsigned flags,
                                           bool collapse_in_lists, size_t limit, char *out, size_t capacity,
                                           bool *truncated) {
  if (!sql || !out || !truncated)
    return SEQUEL_ERR_NULL;
  *truncated = false;
  if (capacity)
    *out = 0;
  if (dialect < SEQUEL_GENERIC || dialect > SEQUEL_CLICKHOUSE || capacity == 0 || (flags & ~SEQUEL_SANITIZE_ALL))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (!limit)
    limit = SEQUEL_TEXT_MAX;
  size_t n = 0;
  while (n <= SEQUEL_INPUT_MAX && sql[n])
    ++n;
  if (n > SEQUEL_INPUT_MAX)
    return SEQUEL_ERR_OVERFLOW;
  if (!sequel_utf8_valid(sql, n))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (dialect != SEQUEL_MYSQL && sequel_text_find(sql, n, "_utf8mb4'"))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  {
    size_t open_brackets = 0, close_brackets = 0;
    for (size_t index = 0; index < n; ++index) {
      open_brackets += sql[index] == '[';
      close_brackets += sql[index] == ']';
    }
    if (open_brackets > close_brackets)
      return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  sequel_parse_options_t options;
  sequel_parse_options_init(&options);
  options.dialect = dialect;
  options.retain_comments = true;
  options.retain_whitespace = true;
  sequel_source_view_t source = {sql, n};
  sequel_lexed_document_t *lexed = NULL;
  sequel_status_t status = sequel_lex(&source, &options, NULL, &lexed);
  if (status != SEQUEL_OK)
    return status;
  sequel_text_result_t result = {0};
  status = sequel_sanitize_tokens(lexed, flags, &result);
  sequel_lexed_document_free(lexed);
  if (status != SEQUEL_OK)
    return status;
  if (collapse_in_lists && (flags & SEQUEL_REPLACE_LITERALS)) {
    status = sequel_inspection_collapse_in_lists(&result, dialect);
    if (status != SEQUEL_OK) {
      sequel_text_result_free(&result);
      return status;
    }
  }
  if (flags & SEQUEL_SANITIZE_REMOVE_COMMENTS)
    sequel_inspection_normalize_text(result.data);
  *truncated = result.length > limit;
  sequel_inspection_cut_utf8(result.data, limit);
  result.length = strlen(result.data);
  if (result.length >= capacity) {
    sequel_text_result_free(&result);
    *truncated = false;
    return SEQUEL_ERR_OVERFLOW;
  }
  memcpy(out, result.data, result.length + 1);
  sequel_text_result_free(&result);
  return SEQUEL_OK;
}

sequel_status_t sequel_sanitize(const char *sql, sequel_dialect_t dialect, bool collapse_in_lists, size_t limit,
                                char *out, size_t capacity, bool *truncated) {
  return sequel_sanitize_with_flags(sql, dialect, SEQUEL_SANITIZE_ALL, collapse_in_lists, limit, out, capacity,
                                    truncated);
}

void sequel_metadata_free(sequel_metadata_t *metadata) {
  if (metadata) {
    free(metadata->_storage);
    *metadata = (sequel_metadata_t){.text = "", .summary = "SQL"};
  }
}

static sequel_status_t inspect_document(const char *sql, size_t length, const sequel_options_t *options,
                                        sequel_metadata_t *out) {
  sequel_parse_options_t parse_options;
  sequel_parse_options_init(&parse_options);
  parse_options.dialect = options ? options->dialect : SEQUEL_GENERIC;
  parse_options.retain_comments = true;
  parse_options.retain_whitespace = true;
  parse_options.tolerant = true;
  parse_options.recover_errors = true;
  sequel_diagnostic_t input_diagnostic = {0};
  sequel_status_t diagnostic_status = sequel_diagnose_n(sql, length, parse_options.dialect, &input_diagnostic);
  if (diagnostic_status != SEQUEL_OK && input_diagnostic.code == SEQUEL_DIAG_UNSUPPORTED_PAYLOAD)
    return diagnostic_status;
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
  status = sequel_build_query_document(syntax, &parse_options, &query);
  if (status != SEQUEL_OK)
    goto done;
  status = sequel_sanitize_tokens(sequel_syntax_lexed(syntax),
                                  SEQUEL_SANITIZE_REMOVE_COMMENTS | SEQUEL_SANITIZE_REPLACE_LITERALS, &sanitized);
  if (status != SEQUEL_OK)
    goto done;
  sequel_inspection_trim_prefix(&sanitized);
  if (options && !options->keep_in_lists)
    status = sequel_inspection_collapse_in_lists(&sanitized, parse_options.dialect);
  if (status != SEQUEL_OK)
    goto done;
  sequel_inspection_normalize_text(sanitized.data);
  sanitized.length = strlen(sanitized.data);
  sequel_summary_options_t summary_options;
  sequel_summary_options_init(&summary_options);
  summary_options.max_length = 0;
  summary_options.include_bind_count = false;
  summary_options.redact_relations = true;
  status = sequel_summarize_query(query, &summary_options, &summary);
  if (status != SEQUEL_OK)
    goto done;
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

sequel_status_t sequel_inspect(const char *sql, const sequel_options_t *options, sequel_metadata_t *out) {
  if (!out)
    return SEQUEL_ERR_NULL;
  *out = (sequel_metadata_t){.text = "", .summary = "SQL"};
  if (!sql)
    return SEQUEL_ERR_NULL;
  sequel_options_t o = options ? *options : (sequel_options_t){0};
  if (o.dialect > SEQUEL_CLICKHOUSE || o.dialect < SEQUEL_GENERIC || o.max_query_text_bytes > SEQUEL_TEXT_MAX)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  size_t n = 0;
  while (n <= SEQUEL_INPUT_MAX && sql[n])
    ++n;
  if (n > SEQUEL_INPUT_MAX)
    return SEQUEL_ERR_OVERFLOW;
  if (!sequel_utf8_valid(sql, n))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (o.dialect != SEQUEL_MYSQL && sequel_text_find(sql, n, "_utf8mb4'"))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  return inspect_document(sql, n, &o, out);
}
