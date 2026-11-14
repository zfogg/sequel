#include "diagnostics.h"

/* Quote even catalog prose, so a future explanation cannot break JSON syntax.
 * SQL and user-provided prose are never passed to this writer. */
static void diagnostic_json_text(FILE *stream, const char *value) {
  fputc('"', stream);
  for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
    switch (*p) {
    case '"':
      fputs("\\\"", stream);
      break;
    case '\\':
      fputs("\\\\", stream);
      break;
    case '\n':
      fputs("\\n", stream);
      break;
    case '\r':
      fputs("\\r", stream);
      break;
    case '\t':
      fputs("\\t", stream);
      break;
    default:
      if (*p < 32)
        fprintf(stream, "\\u%04x", (unsigned)*p);
      else
        fputc(*p, stream);
    }
  }
  fputc('"', stream);
}

static void diagnostic_json(FILE *stream, const sequel_diagnostic_t *d) {
  const char *severity = d->severity == SEQUEL_ERROR ? "error" : d->severity == SEQUEL_WARNING ? "warning" : "note";
  fputs("{\"code\":", stream);
  diagnostic_json_text(stream, sequel_diagnostic_code_name(d->code));
  fputs(",\"severity\":", stream);
  diagnostic_json_text(stream, severity);
  fprintf(stream,
          ",\"offset\":%zu,\"end_offset\":%zu,\"line\":%zu,\"column\":%zu,"
          "\"end_line\":%zu,\"end_column\":%zu,\"incomplete\":%s,\"statement_complete\":%s,\"message\":",
          d->offset, d->end_offset, d->line, d->column, d->end_line, d->end_column, d->incomplete ? "true" : "false", d->statement_complete ? "true" : "false");
  diagnostic_json_text(stream, d->message);
  fputs(",\"explanation\":", stream);
  diagnostic_json_text(stream, d->explanation);
  fputs(",\"hint\":", stream);
  diagnostic_json_text(stream, d->hint);
  fputc('}', stream);
}

static bool diagnostic_is_input_failure(sequel_diagnostic_code_t code) { return code == SEQUEL_DIAG_CONTROL_CHARACTER || code == SEQUEL_DIAG_INVALID_UTF8 || code == SEQUEL_DIAG_NESTING_LIMIT; }

static bool diagnostic_is_duplicate(const sequel_diagnostic_list_t *diagnostics, const sequel_diagnostic_record_t *candidate) {
  for (size_t i = 0; i < diagnostics->count; ++i) {
    const sequel_diagnostic_record_t *existing = &diagnostics->records[i];
    if (existing->code == candidate->code && existing->range.start == candidate->range.start && existing->range.end == candidate->range.end)
      return true;
  }
  return false;
}

static bool diagnostic_is_unterminated(sequel_diagnostic_code_t code) {
  return code == SEQUEL_DIAG_UNTERMINATED_STRING || code == SEQUEL_DIAG_UNTERMINATED_COMMENT || code == SEQUEL_DIAG_UNTERMINATED_IDENTIFIER || code == SEQUEL_DIAG_UNTERMINATED_PARAMETER;
}

/* All strings are fixed library-owned explanations, not SQL or user content. */
void print_diagnostic(FILE *stream, const sequel_diagnostic_t *d, int json) {
  const char *severity = d->severity == SEQUEL_ERROR ? "error" : d->severity == SEQUEL_WARNING ? "warning" : "note";
  if (json) {
    diagnostic_json(stream, d);
    fputc('\n', stream);
  } else {
    fprintf(stream, "sequel: %zu:%zu: %s [%s]: %s\n  %s\n  Hint: %s\n", d->line, d->column, severity, sequel_diagnostic_code_name(d->code), d->message, d->explanation, d->hint);
    if (d->code == SEQUEL_DIAG_NONE)
      fprintf(stream, "  Statement complete: %s\n", d->statement_complete ? "yes" : "no (no final semicolon)");
  }
}

