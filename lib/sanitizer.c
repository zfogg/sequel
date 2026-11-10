#include <stdlib.h>
#include <string.h>

#include <sequel/sanitizer.h>

static int sanitizer_append_bytes(char **data, size_t *length, size_t *capacity, const char *bytes, size_t amount) {
  if (amount > SIZE_MAX - *length)
    return 0;
  size_t required = *length + amount;
  if (required + 1 > *capacity) {
    size_t next = *capacity ? *capacity : 64;
    while (next < required + 1) {
      if (next > SIZE_MAX / 2)
        return 0;
      next *= 2;
    }
    char *grown = realloc(*data, next);
    if (!grown)
      return 0;
    *data = grown;
    *capacity = next;
  }
  memcpy(*data + *length, bytes, amount);
  *length += amount;
  (*data)[*length] = 0;
  return 1;
}

sequel_status_t sequel_sanitize_tokens(const sequel_lexed_document_t *lexed, unsigned flags,
                                       sequel_text_result_t *out) {
  if (!lexed || !out)
    return SEQUEL_ERR_NULL;
  sequel_source_view_t source = sequel_lexed_source(lexed);
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  char *data = NULL;
  size_t length = 0, capacity = 0;
  for (size_t i = 0; i < token_count; ++i) {
    const sequel_token_t *token = &tokens[i];
    if (token->kind == SEQUEL_TOKEN_EOF)
      break;
    if (token->range.end > source.length || token->range.start > token->range.end)
      goto invalid;
    if (token->flags & (SEQUEL_TOKEN_FLAG_UNTERMINATED | SEQUEL_TOKEN_FLAG_RECOVERED))
      goto invalid;
    if ((flags & SEQUEL_SANITIZE_REMOVE_COMMENTS) &&
        (token->kind == SEQUEL_TOKEN_LINE_COMMENT || token->kind == SEQUEL_TOKEN_BLOCK_COMMENT)) {
      bool adjacent_block = token->kind == SEQUEL_TOKEN_BLOCK_COMMENT && i + 1 < token_count &&
                            tokens[i + 1].range.start == token->range.end;
      if (adjacent_block && length && data[length - 1] != ' ' && data[length - 1] != '\n')
        if (!sanitizer_append_bytes(&data, &length, &capacity, " ", 1))
          goto no_memory;
      continue;
    }
    if ((flags & SEQUEL_SANITIZE_REPLACE_LITERALS) &&
        ((token->flags & SEQUEL_TOKEN_FLAG_LITERAL) || token->kind == SEQUEL_TOKEN_PARAMETER)) {
      if (!sanitizer_append_bytes(&data, &length, &capacity, "?", 1))
        goto no_memory;
      continue;
    }
    if (!sanitizer_append_bytes(&data, &length, &capacity, source.data + token->range.start,
                                token->range.end - token->range.start))
      goto no_memory;
  }
  if (!data) {
    data = malloc(1);
    if (!data)
      goto no_memory;
    data[0] = 0;
    capacity = 1;
  }
  sequel_text_result_free(out);
  *out = (sequel_text_result_t){.data = data, .length = length, .capacity = capacity, .mode = SEQUEL_FORMAT_LEXICAL};
  return SEQUEL_OK;
invalid:
  free(data);
  return SEQUEL_ERR_INVALID_ARGUMENT;
no_memory:
  free(data);
  return SEQUEL_ERR_NOMEM;
}
