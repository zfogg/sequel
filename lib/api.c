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

static sequel_status_t inspect_document(const char *sql, size_t length, const sequel_options_t *options,
                                        sequel_metadata_t *out) {
  sequel_parse_options_t parse_options;
  sequel_parse_options_init(&parse_options);
  parse_options.dialect = options ? options->dialect : SEQUEL_GENERIC;
  parse_options.retain_comments = true;
  parse_options.retain_whitespace = true;
  sequel_source_view_t source = {sql, length};
  sequel_syntax_document_t *syntax = NULL;
  sequel_query_document_t *query = NULL;
  sequel_text_result_t sanitized = {0}, summary = {0};
  sequel_status_t status = sequel_parse_sql(&source, &parse_options, NULL, &syntax);
  if (status != SEQUEL_OK)
    goto done;
  size_t issue_count = sequel_syntax_issue_count(syntax);
  if (issue_count) {
    status = SEQUEL_ERR_INVALID_ARGUMENT;
    goto done;
  }
  status = sequel_build_query_document(syntax, &parse_options, &query);
  if (status != SEQUEL_OK)
    goto done;
  status = sequel_sanitize_tokens(sequel_syntax_lexed(syntax), SEQUEL_SANITIZE_REMOVE_COMMENTS |
                                                                  SEQUEL_SANITIZE_REPLACE_LITERALS,
                                  &sanitized);
  if (status != SEQUEL_OK)
    goto done;
  if (options && !options->keep_in_lists)
    collapse_in(sanitized.data, parse_options.dialect);
  sequel_summary_options_t summary_options;
  sequel_summary_options_init(&summary_options);
  summary_options.max_length = 0;
  status = sequel_summarize_query(query, &summary_options, &summary);
  if (status != SEQUEL_OK)
    goto done;
  if (summary.length > SEQUEL_SUMMARY_MAX) {
    size_t limit = sequel_utf8_prefix(summary.data, summary.length, SEQUEL_SUMMARY_MAX - 3);
    char *short_summary = realloc(summary.data, limit + 4);
    if (!short_summary) {
      status = SEQUEL_ERR_NOMEM;
      goto done;
    }
    summary.data = short_summary;
    memcpy(summary.data + limit, "…", 3);
    summary.data[limit + 3] = 0;
    summary.length = limit + 3;
    summary.capacity = limit + 4;
  }
  size_t text_length = options && options->omit_query_text ? 0 : sanitized.length;
  size_t summary_length = summary.length;
  char *storage = malloc(text_length + summary_length + 2);
  if (!storage) {
    status = SEQUEL_ERR_NOMEM;
    goto done;
  }
  memcpy(storage, sanitized.data, text_length);
  storage[text_length] = 0;
  memcpy(storage + text_length + 1, summary.data, summary_length + 1);
  out->_storage = storage;
  out->text = storage;
  out->summary = storage + text_length + 1;
  out->statement_count = sequel_query_statement_count(query);
  const sequel_statement_info_t *statements = sequel_query_statements(query);
  if (out->statement_count) {
    sequel_source_view_t query_source = sequel_query_source(query);
    const sequel_statement_info_t *first = &statements[0];
    size_t start = first->range.start, end = first->range.end;
    if (end <= query_source.length && end >= start) {
      size_t amount = end - start < sizeof(out->operation) - 1 ? end - start : sizeof(out->operation) - 1;
      size_t cursor = start;
      while (cursor < end && isspace((unsigned char)query_source.data[cursor]))
        ++cursor;
      amount = end - cursor < sizeof(out->operation) - 1 ? end - cursor : sizeof(out->operation) - 1;
      size_t written = 0;
      while (written < amount && !isspace((unsigned char)query_source.data[cursor + written]) &&
             query_source.data[cursor + written] != '(') {
        out->operation[written] = (char)toupper((unsigned char)query_source.data[cursor + written]);
        ++written;
      }
      out->operation[written] = 0;
    }
  }
  out->truncated = text_length > (options && options->max_query_text_bytes ? options->max_query_text_bytes
                                                                            : SEQUEL_TEXT_MAX);
  if (out->truncated) {
    size_t limit = options->max_query_text_bytes ? options->max_query_text_bytes : SEQUEL_TEXT_MAX;
    utf8_cut((char *)out->text, limit);
  }
  status = SEQUEL_OK;
done:
  sequel_text_result_free(&sanitized);
  sequel_text_result_free(&summary);
  sequel_query_document_free(query);
  sequel_syntax_document_free(syntax);
  return status;
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
  return inspect_document(sql, n, &o, out);
}
