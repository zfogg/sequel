/* Unified analysis uses the existing dialect lexer. It hashes framed token
 * classes and produces numeric provenance while values are replaced. */
static int shared_token_kind(sequel_token_kind_t kind) {
  switch (kind) {
  case SEQUEL_TOKEN_LINE_COMMENT:
    return 3;
  case SEQUEL_TOKEN_BLOCK_COMMENT:
    return 4;
  case SEQUEL_TOKEN_IDENTIFIER:
  case SEQUEL_TOKEN_KEYWORD:
  case SEQUEL_TOKEN_BOOLEAN:
  case SEQUEL_TOKEN_NULL:
    return 1;
  case SEQUEL_TOKEN_QUOTED_IDENTIFIER:
  case SEQUEL_TOKEN_STRING:
  case SEQUEL_TOKEN_PARAMETER:
    return 2;
  default:
    return 0;
  }
}

static sequel_status_t adapt_lexed_tokens(const sequel_lexed_document_t *lexed, format_token **out, size_t *count) {
  if (!lexed || !out || !count)
    return SEQUEL_ERR_NULL;
  *out = NULL;
  *count = 0;
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  sequel_source_view_t source = sequel_lexed_source(lexed);
  size_t capacity = 0;
  size_t previous_end = 0;
  bool gap = false;
  bool pending_newline = false;
  for (size_t i = 0; i < token_count; ++i) {
    const sequel_token_t *token = &tokens[i];
    if (token->kind == SEQUEL_TOKEN_EOF)
      break;
    if (token->range.end > source.length || token->range.start > token->range.end) {
      goto invalid;
    }
    if (token->kind == SEQUEL_TOKEN_WHITESPACE) {
      for (size_t offset = token->range.start; offset < token->range.end; ++offset)
        pending_newline |= source.data[offset] == '\r' || source.data[offset] == '\n';
      previous_end = token->range.end;
      gap = true;
      continue;
    }
    if (*count == capacity) {
      size_t next = capacity ? capacity * 2 : 64;
      if (next < *count || next > SIZE_MAX / sizeof(**out)) {
        goto overflow;
      }
      format_token *grown = realloc(*out, next * sizeof(*grown));
      if (!grown)
        goto no_memory;
      *out = grown;
      capacity = next;
    }
    bool newline = false;
    for (size_t offset = previous_end; offset < token->range.start; ++offset)
      newline |= source.data[offset] == '\r' || source.data[offset] == '\n';
    int kind = shared_token_kind(token->kind);
    bool opening = kind == 0 && token->range.end == token->range.start + 1 &&
                   strchr("([{", source.data[token->range.start]) != NULL;
    bool previous_open = *count && (*out)[*count - 1].kind == 0 && (*out)[*count - 1].t.n == 1 &&
                         strchr("([{", *(*out)[*count - 1].t.p) != NULL;
    (*out)[(*count)++] = (format_token){.t = {.p = source.data + token->range.start,
                                              .n = token->range.end - token->range.start,
                                              .word = kind == 1,
                                              .space = gap,
                                              .clickhouse = sequel_lexed_dialect(lexed) == SEQUEL_CLICKHOUSE},
                                        .gap = gap,
                                        .newline = newline || pending_newline,
                                        .kind = kind,
                                        .matching = 0,
                                        .multiline = newline || (pending_newline && (!opening || previous_open)),
                                        .flat_width = (*count ? (*out)[*count - 1].flat_width : 0) +
                                                      token->range.end - token->range.start + 1,
                                        .source_offset = token->range.start,
                                        .rendered_offset = 0};
    previous_end = token->range.end;
    gap = false;
    pending_newline = false;
  }
  return SEQUEL_OK;
invalid:
  free(*out);
  *out = NULL;
  *count = 0;
  return SEQUEL_ERR_INVALID_ARGUMENT;
overflow:
  free(*out);
  *out = NULL;
  *count = 0;
  return SEQUEL_ERR_OVERFLOW;
no_memory:
  free(*out);
  *out = NULL;
  *count = 0;
  return SEQUEL_ERR_NOMEM;
}

static bool requires_template_scan(const char *sql, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    if (sql[i] == '%' && i + 1 < length && (sql[i + 1] == 's' || sql[i + 1] == '('))
      return true;
    if (sql[i] == '\\' && i + 1 < length && (sql[i + 1] == '\'' || sql[i + 1] == '"'))
      return true;
    if (sql[i] == '$' && i + 1 < length && sql[i + 1] == '$')
      return true;
    if (sql[i] == '[') {
      for (size_t j = i + 1; j < length && sql[j] != ']'; ++j)
        if (sql[j] == ':')
          return true;
    }
    if (i + 2 < length && (sql[i] == 'I' || sql[i] == 'i') && (sql[i + 1] == 'N' || sql[i + 1] == 'n')) {
      size_t next = i + 2;
      while (next < length && (sql[next] == ' ' || sql[next] == '\t' || sql[next] == '\r' || sql[next] == '\n'))
        ++next;
      if (next < length && sql[next] == '(')
        return true;
    }
  }
  return false;
}
static size_t close_group(token *t, size_t n, size_t i) {
  size_t depth = 0;
  for (size_t j = i; (sequel_work_tick(1), j < n); ++j) {
    if (sym(t[j], '('))
      ++depth;
    else if (sym(t[j], ')') && --depth == 0)
      return j;
  }
  return n;
}

static bool identifier(token t) {
  return (t.clickhouse && clickhouse_quote(t.p, t.n, 0) == 0x9c) || t.word || *t.p == '"' || *t.p == '`' || *t.p == '[';
}

static size_t name(output *b, token *t, size_t n, size_t i) {
  if (i >= n || !identifier(t[i]))
    return i;
  emit_token(b, t[i++]);
  while (i + 1 < n && sym(t[i], '.') && identifier(t[i + 1])) {
    text(b, ".");
    emit_token(b, t[i + 1]);
    i += 2;
  }
  return i;
}

static bool boundary(token t) {
  const char *words[] = {"FROM",  "WHERE",     "GROUP",  "HAVING",    "ORDER", "LIMIT", "OFFSET",  "FETCH", "FOR",
                         "UNION", "INTERSECT", "EXCEPT", "RETURNING", "SET",   "USING", "QUALIFY", "WINDOW"};
  if (t.clickhouse && (kw(t, "PREWHERE") || kw(t, "SETTINGS") || kw(t, "FORMAT") || kw(t, "SAMPLE") || kw(t, "VALUES")))
    return true;
  for (size_t i = 0; (sequel_work_tick(1), i < sizeof(words) / sizeof(*words)); ++i)
    if (kw(t, words[i]))
      return true;
  return sym(t, ';') || sym(t, ')');
}

static size_t clause_end(token *t, size_t n, size_t i) {
  unsigned cases = 0;
  for (; (sequel_work_tick(1), i < n); ++i) {
    if (sym(t[i], '(')) {
      i = close_group(t, n, i);
      if (i == n)
        return n;
      continue;
    }
    if (kw(t[i], "CASE")) {
      ++cases;
      continue;
    }
    if (kw(t[i], "END") && cases) {
      --cases;
      continue;
    }
    if (cases)
      continue;
    if (i && kw(t[i], "FROM") && kw(t[i - 1], "DISTINCT"))
      continue;
    if (i && kw(t[i], "GROUP") && kw(t[i - 1], "WITHIN"))
      continue;
    if (i + 1 < n && kw(t[i], "ON") && kw(t[i + 1], "CONFLICT"))
      break;
    if (boundary(t[i]))
      break;
  }
  return i < n ? i : n;
}

static bool query_start(token t) {
  return kw(t, "SELECT") || kw(t, "WITH") || kw(t, "INSERT") || kw(t, "UPDATE") || kw(t, "DELETE") || kw(t, "MERGE") ||
         kw(t, "VALUES") || kw(t, "EXPLAIN");
}

static void summarize(output *, token *, size_t, unsigned);

static void nested(output *b, token *t, size_t n, unsigned depth) {
  if (depth > 64) {
    item(b, "…");
    return;
  }
  for (size_t i = 0; (sequel_work_tick(1), i < n); ++i)
    if (sym(t[i], '(')) {
      size_t end = close_group(t, n, i);
      if (i + 1 < end && query_start(t[i + 1])) {
        if (i && (kw(t[i - 1], "EXISTS") || kw(t[i - 1], "IN"))) {
          if (i > 1 && kw(t[i - 2], "NOT"))
            item(b, "NOT");
          item(b, kw(t[i - 1], "IN") ? "IN" : "EXISTS");
        }
        item(b, "(");
        summarize(b, t + i + 1, end - i - 1, depth + 1);
        text(b, ")");
      } else
        nested(b, t + i + 1, end - i - 1, depth + 1);
      i = end;
    }
}

static bool join_modifier(token t) {
  const char *words[] = {"NATURAL", "LEFT", "RIGHT", "FULL", "OUTER",     "INNER",
                         "CROSS",   "SEMI", "ANTI",  "ASOF", "POSITIONAL"};
  if (t.clickhouse && (kw(t, "GLOBAL") || kw(t, "ANY") || kw(t, "ALL") || kw(t, "ARRAY")))
    return true;
  for (size_t i = 0; (sequel_work_tick(1), i < sizeof(words) / sizeof(*words)); ++i)
    if (kw(t, words[i]))
      return true;
  return false;
}

static size_t join_at(token *t, size_t n, size_t i) {
  size_t j = i;
  while (j < n && join_modifier(t[j]))
    ++j;
  if (j < n && kw(t[j], "JOIN")) {
    ++j;
    if (j < n && kw(t[j], "LATERAL"))
      ++j;
    return j;
  }
  return i;
}

