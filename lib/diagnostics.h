#pragma once

#include <sequel.h>

/* Private helpers shared by the SQL engine's included implementation files. */
void diagnostic_report(const char *sql, size_t n, sequel_diagnostic_t *out, sequel_diagnostic_code_t code, size_t start,
                       size_t end);

sequel_status_t diagnostic_input(const char *sql, size_t length, sequel_dialect_t dialect,
                                 sequel_diagnostic_t *diagnostic);
