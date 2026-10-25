sequel_status_t sequel_compact(const char *sql, sequel_dialect_t dialect, char *out, size_t capacity) {
  if (!sql || !out)
    return SEQUEL_ERR_NULL;
  if (!capacity || dialect < SEQUEL_GENERIC || dialect > SEQUEL_CLICKHOUSE)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  *out = 0;
  size_t n = 0;
  while (n <= INPUT_MAX && sql[n])
    ++n;
  if (n > INPUT_MAX)
    return SEQUEL_ERR_OVERFLOW;
  if (!sequel_utf8_valid(sql, n))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  format_token *ts = calloc(n < TOKEN_MAX ? n + 1 : TOKEN_MAX, sizeof(*ts));
  if (!ts)
    return SEQUEL_ERR_NOMEM;
  size_t count = format_scan(sql, n, (sequel_dialect_t)dialect, ts);
  if (count == SIZE_MAX) {
    free(ts);
    return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  output b = {out, 0, capacity};
  const char *previous = sql;
  for (size_t i = 0; i < count; ++i) {
    token t = ts[i].t;
    size_t gap = (size_t)(t.p - previous);
    if (gap) {
      if (i == 0 || memchr(previous, '\n', gap) || memchr(previous, '\r', gap))
        put(&b, previous, gap);
      else
        text(&b, " ");
    }
    emit_token(&b, t);
    previous = t.p + t.n;
  }
  put(&b, previous, n - (size_t)(previous - sql));
  free(ts);
  if (b.overflow) {
    *out = 0;
    return SEQUEL_ERR_OVERFLOW;
  }
  return SEQUEL_OK;
}
