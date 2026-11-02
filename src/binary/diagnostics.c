#include "diagnostics.h"

/* All strings are fixed library-owned explanations, not SQL or user content. */
void print_diagnostic(FILE *stream, const sequel_diagnostic_t *d, int json) {
  const char *severity = d->severity == SEQUEL_ERROR ? "error" : d->severity == SEQUEL_WARNING ? "warning" : "note";
  if (json) {
    fprintf(stream,
            "{\"code\":\"%s\",\"severity\":\"%s\",\"offset\":%zu,\"end_offset\":%zu,"
            "\"line\":%zu,\"column\":%zu,\"end_line\":%zu,\"end_column\":%zu,"
            "\"incomplete\":%s,\"statement_complete\":%s,\"message\":\"%s\","
            "\"explanation\":\"%s\",\"hint\":\"%s\"}\n",
            sequel_diagnostic_code_name(d->code), severity, d->offset, d->end_offset, d->line, d->column, d->end_line,
            d->end_column, d->incomplete ? "true" : "false", d->statement_complete ? "true" : "false", d->message,
            d->explanation, d->hint);
  } else {
    fprintf(stream, "sequel: %zu:%zu: %s [%s]: %s\n  %s\n  Hint: %s\n", d->line, d->column, severity,
            sequel_diagnostic_code_name(d->code), d->message, d->explanation, d->hint);
    if (d->code == SEQUEL_DIAG_NONE)
      fprintf(stream, "  Statement complete: %s\n", d->statement_complete ? "yes" : "no (no final semicolon)");
  }
}
