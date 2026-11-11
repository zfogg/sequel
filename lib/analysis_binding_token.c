/* Classify one source token as a canonical binding marker. */
static bool analysis_bind_token(format_token t, sequel_dialect_t d, const char **key, size_t *length, size_t *number,
                                sequel_bind_style_t *style) {
  const char *p = t.t.p;
  size_t n = t.t.n;
  *key = p;
  *length = n;
  *number = 0;
  *style = SEQUEL_BIND_POSITIONAL;
  if (d == SEQUEL_MYSQL && p[0] == '@')
    return false;
  if (n > 1 && p[0] == '@' && p[1] == '@')
    return false;
  if (n == 2 && p[0] == '%' && p[1] == 's') {
    *length = 0;
    return true;
  }
  if (n >= 4 && p[0] == '%' && p[1] == '(' && p[n - 2] == ')' && p[n - 1] == 's') {
    *key = p + 2;
    *length = n - 4;
    *style = SEQUEL_BIND_NAMED;
    return true;
  }
  if (n > 2 && p[0] == '{' && d == SEQUEL_CLICKHOUSE) {
    size_t i = 1;
    while (i < n && p[i] != ':' && !isspace((unsigned char)p[i]))
      ++i;
    *key = p + 1;
    *length = i - 1;
    *style = SEQUEL_BIND_TYPED;
    return true;
  }
  if (p[0] == '?' && d != SEQUEL_POSTGRESQL) {
    if (n == 1) {
      *length = 0;
      return true;
    }
  } else if (!(p[0] == '$' || p[0] == ':' || p[0] == '@') || n < 2)
    return false;
  if (p[0] == ':' && (p[1] == ':' || p[1] == '='))
    return false;
  if (p[0] == '$' && d != SEQUEL_SQLITE && dollar_tag_end(p, n, 0, d))
    return false;
  bool digits = true;
  for (size_t i = 1; i < n; i++) {
    sequel_work_tick(1);
    if (!isdigit((unsigned char)p[i]))
      digits = false;
  }
  if (digits && ((p[0] == '$' && d != SEQUEL_SQLITE) || p[0] == '?')) {
    *style = SEQUEL_BIND_NUMBERED;
    for (size_t i = 1; i < n; i++) {
      sequel_work_tick(1);
      size_t digit = (size_t)(p[i] - '0');
      if (*number > (SIZE_MAX - digit) / 10)
        sequel_abort(SEQUEL_ERR_INVALID_ARGUMENT);
      *number = *number * 10 + digit;
    }
    if (!*number)
      sequel_abort(SEQUEL_ERR_INVALID_ARGUMENT);
    return true;
  }
  if (p[0] == '?')
    return false;
  if (!letter((unsigned char)p[1]) && !(d == SEQUEL_SQLITE && isdigit((unsigned char)p[1])))
    return false;
  *style = SEQUEL_BIND_NAMED;
  if (d != SEQUEL_SQLITE) {
    *key = p + 1;
    *length = n - 1;
  }
  return true;
}