static void expression(output *b, token *t, size_t n, unsigned depth) {
  size_t started = b->n;
  for (size_t i = 0; (sequel_work_tick(1), i < n); ++i) {
    if (t[i].space && b->n > started && b->p[b->n - 1] != ' ')
      text(b, " ");
    if (sym(t[i], '(')) {
      size_t end = close_group(t, n, i);
      text(b, "(");
      if (i + 1 < end && query_start(t[i + 1]))
        summarize(b, t + i + 1, end - i - 1, depth + 1);
      else if (depth < 64)
        expression(b, t + i + 1, end - i - 1, depth + 1);
      text(b, ")");
      i = end;
    } else if (isdigit((unsigned char)*t[i].p) ||
               (sym(t[i], '.') && i + 1 < n && isdigit((unsigned char)*t[i + 1].p))) {
      text(b, "?");
      if (sym(t[i], '.'))
        ++i;
    } else
      emit_token(b, t[i]);
  }
}

static void relations(output *, token *, size_t, unsigned);

static size_t relation(output *b, token *t, size_t n, size_t i, unsigned depth) {
  while (i < n && (kw(t[i], "ONLY") || kw(t[i], "LATERAL"))) {
    upper(b, t[i++]);
    text(b, " ");
  }
  if (i >= n) {
    text(b, "…");
    return i;
  }
  if (sym(t[i], '(')) {
    size_t end = close_group(t, n, i);
    text(b, "(");
    if (i + 1 < end && query_start(t[i + 1]))
      summarize(b, t + i + 1, end - i - 1, depth + 1);
    else
      relations(b, t + i + 1, end - i - 1, depth + 1);
    text(b, ")");
    i = end < n ? end + 1 : n;
  } else {
    size_t next = name(b, t, n, i);
    if (next == i) {
      text(b, "…");
      return i + 1;
    }
    i = next;
    if (i < n && sym(t[i], '(')) {
      size_t end = close_group(t, n, i);
      text(b, "(…)");
      i = end < n ? end + 1 : n;
    }
  }
  if (i + 1 < n && kw(t[i], "WITH") && kw(t[i + 1], "ORDINALITY")) {
    text(b, " WITH ORDINALITY");
    i += 2;
  }
  if (i < n && t[i].clickhouse && kw(t[i], "FINAL")) {
    text(b, " FINAL");
    ++i;
  }
  if (i < n && kw(t[i], "AS"))
    ++i;
  if (i < n && identifier(t[i]) && !boundary(t[i]) && !kw(t[i], "ON") && !kw(t[i], "WHEN") && !kw(t[i], "WITH") &&
      join_at(t, n, i) == i) {
    text(b, " AS ");
    emit_token(b, t[i++]);
    if (i < n && sym(t[i], '(')) {
      size_t end = close_group(t, n, i);
      expression(b, t + i, end - i + (end < n), depth + 1);
      i = end < n ? end + 1 : n;
    }
  }
  return i;
}

static void relations(output *b, token *t, size_t n, unsigned depth) {
  if (depth > 64) {
    text(b, "…");
    return;
  }
  size_t i = relation(b, t, n, 0, depth);
  while (i < n) {
    size_t next = join_at(t, n, i);
    if (next > i) {
      text(b, " ");
      for (; (sequel_work_tick(1), i < next); ++i) {
        upper(b, t[i]);
        text(b, " ");
      }
      i = relation(b, t, n, i, depth);
    } else if (sym(t[i], ',')) {
      text(b, ", ");
      i = relation(b, t, n, i + 1, depth);
    } else if (kw(t[i], "ON") || kw(t[i], "USING")) {
      text(b, " ");
      upper(b, t[i++]);
      text(b, " ");
      size_t start = i;
      int cases = 0;
      for (; (sequel_work_tick(1), i < n); ++i) {
        if (sym(t[i], '(')) {
          i = close_group(t, n, i);
          continue;
        }
        if (sym(t[i], '[')) {
          size_t depth_square = 1;
          while (++i < n && depth_square) {
            if (sym(t[i], '['))
              ++depth_square;
            else if (sym(t[i], ']'))
              --depth_square;
          }
          if (i < n)
            --i;
          continue;
        }
        if (kw(t[i], "CASE"))
          ++cases;
        else if (kw(t[i], "END") && cases)
          --cases;
        if (!cases && (join_at(t, n, i) > i || sym(t[i], ',')))
          break;
      }
      /* An unmatched group can advance the loop past its final token. */
      if (i > n)
        i = n;
      expression(b, t + start, i - start, depth + 1);
    } else
      ++i;
  }
}

static void bound(output *b, token *t, size_t n) {
  if (n == 1 && kw(t[0], "ALL")) {
    text(b, "ALL");
    return;
  }
  bool valid = n == 1 || (n == 2 && (sym(t[0], '-') || sym(t[0], '+')));
  if (valid) {
    token v = t[n - 1];
    for (size_t i = 0; (sequel_work_tick(1), i < v.n); ++i)
      if (!isdigit((unsigned char)v.p[i]))
        valid = false;
  }
  if (valid) {
    for (size_t i = 0; (sequel_work_tick(1), i < n); ++i)
      emit_token(b, t[i]);
  } else
    text(b, "?");
}

