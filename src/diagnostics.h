#pragma once

#include <sequel.h>
#include <stdio.h>

/** Write a diagnostic as human-readable text or JSON. */
void print_diagnostic(FILE *stream, const sequel_diagnostic_t *diagnostic, int json);

/** Analyze explicit input and write all safe-boundary issues without SQL text.
 * JSON uses a versioned envelope; text prints one explanation per issue. */
sequel_status_t diagnose_all(FILE *stream, const char *sql, size_t length, sequel_dialect_t dialect, int json);

/** Run the interactive SQL session with the selected highlighting options. */
int interactive(const sequel_highlight_options_t *options, int json);
