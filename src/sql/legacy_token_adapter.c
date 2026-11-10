static int legacy_token_kind(sequel_token_kind_t kind) {
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

static sequel_status_t legacy_adapt_lexed(const sequel_lexed_document_t *lexed, format_token **out, size_t *count) {
  if (!lexed || !out || !count)
    return SEQUEL_ERR_NULL;
  *out = NULL;
  *count = 0;
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  sequel_source_view_t source = sequel_lexed_source(lexed);
  size_t capacity = 0;
  size_t previous_end = 0;
  for (size_t i = 0; i < token_count; ++i) {
    const sequel_token_t *token = &tokens[i];
    if (token->kind == SEQUEL_TOKEN_EOF)
      break;
    if (token->range.end > source.length || token->range.start > token->range.end)
      goto invalid;
    if (token->kind == SEQUEL_TOKEN_WHITESPACE) {
      previous_end = token->range.end;
      continue;
    }
    if (*count == capacity) {
      size_t next = capacity ? capacity * 2 : 64;
      if (next < *count || next > SIZE_MAX / sizeof(**out))
        goto overflow;
      format_token *grown = realloc(*out, next * sizeof(*grown));
      if (!grown)
        goto no_memory;
      *out = grown;
      capacity = next;
    }
    bool newline = false;
    for (size_t offset = previous_end; offset < token->range.start; ++offset)
      newline |= source.data[offset] == '\r' || source.data[offset] == '\n';
    int kind = legacy_token_kind(token->kind);
    (*out)[(*count)++] = (format_token){
        .t = {.p = source.data + token->range.start,
              .n = token->range.end - token->range.start,
              .word = kind == 1,
              .space = token->range.start > previous_end,
              .clickhouse = sequel_lexed_dialect(lexed) == SEQUEL_CLICKHOUSE},
        .gap = token->range.start > previous_end,
        .newline = newline,
        .kind = kind,
        .matching = SIZE_MAX,
        .multiline = newline,
        .flat_width = token->range.end - token->range.start,
        .source_offset = token->range.start,
        .rendered_offset = 0};
    previous_end = token->range.end;
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
