#include "util/utf8.h"
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

/* Internal rendering view retained only by the legacy analysis formatter. */
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

/* Token arrays grow with parsed tokens, rather than the maximum input size. */
static void *grow_tokens(void *tokens, size_t *capacity, size_t element_size) {
  size_t maximum = SIZE_MAX / element_size;
  if (*capacity == maximum)
    return NULL;
  size_t next = *capacity ? (*capacity > maximum / 2 ? maximum : *capacity * 2) : 256;
  void *grown = realloc(tokens, next * element_size);
  if (grown)
    *capacity = next;
  return grown;
}

static bool letter(unsigned char c) { return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

static bool part(unsigned char c) { return letter(c) || isdigit(c) || c == '$' || c >= 128; }

static bool equal(const char *s, size_t n, const char *word) {
  if (strlen(word) != n)
    return false;
  for (size_t i = 0; (sequel_work_tick(1), i < n); ++i)
    if (toupper((unsigned char)s[i]) != toupper((unsigned char)word[i]))
      return false;
  return true;
}

static bool kw(token t, const char *word) { return t.word && equal(t.p, t.n, word); }

static bool sym(token t, char c) { return t.n == 1 && *t.p == c; }

static void put(output *b, const char *s, size_t n) {
  if (b->charge_output)
    sequel_output_tick(n);
  if (n > b->cap - b->n - 1) {
    b->overflow = true;
    n = b->cap - b->n - 1;
  }
  sequel_work_tick(n);
  memcpy(b->p + b->n, s, n);
  b->n += n;
  b->p[b->n] = 0;
}

static void text(output *b, const char *s) { put(b, s, strlen(s)); }

static void item(output *b, const char *s) {
  if (b->n && b->p[b->n - 1] != ' ')
    text(b, " ");
  text(b, s);
}

static void emit_token(output *b, token t) { put(b, t.p, t.n); }

static void upper(output *b, token t) {
  for (size_t i = 0; (sequel_work_tick(1), i < t.n); ++i) {
    char c = (char)toupper((unsigned char)t.p[i]);
    put(b, &c, 1);
  }
}

static size_t quoted(const char *s, size_t n, size_t i, char close, bool backslash) {
  for (++i; (sequel_work_tick(1), i < n); ++i) {
    if (!backslash && close == '\'' && sequel_current_call && s[i] == '\\' && i + 1 < n) {
      if (s[i + 1] == '\\') {
        ++i;
        continue;
      }
      if (s[i + 1] == close) {
        sequel_current_call->ambiguous_literal = true;
        sequel_current_call->ambiguous_offset = (size_t)(s + i - sequel_current_call->source);
        return 0;
      }
    }
    if (backslash && s[i] == '\\') {
      if (++i == n)
        return 0;
      continue;
    }
    if (s[i] == close) {
      if (i + 1 < n && s[i + 1] == close) {
        ++i;
        continue;
      }
      return i + 1;
    }
  }
  return 0;
}

static size_t number_end(const char *s, size_t n, size_t i) {
  if (i + 1 < n && s[i] == '0' && strchr("xXbB", s[i + 1])) {
    i += 2;
    while (i < n && (isxdigit((unsigned char)s[i]) || s[i] == '_'))
      ++i;
    return i;
  }
  while (i < n && (isdigit((unsigned char)s[i]) || s[i] == '_' || s[i] == '.'))
    ++i;
  if (i < n && (s[i] == 'e' || s[i] == 'E')) {
    ++i;
    if (i < n && (s[i] == '+' || s[i] == '-'))
      ++i;
    while (i < n && (isdigit((unsigned char)s[i]) || s[i] == '_'))
      ++i;
  }
  return i;
}

/* ClickHouse's lexer accepts Unicode quote pairs without escape processing. */
static unsigned char clickhouse_quote(const char *s, size_t n, size_t i) {
  if (i + 2 < n && (unsigned char)s[i] == 0xe2 && (unsigned char)s[i + 1] == 0x80 &&
      ((unsigned char)s[i + 2] == 0x98 || (unsigned char)s[i + 2] == 0x9c))
    return (unsigned char)s[i + 2];
  return 0;
}

static size_t clickhouse_quoted_end(const char *s, size_t n, size_t i) {
  unsigned char close = clickhouse_quote(s, n, i) + 1;
  for (size_t j = i + 3; (sequel_work_tick(1), j + 2 < n); ++j)
    if ((unsigned char)s[j] == 0xe2 && (unsigned char)s[j + 1] == 0x80 && (unsigned char)s[j + 2] == close)
      return j + 3;
  return 0;
}

static size_t dollar_tag_end(const char *s, size_t n, size_t i, sequel_dialect_t dialect) {
  size_t j = i + 1;
  if (j < n && (letter((unsigned char)s[j]) || (dialect == SEQUEL_CLICKHOUSE && isdigit((unsigned char)s[j]))))
    while (j < n && (letter((unsigned char)s[j]) || isdigit((unsigned char)s[j])))
      ++j;
  return j < n && s[j] == '$' ? j : 0;
}

static size_t dialect_number_end(const char *s, size_t n, size_t i, sequel_dialect_t dialect) {
  if (dialect != SEQUEL_CLICKHOUSE || i + 2 >= n || s[i] != '0' || !strchr("xX", s[i + 1]))
    return number_end(s, n, i);
  size_t j = i + 2;
  while (j < n && (isxdigit((unsigned char)s[j]) || s[j] == '_'))
    ++j;
  if (j < n && s[j] == '.') {
    ++j;
    while (j < n && (isxdigit((unsigned char)s[j]) || s[j] == '_'))
      ++j;
  }
  if (j < n && strchr("pP", s[j])) {
    ++j;
    if (j < n && strchr("+-", s[j]))
      ++j;
    while (j < n && (isdigit((unsigned char)s[j]) || s[j] == '_'))
      ++j;
  }
  return j;
}

/* Typed ClickHouse parameters are opaque lexical tokens, including nested types.
 * Ignore parameter names/types in telemetry; never inspect the supplied values. */
static size_t clickhouse_parameter(const char *s, size_t n, size_t i) {
  size_t j = i + 1;
  if (j >= n || !letter((unsigned char)s[j]))
    return 0;
  while (j < n && part((unsigned char)s[j]))
    ++j;
  while (j < n && isspace((unsigned char)s[j]))
    ++j;
  if (j == n || s[j++] != ':')
    return 0;
  size_t start = j, depth = 0;
  for (; (sequel_work_tick(1), j < n); ++j) {
    if (s[j] == '\'' || s[j] == '"' || s[j] == '`') {
      size_t end = quoted(s, n, j, s[j], true);
      if (!end)
        return 0;
      j = end - 1;
    } else if (s[j] == '(') {
      if (++depth > 64)
        return 0;
    } else if (s[j] == ')') {
      if (!depth)
        return 0;
      --depth;
    } else if (s[j] == '}') {
      return !depth && j > start ? j + 1 : 0;
    } else if (s[j] == '{' || s[j] == ';')
      return 0;
  }
  return 0;
}



// clang-format off
#include "summary.c"
#include "layout.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#define SEQUEL_DIAGNOSTICS_ENGINE 1
#include "diagnostics.c"
#undef SEQUEL_DIAGNOSTICS_ENGINE
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
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
