#include "analysis_internal.h"

void sequel_analysis_add_statement(sequel_analysis_t *out, size_t start, size_t end) {
  size_t count = out->statement_count;
  sequel_output_tick(sizeof(sequel_range_t));
  sequel_range_t *ranges = sequel_analysis_array((void *)out->statements, count + 1, sizeof(*ranges));
  if (!ranges)
    return;
  ranges[count] = (sequel_range_t){start, end};
  out->statements = ranges;
  out->statement_count++;
}

void sequel_analysis_recovered_statement_ranges(const sequel_lexed_document_t *lexed, sequel_analysis_t *out) {
  size_t count = 0, start = SIZE_MAX, last = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &count);
  sequel_source_view_t source = sequel_lexed_source(lexed);
  for (size_t i = 0; i < count; ++i) {
    const sequel_token_t *token = &tokens[i];
    if (token->kind == SEQUEL_TOKEN_EOF)
      break;
    if (sequel_token_is_trivia(token))
      continue;
    if (start == SIZE_MAX)
      start = token->range.start;
    last = token->range.end;
    if (sequel_token_is_symbol(source, token, ';')) {
      sequel_analysis_add_statement(out, start, last);
      start = SIZE_MAX;
    }
  }
  if (start != SIZE_MAX)
    sequel_analysis_add_statement(out, start, last);
}
