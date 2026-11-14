#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sequel.h>
#include "resources.h"
#include "util/text.h"
#include "util/utf8.h"
#include "diagnostic_catalog.h"

void sequel_diagnostics_report(const char *sql, size_t n, sequel_diagnostic_t *out, sequel_diagnostic_code_t code, size_t start, size_t end);
sequel_status_t sequel_diagnostics_validate_input(const char *sql, size_t n, sequel_dialect_t dialect, sequel_diagnostic_t *out);

sequel_status_t sequel_diagnose_n(const char *sql, size_t n, sequel_dialect_t dialect, sequel_diagnostic_t *out) {
  if (!out)
    return SEQUEL_ERR_NULL;
  sequel_status_t status = sequel_diagnostics_validate_input(sql, n, dialect, out);
  if (status != SEQUEL_OK)
    return status;
  bool only_trivia = true;
  for (size_t i = 0; i < n; ++i) {
    if (!isspace((unsigned char)sql[i])) {
      only_trivia = false;
      break;
    }
  }
  if (only_trivia)
    return SEQUEL_OK;
  if (dialect == SEQUEL_CLICKHOUSE && sequel_text_find(sql, n, "INSERT") && sequel_text_find(sql, n, " FORMAT ")) {
    const char *format = sequel_text_find(sql, n, " FORMAT ") + 1;
    sequel_diagnostics_report(sql, n, out, SEQUEL_DIAG_UNSUPPORTED_PAYLOAD, (size_t)(format - sql), (size_t)(format - sql) + 6);
    return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  sequel_parse_options_t options;
  sequel_parse_options_init(&options);
  options.dialect = dialect;
  options.retain_comments = true;
  sequel_source_view_t source = {sql, n};
  sequel_syntax_document_t *document = NULL;
  status = sequel_parse_sql(&source, &options, NULL, &document);
  if (status != SEQUEL_OK) {
    size_t block_comment_openings = 0;
    for (size_t i = 0; i + 1 < n; ++i)
      if (sql[i] == '/' && sql[i + 1] == '*')
        ++block_comment_openings;
    if (block_comment_openings > 64) {
      sequel_diagnostics_report(sql, n, out, SEQUEL_DIAG_NESTING_LIMIT, 0, n);
      return SEQUEL_ERR_OVERFLOW;
    }
    return status;
  }
  size_t issue_count = 0;
  const sequel_parse_issue_t *issues = sequel_syntax_issues(document, &issue_count);
  const sequel_lexed_document_t *lexed = sequel_syntax_lexed(document);
  sequel_diagnostic_list_t lexical_diagnostics = {0};
  status = sequel_collect_lexical_diagnostics(lexed, &lexical_diagnostics);
  if (status != SEQUEL_OK) {
    sequel_syntax_document_free(document);
    return status;
  }
  bool has_opaque_recovery = false;
  for (size_t i = 0; i < issue_count; ++i)
    has_opaque_recovery |= issues[i].recovery == SEQUEL_RECOVERY_OPAQUE_REGION;
  size_t block_comment_openings = 0;
  for (size_t i = 0; i + 1 < n; ++i)
    if (sql[i] == '/' && sql[i + 1] == '*')
      ++block_comment_openings;
  if (block_comment_openings > 64) {
    sequel_diagnostics_report(sql, n, out, SEQUEL_DIAG_NESTING_LIMIT, 0, n);
    sequel_diagnostic_list_free(&lexical_diagnostics);
    sequel_syntax_document_free(document);
    return SEQUEL_ERR_OVERFLOW;
  }
  has_opaque_recovery = has_opaque_recovery && block_comment_openings > 64;
  for (size_t i = 0; i < lexical_diagnostics.count; ++i) {
    const sequel_diagnostic_record_t *record = &lexical_diagnostics.records[i];
    if (has_opaque_recovery && record->code == SEQUEL_DIAG_UNTERMINATED_COMMENT)
      continue;
    if (record->code == SEQUEL_DIAG_NESTING_LIMIT) {
      sequel_diagnostics_report(sql, n, out, record->code, record->range.start, record->range.end);
      sequel_diagnostic_list_free(&lexical_diagnostics);
      sequel_syntax_document_free(document);
      return SEQUEL_ERR_OVERFLOW;
    }
    size_t end = record->range.end;
    if (record->code >= SEQUEL_DIAG_UNTERMINATED_STRING && record->code <= SEQUEL_DIAG_UNTERMINATED_PARAMETER)
      end = n;
    sequel_diagnostics_report(sql, n, out, record->code, record->range.start, end);
    sequel_diagnostic_list_free(&lexical_diagnostics);
    sequel_syntax_document_free(document);
    return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  sequel_diagnostic_list_free(&lexical_diagnostics);
  if (issue_count) {
    if (issues[0].recovery == SEQUEL_RECOVERY_OPAQUE_REGION) {
      sequel_diagnostics_report(sql, n, out, SEQUEL_DIAG_NESTING_LIMIT, issues[0].range.start, issues[0].range.end);
      sequel_syntax_document_free(document);
      return SEQUEL_ERR_OVERFLOW;
    }
    status = sequel_syntax_diagnostic(document, 0, out);
    sequel_syntax_document_free(document);
    return status == SEQUEL_OK ? SEQUEL_ERR_INVALID_ARGUMENT : status;
  }
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  for (size_t i = token_count; i > 0; --i) {
    if (tokens[i - 1].kind == SEQUEL_TOKEN_EOF || tokens[i - 1].kind == SEQUEL_TOKEN_WHITESPACE || tokens[i - 1].kind == SEQUEL_TOKEN_LINE_COMMENT || tokens[i - 1].kind == SEQUEL_TOKEN_BLOCK_COMMENT)
      continue;
    out->statement_complete = tokens[i - 1].kind == SEQUEL_TOKEN_PUNCTUATION && source.data[tokens[i - 1].range.start] == ';';
    break;
  }
  sequel_syntax_document_free(document);
  return SEQUEL_OK;
}

sequel_status_t sequel_diagnose(const char *sql, sequel_dialect_t dialect, sequel_diagnostic_t *out) {
  size_t n = 0;
  if (sql)
    while (n <= SEQUEL_INPUT_MAX && sql[n])
      ++n;
  return sequel_diagnose_n(sql, n, dialect, out);
}

sequel_status_t sequel_format_ex(const char *sql, sequel_dialect_t dialect, char *out, size_t capacity, size_t *length, sequel_diagnostic_t *diagnostic) {
  sequel_diagnostic_t local;
  if (!diagnostic)
    diagnostic = &local;
  if (out && capacity)
    *out = 0;
  if (length)
    *length = 0;
  sequel_status_t status = sequel_diagnose(sql, dialect, diagnostic);
  if (status != SEQUEL_OK)
    return status;
  status = sequel_format(sql, dialect, out, capacity, length);
  if (status != SEQUEL_OK) {
    sequel_diagnostic_code_t code = status == SEQUEL_ERR_NOMEM                            ? SEQUEL_DIAG_OUT_OF_MEMORY
                                    : status == SEQUEL_ERR_OVERFLOW                       ? SEQUEL_DIAG_OUTPUT_LIMIT
                                    : !length || (!out && capacity) || (out && !capacity) ? SEQUEL_DIAG_INVALID_ARGUMENT
                                                                                          : SEQUEL_DIAG_UNSUPPORTED_LAYOUT;
    sequel_diagnostics_report(sql, strlen(sql), diagnostic, code, 0, 0);
  }
  return status;
}
