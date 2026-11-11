/* Identify values that sanitization and fingerprinting must redact. */
static bool analysis_literal(format_token t, sequel_dialect_t d) {
  unsigned char c = (unsigned char)*t.t.p;
  if (isdigit(c) || (c == '.' && t.t.n > 1 && isdigit((unsigned char)t.t.p[1])))
    return true;
  if (t.kind == 1 && (kw(t.t, "TRUE") || kw(t.t, "FALSE") || kw(t.t, "NULL")))
    return true;
  if (t.kind != 2)
    return false;
  if (c == '\'' || (d == SEQUEL_MYSQL && c == '"') ||
      (d == SEQUEL_CLICKHOUSE && clickhouse_quote(t.t.p, t.t.n, 0) == 0x98))
    return true;
  if (c == '$' && d != SEQUEL_SQLITE && dollar_tag_end(t.t.p, t.t.n, 0, d))
    return true;
  return c != '"' && c != '`' && c != '[' && c != '{' && c != '$' && c != '%' && c != '?' && c != ':' && c != '@' &&
         !(d == SEQUEL_CLICKHOUSE && clickhouse_quote(t.t.p, t.t.n, 0) == 0x9c);
}
