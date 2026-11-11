#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <ctype.h>

#include "scan.h"

typedef struct {
  const char *p;
  size_t n;
  bool word;
  bool space;
  bool clickhouse;
} token;

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
} analysis_token_t;

static inline bool sequel_runtime_keyword(token value, const char *word) {
  return value.word && value.p && sequel_scan_equal(value.p, value.n, word);
}

static inline bool sequel_runtime_symbol(token value, char character) {
  return value.n == 1 && value.p && *value.p == character;
}

static inline bool sequel_runtime_letter(unsigned char character) {
  return character == '_' || (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

static inline size_t sequel_runtime_dollar_tag_end(const char *text, size_t length, size_t offset,
                                                   sequel_dialect_t dialect) {
  size_t cursor = offset + 1;
  if (cursor < length && (sequel_runtime_letter((unsigned char)text[cursor]) ||
                          (dialect == SEQUEL_CLICKHOUSE && isdigit((unsigned char)text[cursor]))))
    while (cursor < length &&
           (sequel_runtime_letter((unsigned char)text[cursor]) || isdigit((unsigned char)text[cursor])))
      ++cursor;
  return cursor < length && text[cursor] == '$' ? cursor : 0;
}
