#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <sequel.h>
#include <sequel/parser.h>
#include "metadata.h"
#include "metadata_document.h"
#include "metadata_text.h"
#include "tokens.h"
#include "util/text.h"
#include "util/utf8.h"

static bool sequel_api_word(const char *text, size_t length, const char *word) {
  size_t word_length = strlen(word);
  if (length != word_length)
    return false;
  for (size_t i = 0; i < length; ++i)
    if (toupper((unsigned char)text[i]) != toupper((unsigned char)word[i]))
      return false;
  return true;
}

static bool sequel_api_clickhouse_payload(const sequel_source_view_t *source, const sequel_token_t *tokens,
                                          size_t count) {
  bool insert = false;
  for (size_t i = 0; i < count; ++i) {
    if (tokens[i].kind == SEQUEL_TOKEN_EOF)
      break;
    if (tokens[i].kind == SEQUEL_TOKEN_WHITESPACE || sequel_token_is_comment(&tokens[i]))
      continue;
    const char *text = source->data + tokens[i].range.start;
    size_t length = tokens[i].range.end - tokens[i].range.start;
    if (sequel_api_word(text, length, "INSERT")) {
      insert = true;
      continue;
    }
    if (insert && sequel_api_word(text, length, "FORMAT"))
      return true;
    if (tokens[i].kind == SEQUEL_TOKEN_PUNCTUATION && *text == ';')
      insert = false;
  }
  return false;
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
  while (n <= SEQUEL_INPUT_MAX && sql[n])
    ++n;
  if (n > SEQUEL_INPUT_MAX)
    return SEQUEL_ERR_OVERFLOW;
  if (!sequel_utf8_valid(sql, n))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (dialect != SEQUEL_MYSQL && sequel_text_find(sql, n, "_utf8mb4'"))
    return SEQUEL_ERR_INVALID_ARGUMENT;
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
  if (dialect == SEQUEL_CLICKHOUSE) {
    size_t token_count = 0;
    const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
    if (sequel_api_clickhouse_payload(&source, tokens, token_count)) {
      sequel_lexed_document_free(lexed);
      return SEQUEL_ERR_INVALID_ARGUMENT;
    }
  }
  sequel_text_result_t result = {0};
  status = sequel_sanitize_tokens(lexed, flags, &result);
  sequel_lexed_document_free(lexed);
  if (status != SEQUEL_OK)
    return status;
  if (collapse_in_lists && (flags & SEQUEL_REPLACE_LITERALS)) {
    status = sequel_tokens_collapse_in_lists(&result, dialect);
    if (status != SEQUEL_OK) {
      sequel_text_result_free(&result);
      return status;
    }
  }
  if (flags & SEQUEL_SANITIZE_REMOVE_COMMENTS)
    sequel_metadata_normalize_text(result.data);
  *truncated = result.length > limit;
  sequel_metadata_cut_utf8(result.data, limit);
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

sequel_status_t sequel_metadata_n(const char *sql, size_t n, const sequel_metadata_options_t *options,
                                  sequel_metadata_t *out) {
  if (!out)
    return SEQUEL_ERR_NULL;
  *out = (sequel_metadata_t){.text = "", .summary = "SQL"};
  if (!sql)
    return SEQUEL_ERR_NULL;
  sequel_metadata_options_t o = options ? *options : (sequel_metadata_options_t){0};
  if (o.dialect > SEQUEL_CLICKHOUSE || o.dialect < SEQUEL_GENERIC || o.max_query_text_bytes > SEQUEL_TEXT_MAX)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (n > SEQUEL_INPUT_MAX)
    return SEQUEL_ERR_OVERFLOW;
  if (memchr(sql, 0, n))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (!sequel_utf8_valid(sql, n))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (!n)
    return SEQUEL_OK;
  if (o.dialect != SEQUEL_MYSQL && sequel_text_find(sql, n, "_utf8mb4'"))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  return sequel_metadata_document(sql, n, &o, out);
}

sequel_status_t sequel_metadata(const char *sql, const sequel_metadata_options_t *options, sequel_metadata_t *out) {
  if (out)
    *out = (sequel_metadata_t){.text = "", .summary = "SQL"};
  if (!sql)
    return SEQUEL_ERR_NULL;
  size_t n = 0;
  while (n <= SEQUEL_INPUT_MAX && sql[n])
    ++n;
  return n > SEQUEL_INPUT_MAX ? SEQUEL_ERR_OVERFLOW : sequel_metadata_n(sql, n, options, out);
}