static void summarize(output *b, token *t, size_t n, unsigned depth) {
  if (depth > 64) {
    text(b, "…");
    return;
  }
  if (!n)
    return;
  size_t i = 0;
  if (t[0].clickhouse && kw(t[0], "WITH")) {
    /* Scalar aliases and CTEs share WITH in ClickHouse. Preserve the outer
     * query shape without exporting arbitrary scalar expressions. */
    for (size_t j = 1; (sequel_work_tick(1), j < n); ++j) {
      if (sym(t[j], '(')) {
        j = close_group(t, n, j);
        continue;
      }
      if (kw(t[j], "SELECT")) {
        text(b, "WITH … ");
        summarize(b, t + j, n - j, depth + 1);
        return;
      }
    }
  }
  if (kw(t[0], "WITH")) {
    text(b, "WITH ");
    i = 1;
    if (i < n && kw(t[i], "RECURSIVE")) {
      text(b, "RECURSIVE ");
      ++i;
    }
    for (; (sequel_work_tick(1), 1);) {
      size_t next = name(b, t, n, i);
      if (next == i) {
        text(b, "SQL");
        return;
      }
      i = next;
      if (i < n && sym(t[i], '('))
        i = close_group(t, n, i) + 1;
      if (i >= n || !kw(t[i], "AS")) {
        text(b, " SQL");
        return;
      }
      ++i;
      text(b, " AS");
      if (i < n && kw(t[i], "NOT")) {
        text(b, " NOT");
        ++i;
      }
      if (i < n && kw(t[i], "MATERIALIZED")) {
        text(b, " MATERIALIZED");
        ++i;
      }
      if (i >= n || !sym(t[i], '(')) {
        text(b, " SQL");
        return;
      }
      size_t end = close_group(t, n, i);
      text(b, " (");
      summarize(b, t + i + 1, end - i - 1, depth + 1);
      text(b, ")");
      i = end < n ? end + 1 : n;
      if (i >= n || !sym(t[i], ','))
        break;
      text(b, ", ");
      ++i;
    }
    text(b, " ");
  }
  if (i >= n)
    return;
  if (kw(t[i], "EXPLAIN")) {
    text(b, "EXPLAIN");
    ++i;
    if (i < n && sym(t[i], '(')) {
      size_t end = close_group(t, n, i);
      for (size_t j = i + 1; (sequel_work_tick(1), j < end); ++j)
        if (kw(t[j], "ANALYZE")) {
          text(b, " ANALYZE");
          break;
        }
      i = end < n ? end + 1 : n;
    }
    while (i < n && (kw(t[i], "ANALYZE") || kw(t[i], "VERBOSE") || kw(t[i], "QUERY") || kw(t[i], "PLAN"))) {
      if (kw(t[i], "ANALYZE"))
        text(b, " ANALYZE");
      ++i;
    }
    if (i < n) {
      text(b, " ");
      summarize(b, t + i, n - i, depth + 1);
    }
    return;
  }
  token operation_token = t[i];
  if (sym(t[i], '(')) {
    size_t end = close_group(t, n, i);
    text(b, "(");
    summarize(b, t + i + 1, end - i - 1, depth + 1);
    text(b, ")");
    i = end < n ? end + 1 : n;
  } else {
    token op = t[i++];
    const char *operations[] = {"SELECT", "INSERT", "UPDATE",   "DELETE", "MERGE",   "COPY",   "CALL",   "TRUNCATE",
                                "CREATE", "ALTER",  "DROP",     "VACUUM", "ANALYZE", "PRAGMA", "SHOW",   "SET",
                                "BEGIN",  "COMMIT", "ROLLBACK", "VALUES", "REPLACE", "PIVOT",  "UNPIVOT"};
    bool supported = false;
    for (size_t j = 0; (sequel_work_tick(1), j < sizeof(operations) / sizeof(*operations)); ++j)
      if (kw(op, operations[j]))
        supported = true;
    if (!supported) {
      text(b, "SQL");
      return;
    }
    upper(b, op);
    if (kw(op, "PIVOT") || kw(op, "UNPIVOT")) {
      if (i < n) {
        text(b, " ");
        name(b, t, n, i);
      }
      return;
    }
    if (kw(op, "CREATE")) {
      size_t j = i;
      bool unique = j < n && kw(t[j], "UNIQUE");
      if (unique)
        ++j;
      if (j < n && kw(t[j], "INDEX")) {
        ++j;
        if (j < n && kw(t[j], "CONCURRENTLY"))
          ++j;
        if (j + 2 < n && kw(t[j], "IF") && kw(t[j + 1], "NOT") && kw(t[j + 2], "EXISTS"))
          j += 3;
        char index_name[4097] = {0};
        output index = {index_name, 0, sizeof(index_name)};
        size_t next = name(&index, t, n, j);
        if (next > j && next < n && kw(t[next], "ON")) {
          item(b, unique ? "UNIQUE INDEX" : "INDEX");
          item(b, index_name);
          item(b, "ON ...");
          if (index.overflow)
            b->overflow = true;
          return;
        }
      }
    }
    if (kw(op, "SELECT")) {
      if (i < n && (kw(t[i], "DISTINCT") || kw(t[i], "ALL"))) {
        text(b, " ");
        upper(b, t[i++]);
        if (i < n && kw(t[i], "ON")) {
          item(b, "ON (…)");
          ++i;
          if (i < n && sym(t[i], '('))
            i = close_group(t, n, i) + 1;
        }
      }
      size_t end = clause_end(t, n, i);
      char function_text[4097] = {0};
      output functions = {function_text, 0, sizeof(function_text)};
      for (size_t j = i; (sequel_work_tick(1), j < end); ++j) {
        if (sym(t[j], '(')) {
          j = close_group(t, n, j);
          continue;
        }
        char fn[1024] = {0};
        output f = {fn, 0, sizeof(fn)};
        size_t next = name(&f, t, end, j);
        if (next > j && next < end && sym(t[next], '(') && !kw(t[j], "EXISTS") && !kw(t[j], "CAST")) {
          size_t close = close_group(t, n, next);
          if (functions.n)
            text(&functions, ", ");
          text(&functions, fn);
          text(&functions, close == next + 1 ? "()" : "(…)");
          j = next - 1;
        }
      }
      if (end < n && kw(t[end], "FROM"))
        item(b, "…");
      else if (functions.n)
        item(b, function_text);
      else {
        char scalar_text[65] = {0};
        output scalar = {scalar_text, 0, sizeof(scalar_text)};
        bool simple = end > i;
        for (size_t j = i; (sequel_work_tick(1), j < end); ++j)
          if (sym(t[j], ',') || kw(t[j], "SELECT") || kw(t[j], "WITH") || kw(t[j], "CASE") ||
              (!t[j].word && strchr("\"`[", *t[j].p)))
            simple = false;
        if (simple)
          expression(&scalar, t + i, end - i, depth + 1);
        item(b, simple && scalar.n && !scalar.overflow ? scalar_text : "…");
      }
      nested(b, t + i, end - i, depth + 1);
      for (size_t j = i; (sequel_work_tick(1), j < end); ++j) {
        if (sym(t[j], '(')) {
          j = close_group(t, n, j);
          continue;
        }
        if (kw(t[j], "OVER"))
          item(b, "OVER (…)");
        if (kw(t[j], "FILTER"))
          item(b, "FILTER (…)");
        if (j + 1 < end && kw(t[j], "WITHIN") && kw(t[j + 1], "GROUP"))
          item(b, "WITHIN GROUP (…)");
      }
      i = end;
    } else if (kw(op, "UPDATE")) {
      text(b, " ");
      size_t end = clause_end(t, n, i);
      relations(b, t + i, end - i, depth + 1);
      i = end;
    } else if (kw(op, "VALUES")) {
      item(b, "…");
      return;
    } else if (kw(op, "CALL") || kw(op, "COPY") || kw(op, "VACUUM") || kw(op, "ANALYZE") || kw(op, "TRUNCATE")) {
      if (i < n && kw(t[i], "TABLE"))
        ++i;
      text(b, " ");
      i = name(b, t, n, i);
      if (kw(op, "CALL"))
        return;
    }
  }
  while (i < n) {
    token v = t[i];
    if (sym(v, ';') || sym(v, ')'))
      break;
    if (kw(v, "UNION") || kw(v, "INTERSECT") || kw(v, "EXCEPT")) {
      text(b, " ");
      upper(b, t[i++]);
      while (i < n && (kw(t[i], "ALL") || kw(t[i], "DISTINCT") || kw(t[i], "BY") || kw(t[i], "NAME"))) {
        text(b, " ");
        upper(b, t[i++]);
      }
      text(b, " ");
      summarize(b, t + i, n - i, depth + 1);
      return;
    }
    if (kw(v, "FROM") || kw(v, "USING")) {
      if (kw(operation_token, "COPY"))
        break;
      text(b, " ");
      upper(b, v);
      text(b, " ");
      /* A relation may itself have a clause-keyword name (e.g. ClickHouse
       * system.settings). Consume that first name before finding clauses. */
      size_t start = i + 1;
      if (start < n && identifier(t[start])) {
        ++start;
        while (start + 1 < n && sym(t[start], '.') && identifier(t[start + 1]))
          start += 2;
      }
      size_t end = clause_end(t, n, start);
      relations(b, t + i + 1, end - i - 1, depth + 1);
      i = end;
      continue;
    }
    if (kw(v, "INTO")) {
      item(b, "INTO ");
      i = kw(t[0], "MERGE") ? relation(b, t, n, i + 1, depth + 1) : name(b, t, n, i + 1);
      if (i < n && sym(t[i], '('))
        i = close_group(t, n, i) + 1;
      continue;
    }
    if (kw(v, "SELECT") && kw(operation_token, "INSERT")) {
      text(b, " ");
      summarize(b, t + i, n - i, depth + 1);
      return;
    }
    if (kw(v, "LIMIT") || kw(v, "OFFSET")) {
      text(b, " ");
      upper(b, v);
      text(b, " ");
      size_t end = clause_end(t, n, i + 1), comma = i + 1;
      size_t bound_end = end, by = end;
      if (v.clickhouse)
        for (size_t j = i + 1; (sequel_work_tick(1), j < end); ++j)
          if (kw(t[j], "BY")) {
            by = j;
            bound_end = j;
            break;
          }
      if (bound_end > i + 1 && (kw(t[bound_end - 1], "ROWS") || kw(t[bound_end - 1], "ROW")))
        --bound_end;
      while (comma < bound_end && !sym(t[comma], ',')) {
        if (sym(t[comma], '('))
          comma = close_group(t, bound_end, comma);
        if (comma < bound_end)
          ++comma;
      }
      bound(b, t + i + 1, (comma < bound_end ? comma : bound_end) - i - 1);
      if (comma < bound_end) {
        text(b, ", ");
        bound(b, t + comma + 1, bound_end - comma - 1);
      }
      if (by < end)
        item(b, "BY …");
      nested(b, t + i + 1, bound_end - i - 1, depth + 1);
      i = end;
      continue;
    }
    if (kw(v, "FOR")) {
      item(b, "FOR");
      ++i;
      while (i < n) {
        bool matched = false;
        const char *locks[] = {"UPDATE", "NO", "KEY", "SHARE", "SKIP", "LOCKED", "NOWAIT"};
        for (size_t j = 0; (sequel_work_tick(1), j < sizeof(locks) / sizeof(*locks)); ++j)
          if (kw(t[i], locks[j])) {
            item(b, locks[j]);
            matched = true;
            break;
          }
        if (!matched && boundary(t[i]))
          break;
        ++i;
      }
      continue;
    }
    if (kw(v, "FETCH")) {
      size_t end = clause_end(t, n, i + 1), j = i + 1;
      item(b, "FETCH");
      if (j < end && (kw(t[j], "FIRST") || kw(t[j], "NEXT"))) {
        text(b, " ");
        upper(b, t[j++]);
      } else
        item(b, "FIRST");
      size_t row = j;
      while (row < end && !kw(t[row], "ROW") && !kw(t[row], "ROWS"))
        ++row;
      text(b, " ");
      if (row > j)
        bound(b, t + j, row - j);
      else
        text(b, "1");
      item(b, row + 2 < end && kw(t[row + 1], "WITH") && kw(t[row + 2], "TIES") ? "ROWS WITH TIES" : "ROWS ONLY");
      i = end;
      continue;
    }
    if (kw(v, "ON") && i + 1 < n && kw(t[i + 1], "CONFLICT")) {
      i += 2;
      if (i < n && sym(t[i], '('))
        i = close_group(t, n, i) + 1;
      while (i < n && !kw(t[i], "DO") && !kw(t[i], "RETURNING"))
        ++i;
      if (i + 1 < n && kw(t[i], "DO")) {
        item(b, "ON CONFLICT DO ");
        upper(b, t[i + 1]);
        i += 2;
      }
      continue;
    }
    if ((v.clickhouse && (kw(v, "PREWHERE") || kw(v, "SETTINGS") || kw(v, "SAMPLE") || kw(v, "FORMAT"))) ||
        kw(v, "WHERE") || kw(v, "HAVING") || kw(v, "QUALIFY") || kw(v, "SET") || kw(v, "RETURNING") || kw(v, "GROUP") ||
        kw(v, "ORDER") || kw(v, "WINDOW")) {
      text(b, " ");
      upper(b, v);
      ++i;
      if (i < n && kw(t[i], "BY")) {
        text(b, " BY");
        ++i;
      }
      size_t end = clause_end(t, n, i);
      item(b, "…");
      nested(b, t + i, end - i, depth + 1);
      i = end;
      continue;
    }
    if (kw(v, "VALUES")) {
      item(b, "VALUES …");
      ++i;
      continue;
    }
    if (sym(v, '(')) {
      size_t end = close_group(t, n, i);
      nested(b, t + i, end - i + (end < n), depth + 1);
      i = end < n ? end + 1 : n;
      continue;
    }
    ++i;
  }
}



typedef struct {
  char *p;
  size_t n, capacity;
} analysis_text;

typedef struct {
  sequel_map_segment_t *p;
  size_t n, capacity;
} analysis_map;

typedef struct {
  const char *key;
  size_t length, slot, identity;
  uint64_t hash;
  bool occupied, owned, numbered;
} analysis_binding;

typedef struct {
  analysis_binding *p;
  size_t count, capacity, maximum, identities;
} analysis_bindings;

typedef struct {
  format_token *p;
  size_t n, capacity;
} analysis_layout;

typedef struct {
  sequel_analysis_t *out;
  sequel_analysis_options_t options;
} analysis_state;

