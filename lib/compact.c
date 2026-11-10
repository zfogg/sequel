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
  sequel_parse_options_t options;
  sequel_parse_options_init(&options);
  options.dialect = dialect;
  options.retain_comments = true;
  options.retain_whitespace = true;
  sequel_source_view_t source = {sql, n};
  sequel_lexed_document_t *document = NULL;
  sequel_status_t status = sequel_lex(&source, &options, NULL, &document);
  if (status != SEQUEL_OK)
    return status;
  size_t count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(document, &count);
  output b = {out, 0, capacity};
  size_t previous = 0;
  for (size_t i = 0; i < count; ++i) {
    const sequel_token_t *current = &tokens[i];
    if (current->kind == SEQUEL_TOKEN_EOF)
      break;
    size_t gap = current->range.start - previous;
    if (gap) {
      if (memchr(sql + previous, '\n', gap) || memchr(sql + previous, '\r', gap))
        put(&b, sql + previous, gap);
      else
        text(&b, " ");
    }
    if (current->kind != SEQUEL_TOKEN_WHITESPACE)
      put(&b, sql + current->range.start, current->range.end - current->range.start);
    previous = current->range.end;
  }
  if (previous < n)
    put(&b, sql + previous, n - previous);
  sequel_lexed_document_free(document);
  if (b.overflow) {
    *out = 0;
    return SEQUEL_ERR_OVERFLOW;
  }
  return SEQUEL_OK;
}
