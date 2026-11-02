#pragma once

#include <sequel.h>
#include <stdio.h>

/** Write a diagnostic as human-readable text or JSON. */
void print_diagnostic(FILE *stream, const sequel_diagnostic_t *diagnostic, int json);

/** Run the interactive SQL session with the selected highlighting options. */
int interactive(const sequel_highlight_options_t *options, int json);
