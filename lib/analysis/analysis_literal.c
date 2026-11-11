/* Identify values that sanitization and fingerprinting must redact. */
#include "analysis_internal.h"

static unsigned char clickhouse_quote_kind(const char *text, size_t length) {
  if (length < 3 || (unsigned char)text[0] != 0xe2 || (unsigned char)text[1] != 0x80)
    return 0;
  return ((unsigned char)text[2] == 0x98 || (unsigned char)text[2] == 0x9c) ? (unsigned char)text[2] : 0;
}

bool sequel_analysis_literal(analysis_token_t t, sequel_dialect_t d) {
  unsigned char c = (unsigned char)*t.t.p;
  if (isdigit(c) || (c == '.' && t.t.n > 1 && isdigit((unsigned char)t.t.p[1])))
    return true;
  if (t.kind == 1 && (sequel_runtime_keyword(t.t, "TRUE") || sequel_runtime_keyword(t.t, "FALSE") ||
                      sequel_runtime_keyword(t.t, "NULL")))
    return true;
  if (t.kind != 2)
    return false;
  if (c == '\'' || (d == SEQUEL_MYSQL && c == '"') ||
      (d == SEQUEL_CLICKHOUSE && clickhouse_quote_kind(t.t.p, t.t.n) == 0x98))
    return true;
  if (c == '$' && d != SEQUEL_SQLITE && sequel_runtime_dollar_tag_end(t.t.p, t.t.n, 0, d))
    return true;
  return c != '"' && c != '`' && c != '[' && c != '{' && c != '$' && c != '%' && c != '?' && c != ':' && c != '@' &&
         !(d == SEQUEL_CLICKHOUSE && clickhouse_quote_kind(t.t.p, t.t.n) == 0x9c);
}