sequel_status_t diagnose_all(FILE *stream, const char *sql, size_t length, sequel_dialect_t dialect, int json) {
  sequel_source_view_t source = {sql, length};
  sequel_parse_options_t parse_options;
  sequel_parse_options_init(&parse_options);
  parse_options.dialect = dialect;
  parse_options.retain_comments = true;
  parse_options.tolerant = true;
  parse_options.recover_errors = true;
  sequel_syntax_document_t *syntax = NULL;
  sequel_diagnostic_list_t diagnostics = {0};
  sequel_diagnostic_t input_diagnostic = {0};
  sequel_status_t input_status = sequel_diagnose_n(sql, length, dialect, &input_diagnostic);
  const bool input_failure = input_status != SEQUEL_OK && diagnostic_is_input_failure(input_diagnostic.code);
  sequel_status_t status = input_failure ? input_status : sequel_parse_sql(&source, &parse_options, NULL, &syntax);
  if (input_failure) {
    sequel_diagnostic_record_t record = {
        .origin = SEQUEL_DIAGNOSTIC_LEXICAL, .code = input_diagnostic.code, .severity = input_diagnostic.severity, .range = {input_diagnostic.offset, input_diagnostic.end_offset}};
    if (!sequel_diagnostic_list_append(&diagnostics, record))
      status = SEQUEL_ERR_NOMEM;
  }
  if (status == SEQUEL_OK) {
    /* The CLI's --all contract is lexical/delimiter diagnostics.  Query
     * lints belong to the analysis API and would both leak implementation
     * policy into this command and duplicate recovered token failures. */
    status = sequel_collect_syntax_diagnostics(syntax, &diagnostics);
    if (status == SEQUEL_OK) {
      sequel_diagnostic_list_t lexical = {0};
      status = sequel_collect_lexical_diagnostics(sequel_syntax_lexed(syntax), &lexical);
      for (size_t i = 0; status == SEQUEL_OK && i < lexical.count; ++i) {
        const sequel_diagnostic_record_t *record = &lexical.records[i];
        if (record->code == SEQUEL_DIAG_UNSUPPORTED_LAYOUT || diagnostic_is_duplicate(&diagnostics, record))
          continue;
        bool syntax_has_unterminated = false;
        for (size_t j = 0; j < diagnostics.count; ++j)
          syntax_has_unterminated |= diagnostic_is_unterminated(diagnostics.records[j].code);
        if (syntax_has_unterminated && diagnostic_is_unterminated(record->code))
          continue;
        if (!sequel_diagnostic_list_append(&diagnostics, *record))
          status = SEQUEL_ERR_NOMEM;
      }
      sequel_diagnostic_list_free(&lexical);
    }
  }
  sequel_diagnostic_list_sort(&diagnostics);
  if (status == SEQUEL_OK && diagnostics.count)
    status = SEQUEL_ERR_INVALID_ARGUMENT;
  const sequel_diagnostic_list_t *diagnostic_view = &diagnostics;
  if (json) {
    fprintf(stream, "{\"schema_version\":%u,\"original_length\":%zu,\"status\":", SEQUEL_DIAGNOSTIC_VERSION, length);
    diagnostic_json_text(stream, status == SEQUEL_OK ? "ok" : "malformed");
    fputs(",\"diagnostics\":[", stream);
    for (size_t i = 0; i < diagnostic_view->count; ++i) {
      const sequel_diagnostic_record_t *record = &diagnostic_view->records[i];
      sequel_diagnostic_t converted = {0};
      sequel_diagnostic_project(sql, length, record, &converted);
      if (i)
        fputc(',', stream);
      diagnostic_json(stream, &converted);
    }
    fputs("]}\n", stream);
  } else if (!diagnostic_view->count) {
    fputs("sequel: no lexical or delimiter issues found\n", stream);
  } else {
    for (size_t i = 0; i < diagnostic_view->count; ++i) {
      const sequel_diagnostic_record_t *record = &diagnostic_view->records[i];
      sequel_diagnostic_t converted = {0};
      sequel_diagnostic_project(sql, length, record, &converted);
      print_diagnostic(stream, &converted, 0);
    }
  }
  sequel_diagnostic_list_free(&diagnostics);
  sequel_syntax_document_free(syntax);
  return status;
}
