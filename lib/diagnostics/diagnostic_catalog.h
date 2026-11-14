#pragma once

#include <stddef.h>

#include <sequel/diagnostics.h>

const char *sequel_diagnostic_catalog_message(sequel_diagnostic_code_t code);
const char *sequel_diagnostic_catalog_explanation(sequel_diagnostic_code_t code);
const char *sequel_diagnostic_catalog_hint(sequel_diagnostic_code_t code);
size_t sequel_diagnostic_catalog_size(void);
