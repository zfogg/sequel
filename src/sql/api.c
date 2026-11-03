static void utf8_cut(char *s, size_t limit) {
  size_t n = strlen(s);
  if (n <= limit)
    return;
  limit = sequel_utf8_prefix(s, n, limit);
  s[limit] = 0;
  while (limit && isspace((unsigned char)s[limit - 1]))
    s[--limit] = 0;
}
static void collapse_in(char *s, sequel_dialect_t dialect) {
  size_t n = strlen(s);
  for (size_t i = 0; (sequel_work_tick(1), i + 2 < n); ++i) {
    /* Retained comments and quoted identifiers are opaque to IN collapsing. */
    unsigned char c = (unsigned char)s[i];
    if (dialect == SEQUEL_CLICKHOUSE && clickhouse_quote(s, n, i)) {
      size_t end = clickhouse_quoted_end(s, n, i);
      if (end) {
        i = end - 1;
        continue;
      }
    }
    if (c == '"' || c == '`' || (c == '[' && (dialect == SEQUEL_GENERIC || dialect == SEQUEL_SQLITE))) {
      size_t end = quoted(s, n, i, c == '[' ? ']' : (char)c, dialect == SEQUEL_CLICKHOUSE);
      if (end) {
        i = end - 1;
        continue;
      }
    }
    if ((c == '-' && s[i + 1] == '-') || (dialect == SEQUEL_CLICKHOUSE && c == '/' && s[i + 1] == '/') ||
        ((dialect == SEQUEL_MYSQL || dialect == SEQUEL_CLICKHOUSE) && c == '#')) {
      while (i < n && s[i] != '\n')
        ++i;
      continue;
    }
    if (c == '/' && s[i + 1] == '*') {
      unsigned depth = 1;
      i += 2;
      while (i + 1 < n && depth) {
        if (s[i] == '/' && s[i + 1] == '*') {
          ++depth;
          i += 2;
        } else if (s[i] == '*' && s[i + 1] == '/') {
          --depth;
          i += 2;
        } else
          ++i;
      }
      --i;
      continue;
    }

    if (!equal(s + i, 2, "IN") || (i && part((unsigned char)s[i - 1])) || (i + 2 < n && part((unsigned char)s[i + 2])))
      continue;
    size_t j = i + 2;
    while (j < n && isspace((unsigned char)s[j]))
      ++j;
    if (j == n || s[j] != '(')
      continue;
    size_t k = j + 1, count = 0;
    bool comma = false;
    for (; (sequel_work_tick(1), 1);) {
      while (k < n && isspace((unsigned char)s[k]))
        ++k;
      if (k == n || s[k] != '?')
        break;
      ++count;
      ++k;
      while (k < n && isspace((unsigned char)s[k]))
        ++k;
      if (k < n && s[k] == ',') {
        comma = true;
        ++k;
        continue;
      }
      comma = false;
      break;
    }
    if (count > 1 && !comma && k < n && s[k] == ')') {
      memmove(s + j + 3, s + k + 1, n - k);
      memcpy(s + j, "(?)", 3);
      n = strlen(s);
    }
  }
}
sequel_status_t sequel_sanitize_with_flags(const char *sql, sequel_dialect_t dialect, unsigned flags,
                                           bool collapse_in_lists, size_t limit, char *out, size_t capacity,
                                           bool *truncated) {
  if (!sql || !out || !truncated)
    return SEQUEL_ERR_NULL;
  *truncated = false;
  if (capacity)
    *out = 0;
  if (dialect < SEQUEL_GENERIC || dialect > SEQUEL_CLICKHOUSE || capacity == 0 || (flags & ~SEQUEL_SANITIZE_ALL))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (!limit)
    limit = SEQUEL_TEXT_MAX;
  size_t n = 0;
  while (n <= INPUT_MAX && sql[n])
    ++n;
  if (n > INPUT_MAX)
    return SEQUEL_ERR_OVERFLOW;
  if (!sequel_utf8_valid(sql, n))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  char *safe = calloc(n + 1, 1);
  if (!safe)
    return SEQUEL_ERR_NOMEM;
  output text_output = {safe, 0, n + 1};
  sequel_status_t status = SEQUEL_ERR_INVALID_ARGUMENT;
  if (!sanitize_policy(sql, n, dialect, false, (flags & SEQUEL_REMOVE_COMMENTS) != 0,
                       (flags & SEQUEL_REPLACE_LITERALS) != 0, &text_output))
    goto done;
  if (collapse_in_lists && (flags & SEQUEL_REPLACE_LITERALS))
    collapse_in(safe, dialect);
  *truncated = strlen(safe) > limit;
  utf8_cut(safe, limit);
  if (strlen(safe) >= capacity) {
    *truncated = false;
    status = SEQUEL_ERR_OVERFLOW;
    goto done;
  }
  memcpy(out, safe, strlen(safe) + 1);
  status = SEQUEL_OK;
done:
  free(safe);
  return status;
}
sequel_status_t sequel_sanitize(const char *sql, sequel_dialect_t dialect, bool collapse_in_lists, size_t limit,
                                char *out, size_t capacity, bool *truncated) {
  return sequel_sanitize_with_flags(sql, dialect, SEQUEL_SANITIZE_ALL, collapse_in_lists, limit, out, capacity,
                                    truncated);
}

void sequel_metadata_free(sequel_metadata_t *metadata) {
  if (metadata) {
    free(metadata->_storage);
    *metadata = (sequel_metadata_t){.text = "", .summary = "SQL"};
  }
}

