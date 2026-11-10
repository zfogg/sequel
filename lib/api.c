static void utf8_cut(char *s, size_t limit) {
  size_t n = strlen(s);
  if (n <= limit)
    return;
  limit = sequel_utf8_prefix(s, n, limit);
  s[limit] = 0;
  while (limit && isspace((unsigned char)s[limit - 1]))
    s[--limit] = 0;
}

static void normalize_sanitized_text(char *text) {
  size_t read = 0, write = 0;
  bool pending_space = false, line_comment = false, quoted = false;
  char quote = 0;
  while (text[read]) {
    char c = text[read];
    if (line_comment) {
      text[write++] = c;
      ++read;
      if (c == '\n' || c == '\r')
        line_comment = false;
      continue;
    }
    if (quoted) {
      text[write++] = c;
      ++read;
      if (c == quote && (read < 2 || text[read - 2] != '\\'))
        quoted = false;
      continue;
    }
    if ((c == '-' && text[read + 1] == '-') || (c == '/' && text[read + 1] == '*')) {
      if (pending_space && write)
        text[write++] = ' ';
      pending_space = false;
      line_comment = c == '-';
      text[write++] = text[read++];
      text[write++] = text[read++];
      continue;
    }
    if (c == '\'' || c == '"' || c == '`') {
      if (pending_space && write)
        text[write++] = ' ';
      pending_space = false;
      quoted = true;
      quote = c;
      text[write++] = text[read++];
      continue;
    }
    if (isspace((unsigned char)c)) {
      pending_space = write != 0;
      ++read;
      continue;
    }
    if (pending_space && write)
      text[write++] = ' ';
    pending_space = false;
    text[write++] = text[read++];
  }
  while (write && isspace((unsigned char)text[write - 1]))
    --write;
  text[write] = 0;
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
  if (dialect != SEQUEL_MYSQL && strstr(sql, "_utf8mb4'"))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  {
    size_t open_brackets = 0, close_brackets = 0;
    for (size_t index = 0; index < n; ++index) {
      open_brackets += sql[index] == '[';
      close_brackets += sql[index] == ']';
    }
    if (open_brackets > close_brackets)
      return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  sequel_parse_options_t options;
  sequel_parse_options_init(&options);
  options.dialect = dialect;
  options.retain_comments = true;
  options.retain_whitespace = true;
  sequel_source_view_t source = {sql, n};
  sequel_lexed_document_t *lexed = NULL;
  sequel_status_t status = sequel_lex(&source, &options, NULL, &lexed);
  if (status != SEQUEL_OK)
    return status;
  sequel_text_result_t result = {0};
  status = sequel_sanitize_tokens(lexed, flags, &result);
  sequel_lexed_document_free(lexed);
  if (status != SEQUEL_OK)
    return status;
  if (collapse_in_lists && (flags & SEQUEL_REPLACE_LITERALS))
    collapse_in(result.data, dialect);
  if (flags & SEQUEL_SANITIZE_REMOVE_COMMENTS)
    normalize_sanitized_text(result.data);
  *truncated = result.length > limit;
  utf8_cut(result.data, limit);
  result.length = strlen(result.data);
  if (result.length >= capacity) {
    sequel_text_result_free(&result);
    *truncated = false;
    return SEQUEL_ERR_OVERFLOW;
  }
  memcpy(out, result.data, result.length + 1);
  sequel_text_result_free(&result);
  return SEQUEL_OK;
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
  parse_options.tolerant = true;
  parse_options.recover_errors = true;
  sequel_source_view_t source = {sql, length};
  sequel_syntax_document_t *syntax = NULL;
  sequel_query_document_t *query = NULL;
  sequel_text_result_t sanitized = {0}, summary = {0};
  sequel_status_t status = sequel_parse_sql(&source, &parse_options, NULL, &syntax);
  if (status != SEQUEL_OK)
    goto done;
  size_t issue_count = sequel_syntax_issue_count(syntax);
  if (issue_count && !parse_options.recover_errors) {
    status = SEQUEL_ERR_INVALID_ARGUMENT;
    goto done;
  }
  status = sequel_build_query_document(syntax, &parse_options, &query);
  if (status != SEQUEL_OK)
    goto done;
  status = sequel_sanitize_tokens(sequel_syntax_lexed(syntax),
                                  SEQUEL_SANITIZE_REMOVE_COMMENTS | SEQUEL_SANITIZE_REPLACE_LITERALS, &sanitized);
  if (status != SEQUEL_OK)
    goto done;
  if (options && !options->keep_in_lists)
    collapse_in(sanitized.data, parse_options.dialect);
  sequel_summary_options_t summary_options;
  sequel_summary_options_init(&summary_options);
  summary_options.max_length = 0;
  summary_options.include_bind_count = false;
  summary_options.redact_relations = true;
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
    bool same_operation = true;
    for (size_t statement_index = 1; statement_index < out->statement_count; ++statement_index)
      if (statements[statement_index].operation != statements[0].operation) {
        same_operation = false;
        break;
      }
    if (!same_operation)
      out->operation[0] = 0;
    else {
      sequel_source_view_t query_source = sequel_query_source(query);
      const sequel_statement_info_t *first = &statements[0];
      size_t start = first->range.start, end = first->range.end;
      if (end <= query_source.length && end >= start) {
        size_t amount = end - start < sizeof(out->operation) - 1 ? end - start : sizeof(out->operation) - 1;
        size_t cursor = start;
        while (cursor < end && isspace((unsigned char)query_source.data[cursor]))
          ++cursor;
        while (cursor < end && query_source.data[cursor] == '(') {
          ++cursor;
          while (cursor < end && isspace((unsigned char)query_source.data[cursor]))
            ++cursor;
        }
        if (cursor + 4 <= end && toupper((unsigned char)query_source.data[cursor]) == 'W' &&
            toupper((unsigned char)query_source.data[cursor + 1]) == 'I' &&
            toupper((unsigned char)query_source.data[cursor + 2]) == 'T' &&
            toupper((unsigned char)query_source.data[cursor + 3]) == 'H') {
          size_t cte_count = sequel_query_cte_count(query);
          if (cte_count)
            cursor = sequel_query_ctes(query)[cte_count - 1].range.end;
          while (cursor < end && (isspace((unsigned char)query_source.data[cursor]) || query_source.data[cursor] == ')'))
            ++cursor;
        }
        amount = end - cursor < sizeof(out->operation) - 1 ? end - cursor : sizeof(out->operation) - 1;
        size_t written = 0;
        while (written < amount && !isspace((unsigned char)query_source.data[cursor + written]) &&
               query_source.data[cursor + written] != '(' &&
               !(cursor + written + 2 < end && (unsigned char)query_source.data[cursor + written] == 0xe2 &&
                 (unsigned char)query_source.data[cursor + written + 1] == 0x80 &&
                 (unsigned char)query_source.data[cursor + written + 2] >= 0x98 &&
                 (unsigned char)query_source.data[cursor + written + 2] <= 0x9d)) {
          out->operation[written] = (char)toupper((unsigned char)query_source.data[cursor + written]);
          ++written;
        }
        out->operation[written] = 0;
      }
    }
  }
  out->truncated =
      text_length > (options && options->max_query_text_bytes ? options->max_query_text_bytes : SEQUEL_TEXT_MAX);
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
