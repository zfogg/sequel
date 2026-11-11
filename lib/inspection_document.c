#include <stdlib.h>
#include <string.h>

#include <sequel/parser.h>

#include "inspection.h"
#include "inspection_document.h"
#include "inspection_text.h"
#include "tokens.h"
#include "util/utf8.h"

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

sequel_status_t sequel_inspection_document(const char *sql, size_t length, const sequel_options_t *options,
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
  sequel_inspection_document_normalize_parameters(&summary);
  sequel_inspection_document_restore_array_literals(&summary, &sanitized);
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
