#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <ctype.h>
#include <string.h>

typedef struct {
  const char *p;
  size_t n;
  bool word;
  bool keyword;
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
  if (!value.word || !value.p || !word || strlen(word) != value.n)
    return false;
  for (size_t i = 0; i < value.n; ++i)
    if (toupper((unsigned char)value.p[i]) != toupper((unsigned char)word[i]))
      return false;
  return true;
}

static inline bool sequel_runtime_symbol(token value, char character) { return value.n == 1 && value.p && *value.p == character; }

static inline bool sequel_runtime_letter(unsigned char character) { return character == '_' || (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z'); }

static inline size_t sequel_runtime_dollar_tag_end(const char *text, size_t length, size_t offset, sequel_dialect_t dialect) {
  size_t cursor = offset + 1;
  if (cursor < length && (sequel_runtime_letter((unsigned char)text[cursor]) || (dialect == SEQUEL_CLICKHOUSE && isdigit((unsigned char)text[cursor]))))
    while (cursor < length && (sequel_runtime_letter((unsigned char)text[cursor]) || isdigit((unsigned char)text[cursor])))
      ++cursor;
  return cursor < length && text[cursor] == '$' ? cursor : 0;
}
