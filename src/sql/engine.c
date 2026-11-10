#include "../utf8.h"
#include <ctype.h>
#include <sequel.h>
#include "diagnostics.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "resources.h"

#define INPUT_MAX SEQUEL_INPUT_MAX
typedef struct {
  const char *p;
  size_t n;
  bool word, space, clickhouse;
} token;
typedef struct {
  char *p;
  size_t n, cap;
  bool overflow, charge_output;
} output;

#include "legacy_tokens.h"

// clang-format off
#include "legacy_common.c"
#include "legacy_scanner.c"
#include "legacy_summary.c"
#include "legacy_formatter.c"
#include "legacy_diagnostics.c"
#include "legacy_api.c"
#include "legacy_compact.c"
#include "legacy_highlight.c"
#include "legacy_contracts.c"
#include "legacy_sha256.c"
#include "legacy_analysis.c"
// clang-format on

#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#undef sequel_checkpoint
#undef INPUT_MAX
