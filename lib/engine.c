#include "utf8.h"
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

#include "tokens.h"

// clang-format off
#include "common.c"
#include "summary.c"
#include "layout.c"
#include "token_adapter.c"
#include "diagnostic_api.c"
#include "api.c"
#include "compact.c"
#include "highlight.c"
#include "contracts.c"
#include "sha256.c"
#include "analysis.c"
// clang-format on

#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#undef sequel_checkpoint
#undef INPUT_MAX
