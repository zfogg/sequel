#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <sequel.h>
#include <sequel/parser.h>
#include "inspection.h"
#include "inspection_document.h"
#include "inspection_text.h"
#include "tokens.h"
#include "util/text.h"
#include "util/utf8.h"

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
  if (collapse_in_lists && (flags & SEQUEL_REPLACE_LITERALS)) {
    status = sequel_tokens_collapse_in_lists(&result, dialect);
    if (status != SEQUEL_OK) {
      sequel_text_result_free(&result);
      return status;
    }
  }
  if (flags & SEQUEL_SANITIZE_REMOVE_COMMENTS)
    sequel_inspection_normalize_text(result.data);
  *truncated = result.length > limit;
  sequel_inspection_cut_utf8(result.data, limit);
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
  while (n <= SEQUEL_INPUT_MAX && sql[n])
    ++n;
  if (n > SEQUEL_INPUT_MAX)
    return SEQUEL_ERR_OVERFLOW;
  if (!sequel_utf8_valid(sql, n))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (o.dialect != SEQUEL_MYSQL && sequel_text_find(sql, n, "_utf8mb4'"))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  return sequel_inspection_document(sql, n, &o, out);
}
