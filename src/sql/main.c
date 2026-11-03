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

// clang-format off
#include "common.c"
#include "scanner.c"
#include "summary.c"
#include "format.c"
#include "diagnostics.c"
#include "api.c"
#include "compact.c"
#include "highlight.c"
#include "contracts.c"
#include "sha256.c"
#include "analysis.c"
// clang-format on

/* Resource instrumentation belongs only to the SQL implementation. In unity
 * builds, subsequent sources must retain their own libc allocation ABI and
 * loop semantics (notably utf8proc_map's caller-owned malloc/free contract). */
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#undef sequel_checkpoint
#undef INPUT_MAX
