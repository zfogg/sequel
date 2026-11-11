/* Build the legacy statement spans from the shared lossless token stream. */
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