sequel_status_t sequel_inspect(const char *sql, const sequel_options_t *options, sequel_metadata_t *out) {
  if (!out)
    return SEQUEL_ERR_NULL;
  *out = (sequel_metadata_t){.text = "", .summary = "SQL"};
  if (!sql)
    return SEQUEL_ERR_NULL;
  sequel_options_t o = options ? *options : (sequel_options_t){0};
  if (o.dialect > SEQUEL_CLICKHOUSE || o.dialect < SEQUEL_GENERIC || o.max_query_text_bytes > SEQUEL_TEXT_MAX)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  size_t n = 0;
  while (n <= INPUT_MAX && sql[n])
    ++n;
  if (n > INPUT_MAX)
    return SEQUEL_ERR_OVERFLOW;
  if (!sequel_utf8_valid(sql, n))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  size_t summary_capacity = n < 64 ? 64 : n + 1;
  if (summary_capacity > SEQUEL_SUMMARY_MAX + 1)
    summary_capacity = SEQUEL_SUMMARY_MAX + 1;
  char *safe = calloc(n + 1, 1), *shape = calloc(n + 1, 1), *summary_text = calloc(summary_capacity, 1);
  token *tokens = NULL;
  if (!safe || !shape || !summary_text) {
    free(safe);
    free(shape);
    free(tokens);
    free(summary_text);
    return SEQUEL_ERR_NOMEM;
  }
  output a = {safe, 0, n + 1}, b = {shape, 0, n + 1};
  sequel_status_t status = SEQUEL_ERR_INVALID_ARGUMENT;
  if (!sanitize(sql, n, o.dialect, false, &a) || !sanitize(sql, n, o.dialect, true, &b))
    goto done;
  size_t count = tokenize(shape, &tokens, o.dialect);
  if (count == SIZE_MAX) {
    status = SEQUEL_ERR_NOMEM;
    goto done;
  }
  if (!count) {
    status = *safe ? SEQUEL_ERR_OVERFLOW : SEQUEL_OK;
    goto done;
  }
retry_summary:;
  output summary = {summary_text, 0, summary_capacity};
  *summary_text = 0;
  out->statement_count = 0;
  *out->operation = 0;
  bool mixed_operations = false;
  for (size_t start = 0; (sequel_work_tick(1), start < count);) {
    size_t end = start, nesting = 0;
    for (; (sequel_work_tick(1), end < count); ++end) {
      if (sym(tokens[end], '('))
        ++nesting;
      else if (sym(tokens[end], ')') && nesting)
        --nesting;
      else if (!nesting && sym(tokens[end], ';'))
        break;
    }
    if (end > start) {
      ++out->statement_count;
      token *statement = tokens + start;
      size_t length = end - start;
      size_t op = 0;
      while (op < length && sym(statement[op], '('))
        ++op;
      if (kw(statement[0], "WITH"))
        for (op = 1; (sequel_work_tick(1), op < length); ++op) {
          if (sym(statement[op], '(')) {
            op = close_group(statement, length, op);
            continue;
          }
          if (kw(statement[op], "SELECT") || kw(statement[op], "INSERT") || kw(statement[op], "UPDATE") ||
              kw(statement[op], "DELETE") || kw(statement[op], "MERGE") || kw(statement[op], "VALUES"))
            break;
        }
      char current_operation[sizeof(out->operation)] = {0};
      if (op < length && statement[op].word) {
        output p = {current_operation, 0, sizeof(current_operation)};
        upper(&p, statement[op]);
      }
      if (out->statement_count == 1)
        strcpy(out->operation, current_operation);
      else if (strcmp(out->operation, current_operation))
        mixed_operations = true;
      if (mixed_operations)
        *out->operation = 0;
      if (summary.n)
        text(&summary, "; ");
      summarize(&summary, statement, length, 0);
    }
    start = end < count ? end + 1 : count;
  }
  if (summary.overflow && summary_capacity < SEQUEL_SUMMARY_MAX + 1) {
    size_t next = summary_capacity > (SEQUEL_SUMMARY_MAX + 1) / 2 ? SEQUEL_SUMMARY_MAX + 1 : summary_capacity * 2;
    char *grown = realloc(summary_text, next);
    if (!grown) {
      status = SEQUEL_ERR_NOMEM;
      goto done;
    }
    summary_text = grown;
    summary_capacity = next;
    goto retry_summary;
  }
  if (!*summary_text)
    strcpy(summary_text, "SQL");
  if (summary.overflow) {
    utf8_cut(summary_text, SEQUEL_SUMMARY_MAX - 3);
    strcat(summary_text, "…");
  }
  if (!o.keep_in_lists)
    collapse_in(safe, o.dialect);
  if (!o.omit_query_text) {
    size_t limit = o.max_query_text_bytes ? o.max_query_text_bytes : SEQUEL_TEXT_MAX;
    out->truncated = strlen(safe) > limit;
    utf8_cut(safe, limit);
  }
  size_t text_length = o.omit_query_text ? 0 : strlen(safe);
  size_t summary_length = strlen(summary_text);
  char *storage = malloc(text_length + summary_length + 2);
  if (!storage) {
    status = SEQUEL_ERR_NOMEM;
    goto done;
  }
  memcpy(storage, safe, text_length);
  storage[text_length] = 0;
  memcpy(storage + text_length + 1, summary_text, summary_length + 1);
  out->_storage = storage;
  out->text = storage;
  out->summary = storage + text_length + 1;
  status = SEQUEL_OK;
done:
  free(safe);
  free(shape);
  free(tokens);
  free(summary_text);
  return status;
}
