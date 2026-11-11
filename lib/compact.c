/* Lossless whitespace compaction over the public token stream. */
#include <string.h>

#include <sequel.h>
#include "util/utf8.h"

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
  bool overflow;
} compact_output;

static void compact_append(compact_output *output, const char *data, size_t length) {
  if (length > output->capacity - output->length - 1) {
    output->overflow = true;
    return;
  }
  memcpy(output->data + output->length, data, length);
  output->length += length;
  output->data[output->length] = 0;
}

static void compact_append_text(compact_output *output, const char *text) {
  compact_append(output, text, strlen(text));
}

sequel_status_t sequel_compact(const char *sql, sequel_dialect_t dialect, char *out, size_t capacity) {
  if (!sql || !out)
    return SEQUEL_ERR_NULL;
  if (!capacity || dialect < SEQUEL_GENERIC || dialect > SEQUEL_CLICKHOUSE)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  *out = 0;
  size_t n = 0;
  while (n <= SEQUEL_INPUT_MAX && sql[n])
    ++n;
  if (n > SEQUEL_INPUT_MAX)
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
  compact_output b = {out, 0, capacity, false};
  size_t previous = 0;
  for (size_t i = 0; i < count; ++i) {
    const sequel_token_t *current = &tokens[i];
    if (current->kind == SEQUEL_TOKEN_EOF)
      break;
    size_t gap = current->range.start - previous;
    if (current->kind == SEQUEL_TOKEN_WHITESPACE) {
      if (memchr(sql + current->range.start, '\n', current->range.end - current->range.start) ||
          memchr(sql + current->range.start, '\r', current->range.end - current->range.start))
        compact_append(&b, sql + current->range.start, current->range.end - current->range.start);
      else if (b.length && b.data[b.length - 1] != ' ')
        compact_append_text(&b, " ");
      previous = current->range.end;
      continue;
    }
    if (gap) {
      if (memchr(sql + previous, '\n', gap) || memchr(sql + previous, '\r', gap))
        compact_append(&b, sql + previous, gap);
      else
        compact_append_text(&b, " ");
    }
    if (current->kind != SEQUEL_TOKEN_WHITESPACE)
      compact_append(&b, sql + current->range.start, current->range.end - current->range.start);
    previous = current->range.end;
  }
  if (previous < n)
    compact_append(&b, sql + previous, n - previous);
  sequel_lexed_document_free(document);
  if (b.overflow) {
    *out = 0;
    return SEQUEL_ERR_OVERFLOW;
  }
  return SEQUEL_OK;
}
