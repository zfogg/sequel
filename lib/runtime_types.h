#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "scan.h"

typedef struct {
  const char *p;
  size_t n;
  bool word;
  bool space;
  bool clickhouse;
} token;

typedef struct {
  char *p;
  size_t n;
  size_t cap;
  bool overflow;
  bool charge_output;
} output;

typedef struct {
  token t;
  bool gap;
  bool newline;
  int kind;
  size_t matching;
  bool multiline;
  size_t flat_width;
  size_t source_offset;
  size_t rendered_offset;
} format_token;

static inline bool sequel_runtime_keyword(token value, const char *word) {
  return value.word && value.p && sequel_scan_equal(value.p, value.n, word);
}

static inline bool sequel_runtime_symbol(token value, char character) {
  return value.n == 1 && value.p && *value.p == character;
}