static const char *analysis_dialect_name(sequel_dialect_t d) {
  static const char *names[] = {"generic", "sqlite", "postgresql", "duckdb", "mysql", "clickhouse"};
  return d >= SEQUEL_GENERIC && d <= SEQUEL_CLICKHOUSE ? names[d] : "invalid";
}

static void analysis_charge(analysis_state *a, size_t bytes) {
  (void)a;
  sequel_output_tick(bytes);
}

static void *analysis_array(void *pointer, size_t count, size_t element) {
  if (count > SIZE_MAX / element)
    sequel_abort(SEQUEL_ERR_LIMIT);
  size_t bytes = count * element,
         capacity = pointer ? ((sequel_allocation *)pointer - 1)->h.bytes - sizeof(sequel_allocation) : 0;
  if (bytes <= capacity)
    return pointer;
  size_t next = capacity ? capacity : element * 8;
  while (next < bytes) {
    if (next > SIZE_MAX / 2) {
      next = bytes;
      break;
    }
    next *= 2;
  }
  return realloc(pointer, next);
}

static void analysis_layout_add(analysis_layout *layout, size_t start, size_t length, int kind, bool gap,
                                sequel_dialect_t dialect) {
  layout->p = analysis_array(layout->p, layout->n + 1, sizeof(*layout->p));
  format_token t = {0};
  t.t.n = length;
  t.t.word = kind == 1;
  t.t.clickhouse = dialect == SEQUEL_CLICKHOUSE;
  t.kind = kind;
  t.gap = gap;
  t.source_offset = start;
  t.flat_width = (layout->n ? layout->p[layout->n - 1].flat_width : 0) + length + 1;
  layout->p[layout->n++] = t;
}

static void analysis_append(analysis_text *b, const char *p, size_t n) {
  sequel_work_tick(n);
  if (n > SIZE_MAX - b->n - 1)
    sequel_abort(SEQUEL_ERR_LIMIT);
  size_t need = b->n + n + 1;
  if (need > b->capacity) {
    size_t next = b->capacity ? b->capacity : 32;
    while (next < need) {
      if (next > SIZE_MAX / 2) {
        next = need;
        break;
      }
      next *= 2;
    }
    b->p = realloc(b->p, next);
    b->capacity = next;
  }
  memcpy(b->p + b->n, p, n);
  b->n += n;
  b->p[b->n] = 0;
}

static void analysis_segment(analysis_map *b, size_t fs, size_t fe, size_t ts, size_t te, sequel_map_kind_t kind) {
  if (fs == fe && ts == te)
    return;
  if (b->n) {
    sequel_map_segment_t *last = &b->p[b->n - 1];
    if (last->kind == kind && last->from_end == fs && last->to_end == ts && kind != SEQUEL_MAP_REPLACE) {
      last->from_end = fe;
      last->to_end = te;
      return;
    }
  }
  if (b->n == b->capacity) {
    size_t next = b->capacity ? b->capacity * 2 : 16;
    if (next < b->capacity || next > SIZE_MAX / sizeof(*b->p))
      sequel_abort(SEQUEL_ERR_LIMIT);
    b->p = realloc(b->p, next * sizeof(*b->p));
    b->capacity = next;
  }
  sequel_output_tick(sizeof(*b->p));
  b->p[b->n++] = (sequel_map_segment_t){fs, fe, ts, te, kind};
}

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

/* Return identity key and explicit numbered arity. Marker prefixes distinguish
 * SQLite binding namespaces; fingerprint frames exclude those spellings. */
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

static uint64_t analysis_key_hash(const char *p, size_t n) {
  sequel_sha256 s;
  sha_init(&s);
  sha_update(&s, p, n);
  char hex[65];
  sha_final(&s, hex);
  uint64_t v = 0;
  for (size_t i = 0; i < 16; i++) {
    sequel_work_tick(1);
    v = (v << 4) | (uint64_t)(hex[i] <= '9' ? hex[i] - '0' : hex[i] - 'a' + 10);
  }
  return v;
}

static analysis_binding *analysis_binding_slot(analysis_bindings *b, const char *key, size_t length, size_t number) {
  if (!b->capacity || b->count >= b->capacity / 2) {
    size_t next = b->capacity ? b->capacity * 2 : 16;
    if (next < b->capacity || next > SIZE_MAX / sizeof(*b->p))
      sequel_abort(SEQUEL_ERR_LIMIT);
    analysis_binding *grown = calloc(next, sizeof(*grown));
    for (size_t i = 0; i < b->capacity; i++) {
      sequel_work_tick(1);
      if (b->p[i].occupied) {
        size_t at = (size_t)b->p[i].hash & (next - 1);
        while (grown[at].occupied)
          at = (at + 1) & (next - 1);
        grown[at] = b->p[i];
      }
    }
    free(b->p);
    b->p = grown;
    b->capacity = next;
  }
  uint64_t hash = length ? analysis_key_hash(key, length) : (uint64_t)(b->identities + 1);
  size_t at = (size_t)hash & (b->capacity - 1);
  while (b->p[at].occupied) {
    analysis_binding *v = &b->p[at];
    if (length && v->hash == hash && v->length == length && v->numbered == (number != 0)) {
      sequel_work_tick(length);
      if (!memcmp(v->key, key, length))
        return v;
    }
    at = (at + 1) & (b->capacity - 1);
  }
  if (b->maximum == SIZE_MAX || b->identities == SIZE_MAX)
    sequel_abort(SEQUEL_ERR_LIMIT);
  size_t slot = number ? number : b->maximum + 1;
  if (slot > b->maximum)
    b->maximum = slot;
  b->p[at] = (analysis_binding){key, length, slot, ++b->identities, hash, true, false, number != 0};
  ++b->count;
  return &b->p[at];
}

static analysis_binding *analysis_numeric_identity(analysis_bindings *b, size_t slot) {
  char encoded[32];
  snprintf(encoded, sizeof(encoded), "%zu", slot);
  size_t n = strlen(encoded);
  analysis_binding *v = analysis_binding_slot(b, encoded, n, slot);
  if (v->key == encoded) {
    char *owned = malloc(n);
    memcpy(owned, encoded, n);
    v->key = owned;
    v->owned = true;
  }
  return v;
}

static void analysis_dictionary_clear(analysis_bindings *b) {
  /* Numeric keys are tiny owned copies; named keys point into caller input. */
  for (size_t i = 0; i < b->capacity; i++) {
    sequel_work_tick(1);
    if (b->p[i].occupied && b->p[i].owned)
      free((void *)b->p[i].key);
  }
  free(b->p);
  *b = (analysis_bindings){0};
}

static void analysis_add_diagnostic(sequel_analysis_t *out, sequel_diagnostic_code_t code, size_t start, size_t end) {
  size_t count = out->diagnostic_count;
  sequel_output_tick(sizeof(sequel_diagnostic_t));
  sequel_diagnostic_t *ds = analysis_array((void *)out->diagnostics, count + 1, sizeof(*ds));
  diagnostic_report(NULL, 0, &ds[count], code, 0, 0);
  ds[count].offset = start;
  ds[count].end_offset = end;
  out->diagnostics = ds;
  out->diagnostic_count++;
}

static bool analysis_has_diagnostic(const sequel_analysis_t *out, sequel_diagnostic_code_t code, size_t start,
                                    size_t end) {
  for (size_t i = 0; i < out->diagnostic_count; ++i)
    if (out->diagnostics[i].code == code && out->diagnostics[i].offset == start &&
        out->diagnostics[i].end_offset == end)
      return true;
  return false;
}

static void analysis_sort_diagnostics(sequel_analysis_t *out) {
  sequel_diagnostic_t *diagnostics = (sequel_diagnostic_t *)out->diagnostics;
  for (size_t i = 1; i < out->diagnostic_count; ++i) {
    sequel_diagnostic_t value = diagnostics[i];
    size_t j = i;
    while (j && diagnostics[j - 1].offset > value.offset) {
      diagnostics[j] = diagnostics[j - 1];
      --j;
    }
    diagnostics[j] = value;
  }
}

static void analysis_add_statement(sequel_analysis_t *out, size_t start, size_t end) {
  size_t count = out->statement_count;
  sequel_output_tick(sizeof(sequel_range_t));
  sequel_range_t *rs = analysis_array((void *)out->statements, count + 1, sizeof(*rs));
  rs[count] = (sequel_range_t){start, end};
  out->statements = rs;
  out->statement_count++;
}

typedef struct {
  size_t offset, index;
  bool ending;
} analysis_position_event;

static int analysis_event_compare(const void *a, const void *b) {
  sequel_work_tick(1);
  const analysis_position_event *x = a, *y = b;
  return x->offset < y->offset ? -1 : x->offset > y->offset ? 1 : 0;
}

static void analysis_positions(const char *sql, sequel_analysis_t *out) {
  size_t n = out->diagnostic_count;
  if (!n)
    return;
  analysis_position_event *events = malloc(n * 2 * sizeof(*events));
  for (size_t i = 0; i < n; i++) {
    sequel_work_tick(1);
    events[i * 2] = (analysis_position_event){out->diagnostics[i].offset, i, false};
    events[i * 2 + 1] = (analysis_position_event){out->diagnostics[i].end_offset, i, true};
  }
  qsort(events, n * 2, sizeof(*events), analysis_event_compare);
  size_t position = 0, line = 1, column = 1;
  bool cr = false;
  for (size_t i = 0; i < n * 2; i++) {
    sequel_work_tick(1);
    while (position < events[i].offset) {
      unsigned char c = (unsigned char)sql[position++];
      if (c == '\r') {
        line++;
        column = 1;
        cr = true;
      } else if (c == '\n') {
        if (!cr)
          line++;
        column = 1;
        cr = false;
      } else {
        if ((c & 0xc0) != 0x80)
          column++;
        cr = false;
      }
    }
    sequel_diagnostic_t *d = (sequel_diagnostic_t *)out->diagnostics + events[i].index;
    if (events[i].ending) {
      d->end_line = line;
      d->end_column = column;
    } else {
      d->line = line;
      d->column = column;
    }
  }
  free(events);
}

