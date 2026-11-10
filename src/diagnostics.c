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
          d->offset, d->end_offset, d->line, d->column, d->end_line, d->end_column, d->incomplete ? "true" : "false",
          d->statement_complete ? "true" : "false");
  diagnostic_json_text(stream, d->message);
  fputs(",\"explanation\":", stream);
  diagnostic_json_text(stream, d->explanation);
  fputs(",\"hint\":", stream);
  diagnostic_json_text(stream, d->hint);
  fputc('}', stream);
}

/* All strings are fixed library-owned explanations, not SQL or user content. */
void print_diagnostic(FILE *stream, const sequel_diagnostic_t *d, int json) {
  const char *severity = d->severity == SEQUEL_ERROR ? "error" : d->severity == SEQUEL_WARNING ? "warning" : "note";
  if (json) {
    diagnostic_json(stream, d);
    fputc('\n', stream);
  } else {
    fprintf(stream, "sequel: %zu:%zu: %s [%s]: %s\n  %s\n  Hint: %s\n", d->line, d->column, severity,
            sequel_diagnostic_code_name(d->code), d->message, d->explanation, d->hint);
    if (d->code == SEQUEL_DIAG_NONE)
      fprintf(stream, "  Statement complete: %s\n", d->statement_complete ? "yes" : "no (no final semicolon)");
  }
}

sequel_status_t diagnose_all(FILE *stream, const char *sql, size_t length, sequel_dialect_t dialect, int json) {
  sequel_analysis_options_t options = {0};
  options.dialect = dialect;
  options.omit_query_text = true;
  options.omit_source_maps = true;
  options.omit_bind_descriptors = true;
  sequel_analysis_t analysis;
  sequel_status_t status = sequel_analyze_n(sql, length, &options, &analysis);
  if (json) {
    fprintf(stream, "{\"schema_version\":%u,\"original_length\":%zu,\"status\":", SEQUEL_DIAGNOSTIC_VERSION,
            analysis.original_length);
    diagnostic_json_text(stream, analysis.status);
    fputs(",\"diagnostics\":[", stream);
    for (size_t i = 0; i < analysis.diagnostic_count; ++i) {
      if (i)
        fputc(',', stream);
      diagnostic_json(stream, &analysis.diagnostics[i]);
    }
    fputs("]}\n", stream);
  } else if (!analysis.diagnostic_count) {
    fputs("sequel: no lexical or delimiter issues found\n", stream);
  } else {
    for (size_t i = 0; i < analysis.diagnostic_count; ++i)
      print_diagnostic(stream, &analysis.diagnostics[i], 0);
  }
  sequel_analysis_free(&analysis);
  return status;
}
