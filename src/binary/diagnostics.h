#ifndef SEQUEL_BINARY_DIAGNOSTICS_H
#define SEQUEL_BINARY_DIAGNOSTICS_H
#include <sequel.h>
#include <stdio.h>
void print_diagnostic(FILE *stream, const sequel_diagnostic_t *diagnostic, int json);
int interactive(const sequel_highlight_options_t *options, int json);
#endif