static void analysis_structure(const char *sql, size_t n, format_token *ts, size_t count, sequel_analysis_t *out) {
  size_t stack[64], depth = 0, start = SIZE_MAX, last = 0;
  for (size_t i = 0; i < count; i++) {
    sequel_work_tick(1);
    format_token t = ts[i];
    if (t.kind >= 3)
      continue;
    size_t off = t.source_offset;
    if (start == SIZE_MAX)
      start = off;
    last = off + t.t.n;
    if (t.kind != 0)
      continue;
    if (sym(t.t, '(') || sym(t.t, '[')) {
      if (depth == 64) {
        analysis_add_diagnostic(out, SEQUEL_DIAG_NESTING_LIMIT, off, off + 1);
        depth = 0;
      } else
        stack[depth++] = i;
    } else if (sym(t.t, ')') || sym(t.t, ']')) {
      if (!depth || (sym(t.t, ')') != sym(ts[stack[depth - 1]].t, '(')))
        analysis_add_diagnostic(out, SEQUEL_DIAG_UNMATCHED_CLOSE, off, off + 1);
      else
        depth--;
    } else if (sym(t.t, ';')) {
      if (depth)
        analysis_add_diagnostic(out, SEQUEL_DIAG_UNCLOSED_GROUP, ts[stack[depth - 1]].source_offset, off);
      depth = 0;
      /* Empty semicolon statements are not counted. */
      if (start != off)
        analysis_add_statement(out, start, off + 1);
      start = SIZE_MAX;
    }
  }
  if (depth)
    analysis_add_diagnostic(out, SEQUEL_DIAG_UNCLOSED_GROUP, ts[stack[depth - 1]].source_offset, n);
  if (start != SIZE_MAX)
    analysis_add_statement(out, start, last);
}

static void analysis_structure_from_lexed(const sequel_lexed_document_t *lexed, sequel_analysis_t *out) {
  size_t count = 0, start = SIZE_MAX, last = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &count);
  sequel_source_view_t source = sequel_lexed_source(lexed);
  for (size_t i = 0; i < count; ++i) {
    const sequel_token_t *token = &tokens[i];
    if (token->kind == SEQUEL_TOKEN_EOF)
      break;
    if (token->kind == SEQUEL_TOKEN_WHITESPACE || token->kind == SEQUEL_TOKEN_LINE_COMMENT ||
        token->kind == SEQUEL_TOKEN_BLOCK_COMMENT)
      continue;
    if (start == SIZE_MAX)
      start = token->range.start;
    last = token->range.end;
    if (token->kind == SEQUEL_TOKEN_PUNCTUATION && token->range.end == token->range.start + 1 &&
        source.data[token->range.start] == ';') {
      analysis_add_statement(out, start, last);
      start = SIZE_MAX;
    }
  }
  if (start != SIZE_MAX)
    analysis_add_statement(out, start, last);
}

static void analysis_compact_list_text(analysis_text *text) {
  size_t read = 0, write = 0;
  while (read < text->n) {
    char c = text->p[read++];
    if (c == '(' || c == ',') {
      text->p[write++] = c;
      while (read < text->n && text->p[read] == ' ')
        ++read;
      if (c == '(' && read + 1 < text->n && text->p[read] == '?' && text->p[read + 1] == '?')
        ++read;
      continue;
    }
    if (c == ' ' && read < text->n && (text->p[read] == ',' || text->p[read] == ')'))
      continue;
    text->p[write++] = c;
  }
  text->n = write;
  text->p[write] = 0;
}

static bool analysis_word(format_token t, const char *word) {
  size_t n = strlen(word);
  if (t.kind != 1 || t.t.n != n)
    return false;
  for (size_t i = 0; i < n; ++i)
    if (toupper((unsigned char)t.t.p[i]) != toupper((unsigned char)word[i]))
      return false;
  return true;
}

static void analysis_lints(const char *sql, size_t n, format_token *ts, size_t count, sequel_dialect_t dialect,
                           sequel_analysis_t *out) {
  (void)sql;
  if (out->statement_count > 1)
    analysis_add_diagnostic(out, SEQUEL_DIAG_MULTIPLE_STATEMENTS, out->statements[1].offset,
                            out->statements[1].end_offset);
  size_t depth = 0, joins = 0, in_items = 0, in_start = SIZE_MAX;
  bool write = false, where = false, table = false, operation = false, star = false;
  for (size_t i = 0; i < count; ++i) {
    format_token t = ts[i];
    if (t.kind >= 3)
      continue;
    if (sym(t.t, '(')) {
      if (++depth > 16)
        analysis_add_diagnostic(out, SEQUEL_DIAG_DEEP_QUERY_NESTING, t.source_offset, t.source_offset + 1);
      if (in_start != SIZE_MAX)
        ++in_items;
    } else if (sym(t.t, ')')) {
      if (in_start != SIZE_MAX && depth == in_start) {
        if (in_items > 32)
          analysis_add_diagnostic(out, SEQUEL_DIAG_LARGE_IN_LIST, t.source_offset, t.source_offset + 1);
        in_start = SIZE_MAX;
        in_items = 0;
      }
      if (depth)
        --depth;
    }
    if (sym(t.t, ';') && i + 1 < count && sym(ts[i + 1].t, ';'))
      analysis_add_diagnostic(out, SEQUEL_DIAG_EMPTY_STATEMENT, t.source_offset, t.source_offset + 1);
    if (t.kind == 1) {
      if (!operation) {
        operation = true;
        if (analysis_word(t, "UPDATE") || analysis_word(t, "DELETE"))
          write = true;
        if (analysis_word(t, "SELECT") || analysis_word(t, "INSERT") || analysis_word(t, "UPDATE") ||
            analysis_word(t, "DELETE") || analysis_word(t, "MERGE") || analysis_word(t, "WITH")) {
          /* recognized */
        } else {
          analysis_add_diagnostic(out, SEQUEL_DIAG_UNKNOWN_OPERATION, t.source_offset, t.source_offset + t.t.n);
        }
      }
      if (analysis_word(t, "FROM") || analysis_word(t, "INTO") || analysis_word(t, "UPDATE") ||
          analysis_word(t, "USING"))
        table = true;
      if (analysis_word(t, "WHERE"))
        where = true;
      if (analysis_word(t, "JOIN"))
        ++joins;
      if (analysis_word(t, "IN"))
        in_start = depth;
      if (analysis_word(t, "ILIKE") && dialect != SEQUEL_POSTGRESQL && dialect != SEQUEL_DUCKDB)
        analysis_add_diagnostic(out, SEQUEL_DIAG_DIALECT_PORTABILITY, t.source_offset, t.source_offset + t.t.n);
    }
    if (sym(t.t, '*') && i > 0 && analysis_word(ts[i - 1], "SELECT"))
      star = true;
  }
  if (!table)
    analysis_add_diagnostic(out, SEQUEL_DIAG_NO_REFERENCED_TABLE, 0, n);
  if (write && !where)
    analysis_add_diagnostic(out, SEQUEL_DIAG_UNBOUNDED_WRITE, 0, n);
  if (star)
    analysis_add_diagnostic(out, SEQUEL_DIAG_SELECT_STAR, 0, n);
  if (joins > 8)
    analysis_add_diagnostic(out, SEQUEL_DIAG_EXCESSIVE_JOINS, 0, n);
}

/* Whitespace is delayed across comments so deleted comment text never appears
 * in output. Every consumed source byte still has explicit map provenance. */
