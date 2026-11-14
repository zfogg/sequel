/* Identify values that sanitization and fingerprinting must redact. */
#include "internal.h"

const char *sequel_analysis_dollar_quoted_literal(const char *sql, size_t length) {
  for (size_t offset = 0; offset < length; ++offset) {
    if (sql[offset] != '$')
      continue;
    size_t delimiter_end = offset + 1;
    while (delimiter_end < length && (isalnum((unsigned char)sql[delimiter_end]) || sql[delimiter_end] == '_'))
      ++delimiter_end;
    if (delimiter_end >= length || sql[delimiter_end] != '$')
      continue;
    size_t body = delimiter_end + 1;
    size_t delimiter_length = delimiter_end - offset + 1;
    while (body + delimiter_length <= length) {
      if (sql[body] == '$' && !memcmp(sql + body, sql + offset, delimiter_length))
        return sql + offset;
      ++body;
    }
  }
  return NULL;
}

const char *sequel_analysis_ambiguous_escaped_quote(const char *sql, size_t length) {
  const char *cursor = sql;
  while ((cursor = sequel_text_find(cursor, (size_t)(sql + length - cursor), "\\'")) != NULL) {
    const char *quote = cursor;
    while (quote > sql) {
      --quote;
      if (*quote == '\'' && (quote == sql || quote[-1] != '\\'))
        break;
    }
    bool extended = quote > sql && (quote[-1] == 'E' || quote[-1] == 'e');
    if (!extended)
      return cursor;
    cursor += 2;
  }
  return NULL;
}

static unsigned char clickhouse_quote_kind(const char *text, size_t length) {
  if (length < 3 || (unsigned char)text[0] != 0xe2 || (unsigned char)text[1] != 0x80)
    return 0;
  return ((unsigned char)text[2] == 0x98 || (unsigned char)text[2] == 0x9c) ? (unsigned char)text[2] : 0;
}

bool sequel_analysis_literal(analysis_token_t t, sequel_dialect_t d) {
  unsigned char c = (unsigned char)*t.t.p;
  if (isdigit(c) || (c == '.' && t.t.n > 1 && isdigit((unsigned char)t.t.p[1])))
    return true;
  if (t.kind == 1 && (sequel_runtime_keyword(t.t, "TRUE") || sequel_runtime_keyword(t.t, "FALSE") || sequel_runtime_keyword(t.t, "NULL")))
    return true;
  if (t.kind != 2)
    return false;
  if (c == '\'' || (d == SEQUEL_MYSQL && c == '"') || (d == SEQUEL_CLICKHOUSE && clickhouse_quote_kind(t.t.p, t.t.n) == 0x98))
    return true;
  if (c == '$' && d != SEQUEL_SQLITE && sequel_runtime_dollar_tag_end(t.t.p, t.t.n, 0, d))
    return true;
  return c != '"' && c != '`' && c != '[' && c != '{' && c != '$' && c != '%' && c != '?' && c != ':' && c != '@' && !(d == SEQUEL_CLICKHOUSE && clickhouse_quote_kind(t.t.p, t.t.n) == 0x9c);
}