static void analysis_sanitize(const char *sql, size_t n, format_token *ts, size_t count, analysis_state *a,
                              analysis_text *safe, analysis_map *map, analysis_layout *layout, sequel_sha256 *hash) {
  sequel_analysis_t *out = a->out;
  analysis_bindings dictionary = {0}, slot_dictionary = {0};
  size_t consumed = 0, statement = 0, bind_count = 0;
  bool gap = false;
  size_t collapse_end = SIZE_MAX, fingerprint_next = 0;
  for (size_t i = 0; i < count; i++) {
    sequel_work_tick(1);
    format_token t = ts[i];
    size_t start = t.source_offset, end = start + t.t.n;
    if (collapse_end != SIZE_MAX && i == collapse_end) {
      consumed = start;
      collapse_end = SIZE_MAX;
      gap = false;
    }
    bool suppressed = collapse_end != SIZE_MAX && i < collapse_end;
    if (!suppressed && start > consumed) {
      if (map)
        analysis_segment(map, consumed, start, safe->n, safe->n, SEQUEL_MAP_DELETE);
      gap = true;
    }
    consumed = end;
    if (t.kind >= 3) {
      if (map && !suppressed)
        analysis_segment(map, start, end, safe->n, safe->n, SEQUEL_MAP_DELETE);
      if (!suppressed)
        gap = true;
      continue;
    }
    bool literal = analysis_literal(t, a->options.dialect);
    const char *key;
    size_t key_length, number;
    sequel_bind_style_t style;
    while (statement + 1 < out->statement_count && start >= out->statements[statement].end_offset)
      statement++;
    bool bind = !literal && analysis_bind_token(t, a->options.dialect, &key, &key_length, &number, &style);
    if (bind) {
      char numeric[32];
      if (number) {
        snprintf(numeric, sizeof(numeric), "%zu", number);
        key = numeric;
        key_length = strlen(numeric);
      }
      analysis_binding *identity = analysis_binding_slot(&dictionary, key, key_length, number);
      if (number && identity->key == numeric) {
        char *owned = malloc(key_length);
        memcpy(owned, numeric, key_length);
        identity->key = owned;
        identity->owned = true;
      }
      size_t old_count = slot_dictionary.count;
      analysis_binding *canonical = analysis_numeric_identity(&slot_dictionary, identity->slot);
      if (slot_dictionary.count != old_count)
        canonical->identity = ++fingerprint_next;
      unsigned char kind = 'B';
      sha_update(hash, &kind, 1);
      sha_u64(hash, canonical->identity);
      if (style == SEQUEL_BIND_TYPED) {
        sequel_sha256 type_hash;
        sha_init(&type_hash);
        const char *colon = memchr(t.t.p, ':', t.t.n);
        size_t type_length = (size_t)((t.t.p + t.t.n - 1) - (colon + 1));
        sequel_parse_options_t type_options;
        sequel_parse_options_init(&type_options);
        type_options.dialect = a->options.dialect;
        type_options.retain_comments = false;
        type_options.retain_whitespace = false;
        sequel_source_view_t type_source = {colon + 1, type_length};
        sequel_lexed_document_t *type_document = NULL;
        if (sequel_lex(&type_source, &type_options, NULL, &type_document) != SEQUEL_OK)
          sequel_abort(SEQUEL_ERR_INVALID_ARGUMENT);
        size_t type_count = 0;
        const sequel_token_t *type_tokens = sequel_lexed_tokens(type_document, &type_count);
        sequel_source_view_t type_view = sequel_lexed_source(type_document);
        for (size_t k = 0; k < type_count; k++) {
          sequel_work_tick(1);
          if (type_tokens[k].kind == SEQUEL_TOKEN_EOF)
            continue;
          const sequel_token_t *type_token = &type_tokens[k];
          bool value = (type_token->flags & SEQUEL_TOKEN_FLAG_LITERAL) != 0;
          char frame = value                                                                                     ? 'L'
                       : type_token->kind == SEQUEL_TOKEN_KEYWORD || type_token->kind == SEQUEL_TOKEN_IDENTIFIER ? 'W'
                                                                                                                 : 'T';
          sha_frame(&type_hash, frame, type_view.data + type_token->range.start,
                    type_token->range.end - type_token->range.start, false);
        }
        char type_digest[65];
        sha_final(&type_hash, type_digest);
        sha_frame(hash, 'Y', type_digest, 64, false);
        sequel_lexed_document_free(type_document);
      } else
        sha_frame(hash, 'Y', "", 0, false);
      if (!a->options.omit_bind_descriptors) {
        sequel_output_tick(sizeof(sequel_bind_descriptor_t));
        sequel_bind_descriptor_t *bs = analysis_array((void *)out->binds, out->bind_count + 1, sizeof(*bs));
        bs[out->bind_count] = (sequel_bind_descriptor_t){++bind_count, identity->slot, statement, start, end, style};
        out->binds = bs;
        out->bind_count++;
      }
    } else if (literal) {
      unsigned char kind = 'B';
      sha_update(hash, &kind, 1);
      sha_u64(hash, ++fingerprint_next);
      sha_frame(hash, 'Y', "", 0, false);
    } else
      sha_frame(hash,
                t.kind == 1   ? 'W'
                : t.kind == 2 ? 'Q'
                              : 'T',
                t.t.p, t.t.n,
                t.kind == 1 && ((a->options.dialect != SEQUEL_MYSQL && a->options.dialect != SEQUEL_CLICKHOUSE) ||
                                isupper((unsigned char)t.t.p[0])));
    if (t.kind == 0 && sym(t.t, ';')) {
      fingerprint_next = 0;
      analysis_dictionary_clear(&dictionary);
      analysis_dictionary_clear(&slot_dictionary);
    }
    while (statement + 1 < out->statement_count && start >= out->statements[statement].end_offset)
      statement++;
    if (suppressed) {
      gap = false;
      continue;
    }

    if (!a->options.keep_in_lists && t.kind == 1 && kw(t.t, "IN")) {
      size_t open = i + 1;
      while (open < count && ts[open].kind >= 3)
        open++;
      if (open < count && sym(ts[open].t, '(')) {
        size_t k = open + 1, items = 0;
        bool expecting = true, valid = true;
        for (; k < count; k++) {
          sequel_work_tick(1);
          if (ts[k].kind >= 3)
            continue;
          if (sym(ts[k].t, ')'))
            break;
          if (expecting) {
            const char *bk;
            size_t bl, bn;
            sequel_bind_style_t st;
            bool value = analysis_literal(ts[k], a->options.dialect) ||
                         analysis_bind_token(ts[k], a->options.dialect, &bk, &bl, &bn, &st);
            if (!value) {
              valid = false;
              break;
            }
            items++;
            expecting = false;
          } else if (sym(ts[k].t, ','))
            expecting = true;
          else {
            valid = false;
            break;
          }
        }
        /* Mark collapse when the opening token is visited. */
        if (valid && !expecting && items > 1 && k < count)
          ts[open].matching = k;
      }
    }
    size_t emitted_before = safe->n;
    if (gap && safe->n) {
      size_t at = safe->n;
      analysis_append(safe, " ", 1);
      if (map)
        analysis_segment(map, start, start, at, safe->n, SEQUEL_MAP_INSERT);
    }
    bool token_gap = gap;
    gap = false;
    size_t before = safe->n;
    if (t.kind == 0 && sym(t.t, '(') && t.matching > i) {
      analysis_append(safe, "(?", 2);
      if (map) {
        analysis_segment(map, start, end, before, before + 1, SEQUEL_MAP_COPY);
        size_t close = ts[t.matching].source_offset;
        analysis_segment(map, end, close, before + 1, before + 2, SEQUEL_MAP_REPLACE);
      }
      analysis_layout_add(layout, before, 1, 0, token_gap, a->options.dialect);
      analysis_layout_add(layout, before + 1, 1, 0, false, a->options.dialect);
      collapse_end = t.matching;
    } else {
      analysis_append(safe, literal || bind ? "?" : t.t.p, literal || bind ? 1 : t.t.n);
      if (map)
        analysis_segment(map, start, end, before, safe->n, literal || bind ? SEQUEL_MAP_REPLACE : SEQUEL_MAP_COPY);
      analysis_layout_add(layout, before, safe->n - before, literal || bind ? 0 : t.kind, token_gap,
                          a->options.dialect);
    }
    if (!a->options.omit_query_text) {
      size_t cap = a->options.max_query_text_bytes ? a->options.max_query_text_bytes : SEQUEL_TEXT_MAX;
      size_t lo = emitted_before < cap ? emitted_before : cap, hi = safe->n < cap ? safe->n : cap;
      analysis_charge(a, hi - lo);
    }
  }
  analysis_dictionary_clear(&dictionary);
  analysis_dictionary_clear(&slot_dictionary);
  if (consumed < n && map)
    analysis_segment(map, consumed, n, safe->n, safe->n, SEQUEL_MAP_DELETE);
  if (!safe->p)
    analysis_append(safe, "", 0);
}

static void analysis_clip_map(analysis_map *map, size_t limit) {
  sequel_current_call->output_bytes -= map->n * sizeof(*map->p);
  analysis_map clipped = {0};
  for (size_t i = 0; i < map->n; i++) {
    sequel_work_tick(1);
    sequel_map_segment_t s = map->p[i];
    size_t ts = s.to_start < limit ? s.to_start : limit, te = s.to_end < limit ? s.to_end : limit;
    if (s.kind == SEQUEL_MAP_COPY && te - ts < s.from_end - s.from_start) {
      size_t middle = s.from_start + (te - ts);
      analysis_segment(&clipped, s.from_start, middle, ts, te, SEQUEL_MAP_COPY);
      analysis_segment(&clipped, middle, s.from_end, te, te, SEQUEL_MAP_DELETE);
    } else
      analysis_segment(&clipped, s.from_start, s.from_end, ts, te, ts == te ? SEQUEL_MAP_DELETE : s.kind);
  }
  free(map->p);
  *map = clipped;
}

static void analysis_format_map(analysis_layout *layout, size_t fn, size_t tn, analysis_map *map) {
  size_t fp = 0, tp = 0;
  for (size_t i = 0; i < layout->n; i++) {
    sequel_work_tick(1);
    format_token t = layout->p[i];
    size_t f = t.source_offset, to = t.rendered_offset;
    if (f > fp || to > tp)
      analysis_segment(map, fp, f, tp, to,
                       f == fp    ? SEQUEL_MAP_INSERT
                       : to == tp ? SEQUEL_MAP_DELETE
                                  : SEQUEL_MAP_REPLACE);
    analysis_segment(map, f, f + t.t.n, to, to + t.t.n, SEQUEL_MAP_COPY);
    fp = f + t.t.n;
    tp = to + t.t.n;
  }
  if (fp < fn || tp < tn)
    analysis_segment(map, fp, fn, tp, tn,
                     fp == fn   ? SEQUEL_MAP_INSERT
                     : tp == tn ? SEQUEL_MAP_DELETE
                                : SEQUEL_MAP_REPLACE);
}

static void analysis_summary(analysis_layout *layout, analysis_text *safe, format_token *scanned, size_t scanned_count,
                             sequel_dialect_t dialect, sequel_analysis_t *out) {
  /* Keep the display view and structural view separate, both derived from the
   * original lexical scan. Structural bounds retain numeric tokens, while
   * values and binds become placeholders. Token spacing must survive this view. */
  for (size_t i = 0; i < layout->n; i++) {
    sequel_work_tick(1);
    layout->p[i].t.p = safe->p + layout->p[i].source_offset;
  }
  token *tokens = NULL;
  size_t count = 0;
  bool comment_gap = false;
  for (size_t i = 0; i < scanned_count; i++) {
    sequel_work_tick(1);
    format_token f = scanned[i];
    if (f.kind >= 3) {
      comment_gap = true;
      continue;
    }
    token t = f.t;
    t.space = f.gap || comment_gap;
    comment_gap = false;
    unsigned char c = (unsigned char)*t.p;
    bool numeric = isdigit(c) || (c == '.' && t.n > 1 && isdigit((unsigned char)t.p[1]));
    const char *key;
    size_t key_length, number;
    sequel_bind_style_t style;
    if ((!numeric && analysis_literal(f, dialect)) ||
        analysis_bind_token(f, dialect, &key, &key_length, &number, &style)) {
      t.p = "?";
      t.n = 1;
      t.word = false;
    }
    /* The structural parser historically sees punctuation/operator bytes as
     * separate tokens. Derive that view directly, without scanning SQL again. */
    if (numeric && c == '.' && t.n > 1) {
      tokens = analysis_array(tokens, count + 2, sizeof(*tokens));
      tokens[count++] = (token){t.p, 1, false, t.space, t.clickhouse};
      tokens[count++] = (token){t.p + 1, t.n - 1, false, false, t.clickhouse};
    } else if (f.kind == 0 && !numeric && t.n > 1) {
      for (size_t k = 0; k < t.n; k++) {
        sequel_work_tick(1);
        tokens = analysis_array(tokens, count + 1, sizeof(*tokens));
        tokens[count++] = (token){t.p + k, 1, false, k == 0 && t.space, t.clickhouse};
      }
    } else {
      tokens = analysis_array(tokens, count + 1, sizeof(*tokens));
      tokens[count++] = t;
    }
  }
  size_t capacity = 64;
  char *buffer = malloc(capacity);
  bool mixed = false;
  output b;
  size_t before = sequel_current_call->output_bytes;
retry:;
  sequel_current_call->output_bytes = before;
  b = (output){buffer, 0, capacity, false, true};
  buffer[0] = 0;
  out->operation[0] = 0;
  size_t statements = 0;
  for (size_t start = 0; start < count;) {
    sequel_work_tick(1);
    size_t end = start;
    while (end < count && !sym(tokens[end], ';'))
      end++;
    if (end > start) {
      size_t length = end - start;
      token *statement = tokens + start;
      size_t op = 0;
      while (op < length && sym(statement[op], '('))
        op++;
      if (kw(statement[0], "WITH"))
        for (op = 1; op < length; op++) {
          sequel_work_tick(1);
          if (sym(statement[op], '(')) {
            op = close_group(statement, length, op);
            continue;
          }
          if (query_start(statement[op]) && !kw(statement[op], "WITH"))
            break;
        }
      char operation[32] = {0};
      if (op < length && statement[op].word) {
        output p = {operation, 0, sizeof(operation)};
        upper(&p, statement[op]);
      }
      if (!statements)
        memcpy(out->operation, operation, sizeof(operation));
      else if (strcmp(out->operation, operation))
        mixed = true;
      if (mixed)
        out->operation[0] = 0;
      if (b.n)
        text(&b, "; ");
      summarize(&b, statement, length, 0);
      statements++;
    }
    start = end < count ? end + 1 : count;
  }
  if (b.overflow) {
    if (capacity > SEQUEL_SUMMARY_MAX / 2)
      sequel_abort(SEQUEL_ERR_LIMIT);
    capacity *= 2;
    buffer = realloc(buffer, capacity);
    goto retry;
  }
  if (!b.n) {
    sequel_output_tick(3);
    strcpy(buffer, "SQL");
  }
  sequel_output_tick(1);
  out->summary = buffer;
  free(tokens);
}

static void analysis_summary_from_query(const sequel_query_document_t *query, sequel_analysis_t *out) {
  sequel_summary_options_t options;
  sequel_summary_options_init(&options);
  options.include_bind_count = false;
  sequel_text_result_t summary = {0};
  if (sequel_summarize_query(query, &options, &summary) != SEQUEL_OK)
    sequel_abort(SEQUEL_ERR_NOMEM);
  size_t length = summary.length;
  char *copy = analysis_array(NULL, length + 1, sizeof(*copy));
  memcpy(copy, summary.data, length + 1);
  out->summary = copy;
  sequel_text_result_free(&summary);
}

static void analysis_empty(sequel_analysis_t *out, size_t n, sequel_dialect_t dialect) {
  *out = (sequel_analysis_t){0};
  out->schema_version = SEQUEL_ANALYSIS_VERSION;
  out->dialect = dialect;
  out->original_length = n;
  out->status = "error";
  out->omission_reason = "analysis_error";
  out->text = "";
  out->summary = "SQL";
  out->formatted = "";
}

void sequel_analysis_free(sequel_analysis_t *out) {
  if (!out)
    return;
  sequel_release_allocations(out->_storage);
  analysis_empty(out, 0, SEQUEL_GENERIC);
}

static sequel_status_t analysis_fail(sequel_analysis_t *out, size_t n, sequel_dialect_t d, sequel_status_t status,
                                     sequel_diagnostic_code_t code) {
  analysis_empty(out, n, d);
  out->status = status == SEQUEL_ERR_CANCELLED                                ? "cancelled"
                : status == SEQUEL_ERR_LIMIT || status == SEQUEL_ERR_OVERFLOW ? "limit"
                : status == SEQUEL_ERR_INVALID_ARGUMENT                       ? "malformed"
                                                                              : "error";
  out->omission_reason = status == SEQUEL_ERR_CANCELLED      ? "cancelled"
                         : !strcmp(out->status, "limit")     ? "analysis_limit"
                         : !strcmp(out->status, "malformed") ? "malformed"
                                                             : "analysis_error";
  diagnostic_report(NULL, 0, &out->_failure_diagnostic, code, 0, 0);
  out->diagnostics = &out->_failure_diagnostic;
  out->diagnostic_count = 1;
  return status;
}

sequel_status_t sequel_analyze_n(const char *sql, size_t n, const sequel_analysis_options_t *options,
                                 sequel_analysis_t *out) {
  if (!out)
    return SEQUEL_ERR_NULL;
  sequel_analysis_options_t o = options ? *options : (sequel_analysis_options_t){0};
  analysis_empty(out, n, o.dialect);
  if (!sql)
    return analysis_fail(out, n, o.dialect, SEQUEL_ERR_NULL, SEQUEL_DIAG_NULL_INPUT);
  if (o.dialect < SEQUEL_GENERIC || o.dialect > SEQUEL_CLICKHOUSE || o.max_input_bytes > SEQUEL_INPUT_MAX ||
      o.max_output_bytes > SEQUEL_TEXT_MAX || o.max_query_text_bytes > SEQUEL_TEXT_MAX || sequel_current_call)
    return analysis_fail(out, n, o.dialect, SEQUEL_ERR_INVALID_ARGUMENT, SEQUEL_DIAG_INVALID_ARGUMENT);
  size_t input_limit = o.max_input_bytes ? o.max_input_bytes : SEQUEL_INPUT_MAX;
  if (n > input_limit)
    return analysis_fail(out, n, o.dialect, SEQUEL_ERR_LIMIT, SEQUEL_DIAG_INPUT_LIMIT);
  size_t memory_limit = o.max_memory_bytes ? o.max_memory_bytes : SEQUEL_INPUT_MAX;
  if (memory_limit < sizeof(sequel_call))
    return analysis_fail(out, n, o.dialect, SEQUEL_ERR_LIMIT, SEQUEL_DIAG_RESOURCE_LIMIT);
  sequel_call *call = sequel_call_new();
  if (!call)
    return analysis_fail(out, n, o.dialect, SEQUEL_ERR_NOMEM, SEQUEL_DIAG_OUT_OF_MEMORY);
  call->source = sql;
  call->output_limit = o.max_output_bytes ? o.max_output_bytes : SEQUEL_TEXT_MAX;
  call->memory_limit = o.max_memory_bytes ? o.max_memory_bytes : SEQUEL_INPUT_MAX;
  call->work_limit = o.max_work_units;
  call->cancelled = o.cancelled;
  call->userdata = o.cancel_userdata;
  sequel_current_call = call;
  if (sequel_checkpoint(call)) {
    sequel_status_t failure = call->failure;
    size_t peak = call->peak, work = call->work;
    sequel_current_call = NULL;
    sequel_release_allocations(call->allocations);
    sequel_call_delete(call);
    sequel_status_t result = analysis_fail(out, n, o.dialect, failure,
                                           failure == SEQUEL_ERR_CANCELLED ? SEQUEL_DIAG_CANCELLED
                                           : failure == SEQUEL_ERR_LIMIT   ? SEQUEL_DIAG_RESOURCE_LIMIT
                                           : failure == SEQUEL_ERR_NOMEM   ? SEQUEL_DIAG_OUT_OF_MEMORY
                                                                           : SEQUEL_DIAG_INVALID_PARAMETER);
    out->memory_peak_bytes = peak;
    out->work_units = work;
    return result;
  }
  if (sizeof(*call) > call->memory_limit)
    sequel_abort(SEQUEL_ERR_LIMIT);
  call->live = call->peak = sizeof(*call);
  sequel_work_tick(1);
  analysis_state a = {out, o};
  sequel_diagnostic_t first;
  sequel_status_t valid = diagnostic_input(sql, n, o.dialect, &first);
  if (valid != SEQUEL_OK) {
    analysis_add_diagnostic(out, first.code, first.offset, first.end_offset);
    analysis_positions(sql, out);
    out->status = "malformed";
    out->omission_reason = "malformed";
  } else {
    const char *ambiguous = NULL;
    if ((o.dialect == SEQUEL_MYSQL && (ambiguous = strstr(sql, "$$"))) ||
        (o.dialect == SEQUEL_POSTGRESQL && (ambiguous = strstr(sql, "\\'")))) {
      call->ambiguous_literal = true;
      call->ambiguous_offset = (size_t)(ambiguous - sql);
    }
    sequel_parse_options_t parse_options;
    sequel_parse_options_init(&parse_options);
    parse_options.dialect = o.dialect;
    parse_options.retain_comments = true;
    sequel_source_view_t parse_source = {sql, n};
    sequel_syntax_document_t *syntax = NULL;
    sequel_query_document_t *query = NULL;
    size_t parse_issue_count = 0;
    sequel_status_t parse_status = sequel_parse_sql(&parse_source, &parse_options, NULL, &syntax);
    if (parse_status == SEQUEL_OK) {
      sequel_syntax_issues(syntax, &parse_issue_count);
      if (!parse_issue_count && sequel_build_query_document(syntax, &parse_options, &query) != SEQUEL_OK) {
        valid = SEQUEL_ERR_NOMEM;
        analysis_add_diagnostic(out, SEQUEL_DIAG_OUT_OF_MEMORY, 0, 0);
      }
    } else {
      valid = parse_status;
      analysis_add_diagnostic(
          out, parse_status == SEQUEL_ERR_LIMIT ? SEQUEL_DIAG_RESOURCE_LIMIT : SEQUEL_DIAG_INVALID_ARGUMENT, 0,
          n ? 1 : 0);
    }
    format_token *tokens = NULL;
    size_t scanned = 0;
    size_t count = SIZE_MAX;
    sequel_status_t adapter_status = SEQUEL_ERR_INVALID_ARGUMENT;
    if (syntax && !parse_issue_count)
      adapter_status = adapt_lexed_tokens(sequel_syntax_lexed(syntax), &tokens, &scanned);
    if (adapter_status == SEQUEL_OK)
      count = scanned;
    else {
      count = 0;
      scanned = 0;
    }
    if (call->ambiguous_literal) {
      diagnostic_report(sql, n, &first, SEQUEL_DIAG_AMBIGUOUS_LITERAL, call->ambiguous_offset,
                        call->ambiguous_offset + 2);
      count = SIZE_MAX;
    }
    if (syntax)
      analysis_structure_from_lexed(sequel_syntax_lexed(syntax), out);
    else
      analysis_structure(sql, n, tokens, scanned, out);
    if (count == SIZE_MAX) {
      if (out->statement_count && scanned && !(tokens[scanned - 1].kind == 0 && sym(tokens[scanned - 1].t, ';')))
        ((sequel_range_t *)out->statements)[out->statement_count - 1].end_offset = n;
      else if (first.offset < n)
        analysis_add_statement(out, first.offset, n);
      if (!parse_issue_count)
        analysis_add_diagnostic(out, first.code, first.offset, first.end_offset);
      valid = SEQUEL_ERR_INVALID_ARGUMENT;
    } else if (syntax && parse_issue_count) {
      for (size_t issue_index = 0; issue_index < parse_issue_count; ++issue_index) {
        sequel_diagnostic_t parser_diagnostic;
        if (sequel_syntax_diagnostic(syntax, issue_index, &parser_diagnostic) == SEQUEL_OK &&
            !analysis_has_diagnostic(out, parser_diagnostic.code, parser_diagnostic.offset,
                                     parser_diagnostic.end_offset))
          analysis_add_diagnostic(out, parser_diagnostic.code, parser_diagnostic.offset, parser_diagnostic.end_offset);
      }
    }
    if (out->diagnostic_count) {
      analysis_sort_diagnostics(out);
      analysis_positions(sql, out);
      out->status = "malformed";
      out->omission_reason = "malformed";
      valid = SEQUEL_ERR_INVALID_ARGUMENT;
    } else {
      analysis_text safe = {0};
      analysis_map map = {0};
      analysis_layout layout = {0};
      sequel_sha256 hash;
      sha_init(&hash);
      sha_frame(&hash, 'V', "sequel-fingerprint-v1", 21, false);
      sha_frame(&hash, 'D', analysis_dialect_name(o.dialect), strlen(analysis_dialect_name(o.dialect)), false);
      bool maps = !o.omit_source_maps && !o.omit_query_text;
      analysis_sanitize(sql, n, tokens, count, &a, &safe, maps ? &map : NULL, &layout, &hash);
      if (o.keep_in_lists)
        analysis_compact_list_text(&safe);
      if (out->statement_count) {
        char digest[65];
        sha_final(&hash, digest);
        snprintf(out->fingerprint, sizeof(out->fingerprint), "sequel:v1:%s:%s", analysis_dialect_name(o.dialect),
                 digest);
        analysis_charge(&a, strlen(out->fingerprint) + 1);
      }
      if (query)
        analysis_summary_from_query(query, out);
      else
        analysis_summary(&layout, &safe, tokens, count, o.dialect, out);
      analysis_charge(&a, strlen(out->operation) + 1);
      size_t limit = o.max_query_text_bytes ? o.max_query_text_bytes : SEQUEL_TEXT_MAX;
      if (!o.omit_query_text && safe.n > limit) {
        out->truncated = true;
        utf8_cut(safe.p, limit);
        safe.n = strlen(safe.p);
        if (maps)
          analysis_clip_map(&map, safe.n);
      }
      out->status = "ok";
      out->omission_reason = o.omit_query_text ? "policy" : !safe.n ? "empty" : "";
      if (o.include_lints) {
        analysis_lints(sql, n, tokens, count, o.dialect, out);
        analysis_positions(sql, out);
      }
      if (o.omit_query_text)
        free(safe.p);
      else {
        out->text = safe.p;
        analysis_charge(&a, 1);
        if (maps) {
          out->original_to_sanitized = (sequel_source_map_t){n, safe.n, map.p, map.n};
        }
        if (o.format && safe.n && !out->truncated) {
          size_t capacity = safe.n + 128;
          char *formatted = malloc(capacity);
          size_t before = call->output_bytes;
          output b = {formatted, 0, capacity, false, true};
          formatted[0] = 0;
          while (!format_layout(layout.p, layout.n, &b)) {
            if (!b.overflow) {
              free(formatted);
              formatted = NULL;
              call->output_bytes = before;
              break;
            }
            if (capacity > SIZE_MAX / 2)
              sequel_abort(SEQUEL_ERR_LIMIT);
            capacity *= 2;
            formatted = realloc(formatted, capacity);
            formatted[0] = 0;
            call->output_bytes = before;
            b = (output){formatted, 0, capacity, false, true};
          }
          if (formatted) {
            analysis_charge(&a, 1);
            out->formatted = formatted;
            if (maps) {
              analysis_map fm = {0};
              analysis_format_map(&layout, safe.n, b.n, &fm);
              out->sanitized_to_formatted = (sequel_source_map_t){safe.n, b.n, fm.p, fm.n};
            }
          }
        }
      }
      free(layout.p);
    }
    sequel_query_document_free(query);
    sequel_syntax_document_free(syntax);
    free(tokens);
  }

  sequel_work_tick(1);
  out->memory_peak_bytes = call->peak;
  out->work_units = call->work;
  out->_storage = call->allocations;
  sequel_current_call = NULL;
  sequel_call_delete(call);
  return valid;
}

sequel_status_t sequel_analyze(const char *sql, const sequel_analysis_options_t *options, sequel_analysis_t *out) {
  if (!out)
    return SEQUEL_ERR_NULL;
  sequel_analysis_options_t o = options ? *options : (sequel_analysis_options_t){0};
  size_t n = 0, limit = o.max_input_bytes ? o.max_input_bytes : SEQUEL_INPUT_MAX;
  if (limit > SEQUEL_INPUT_MAX)
    limit = SEQUEL_INPUT_MAX;
  if (sql)
    while (n <= limit && sql[n]) {
      if ((n & 255) == 0 && o.cancelled && o.cancelled(o.cancel_userdata))
        return analysis_fail(out, n, o.dialect, SEQUEL_ERR_CANCELLED, SEQUEL_DIAG_CANCELLED);
      if (o.max_work_units && n >= o.max_work_units)
        return analysis_fail(out, n, o.dialect, SEQUEL_ERR_LIMIT, SEQUEL_DIAG_RESOURCE_LIMIT);
      n++;
    }
  if (o.max_work_units) {
    if (n >= o.max_work_units)
      return analysis_fail(out, n, o.dialect, SEQUEL_ERR_LIMIT, SEQUEL_DIAG_RESOURCE_LIMIT);
    o.max_work_units -= n;
  }
  sequel_status_t status = sequel_analyze_n(sql, n, &o, out);
  if (n <= SIZE_MAX - out->work_units)
    out->work_units += n;
  return status;
}

sequel_status_t sequel_fingerprint(const char *sql, sequel_dialect_t dialect, char out[96]) {
  if (!out)
    return SEQUEL_ERR_NULL;
  out[0] = 0;
  sequel_analysis_options_t o = {0};
  o.dialect = dialect;
  o.omit_query_text = true;
  o.omit_source_maps = true;
  o.omit_bind_descriptors = true;
  sequel_analysis_t a;
  sequel_status_t status = sequel_analyze(sql, &o, &a);
  if (status == SEQUEL_OK)
    memcpy(out, a.fingerprint, sizeof(a.fingerprint));
  sequel_analysis_free(&a);
  return status;
}

sequel_status_t sequel_check_binds(const sequel_bind_descriptor_t *binds, size_t count, size_t statement,
                                   size_t supplied) {
  if (!binds && count)
    return SEQUEL_ERR_NULL;
  size_t needed = 0;
  for (size_t i = 0; i < count; i++) {
    sequel_work_tick(1);
    if (!binds[i].slot || !binds[i].ordinal || binds[i].offset >= binds[i].end_offset ||
        binds[i].style < SEQUEL_BIND_POSITIONAL || binds[i].style > SEQUEL_BIND_TYPED)
      return SEQUEL_ERR_INVALID_ARGUMENT;
    if (binds[i].statement_index == statement && binds[i].slot > needed)
      needed = binds[i].slot;
  }
  return supplied == needed ? SEQUEL_OK : SEQUEL_ERR_INVALID_ARGUMENT;
}
