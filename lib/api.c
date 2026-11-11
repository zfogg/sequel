#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sequel.h>
#include <sequel/parser.h>
#include "util/text.h"
#include "util/utf8.h"

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

static void trim_sanitized_prefix(sequel_text_result_t *result) {
  if (!result || !result->data)
    return;
  size_t prefix = 0;
  while (prefix < result->length && isspace((unsigned char)result->data[prefix]))
    ++prefix;
  if (!prefix)
    return;
  memmove(result->data, result->data + prefix, result->length - prefix + 1);
  result->length -= prefix;
}

static size_t next_significant_token(const sequel_token_t *tokens, size_t count, size_t index) {
  while (index < count &&
         (tokens[index].kind == SEQUEL_TOKEN_WHITESPACE || tokens[index].kind == SEQUEL_TOKEN_LINE_COMMENT ||
          tokens[index].kind == SEQUEL_TOKEN_BLOCK_COMMENT))
    ++index;
  return index;
}

static bool token_is_symbol(const sequel_source_view_t *source, const sequel_token_t *token, char symbol) {
  return token->kind == SEQUEL_TOKEN_PUNCTUATION && token->range.end == token->range.start + 1 &&
         source->data[token->range.start] == symbol;
}

static bool token_is_question(const sequel_source_view_t *source, const sequel_token_t *token) {
  return (token->kind == SEQUEL_TOKEN_PARAMETER || token->kind == SEQUEL_TOKEN_OPERATOR) &&
         token->range.end == token->range.start + 1 && source->data[token->range.start] == '?';
}

static sequel_status_t collapse_in_result(sequel_text_result_t *result, sequel_dialect_t dialect) {
  if (!result || !result->data || !result->length)
    return SEQUEL_OK;
  sequel_parse_options_t options;
  sequel_parse_options_init(&options);
  options.dialect = dialect;
  options.retain_comments = true;
  options.retain_whitespace = true;
  sequel_source_view_t source = {result->data, result->length};
  sequel_lexed_document_t *lexed = NULL;
  sequel_status_t status = sequel_lex(&source, &options, NULL, &lexed);
  if (status != SEQUEL_OK)
    return status;
  size_t count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &count);
  size_t *starts = NULL, *ends = NULL, replacements = 0, capacity = 0;
  for (size_t i = 0; i < count; ++i) {
    if (tokens[i].kind != SEQUEL_TOKEN_KEYWORD || tokens[i].keyword_id != SEQUEL_KEYWORD_IN)
      continue;
    size_t open = next_significant_token(tokens, count, i + 1);
    if (open >= count || !token_is_symbol(&source, &tokens[open], '('))
      continue;
    size_t cursor = next_significant_token(tokens, count, open + 1), values = 0;
    while (cursor < count && token_is_question(&source, &tokens[cursor])) {
      ++values;
      cursor = next_significant_token(tokens, count, cursor + 1);
      if (cursor < count && token_is_symbol(&source, &tokens[cursor], ','))
        cursor = next_significant_token(tokens, count, cursor + 1);
      else
        break;
    }
    if (values < 2 || cursor >= count || !token_is_symbol(&source, &tokens[cursor], ')'))
      continue;
    if (replacements == capacity) {
      size_t next = capacity ? capacity * 2 : 8;
      size_t *new_starts = malloc(next * sizeof(*starts));
      size_t *new_ends = malloc(next * sizeof(*ends));
      if (!new_starts || !new_ends) {
        free(new_starts);
        free(new_ends);
        free(starts);
        free(ends);
        sequel_lexed_document_free(lexed);
        return SEQUEL_ERR_NOMEM;
      }
      if (replacements) {
        memcpy(new_starts, starts, replacements * sizeof(*starts));
        memcpy(new_ends, ends, replacements * sizeof(*ends));
      }
      free(starts);
      free(ends);
      starts = new_starts;
      ends = new_ends;
      capacity = next;
    }
    starts[replacements] = tokens[open].range.start;
    ends[replacements++] = tokens[cursor].range.end;
  }
  for (size_t i = replacements; i > 0; --i) {
    size_t start = starts[i - 1], end = ends[i - 1];
    memmove(result->data + start + 3, result->data + end, result->length - end + 1);
    memcpy(result->data + start, "(?)", 3);
    result->length -= end - start - 3;
  }
  free(starts);
  free(ends);
  sequel_lexed_document_free(lexed);
  return SEQUEL_OK;
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
    status = collapse_in_result(&result, dialect);
    if (status != SEQUEL_OK) {
      sequel_text_result_free(&result);
      return status;
    }
  }
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
  sequel_diagnostic_t input_diagnostic = {0};
  sequel_status_t diagnostic_status = sequel_diagnose_n(sql, length, parse_options.dialect, &input_diagnostic);
  if (diagnostic_status != SEQUEL_OK && input_diagnostic.code == SEQUEL_DIAG_UNSUPPORTED_PAYLOAD)
    return diagnostic_status;
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
  trim_sanitized_prefix(&sanitized);
  if (options && !options->keep_in_lists)
    status = collapse_in_result(&sanitized, parse_options.dialect);
  if (status != SEQUEL_OK)
    goto done;
  normalize_sanitized_text(sanitized.data);
  sanitized.length = strlen(sanitized.data);
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
    memcpy(summary.data + limit, "…", 3);
    summary.data[limit + 3] = 0;
    summary.length = limit + 3;
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
      const char *name = statements[0].operation == SEQUEL_OPERATION_SELECT        ? "SELECT"
                         : statements[0].operation == SEQUEL_OPERATION_INSERT      ? "INSERT"
                         : statements[0].operation == SEQUEL_OPERATION_UPDATE      ? "UPDATE"
                         : statements[0].operation == SEQUEL_OPERATION_DELETE      ? "DELETE"
                         : statements[0].operation == SEQUEL_OPERATION_MERGE       ? "MERGE"
                         : statements[0].operation == SEQUEL_OPERATION_CREATE      ? "CREATE"
                         : statements[0].operation == SEQUEL_OPERATION_ALTER       ? "ALTER"
                         : statements[0].operation == SEQUEL_OPERATION_DROP        ? "DROP"
                         : statements[0].operation == SEQUEL_OPERATION_EXPLAIN     ? "EXPLAIN"
                         : statements[0].operation == SEQUEL_OPERATION_OPTIMIZE    ? "OPTIMIZE"
                         : statements[0].operation == SEQUEL_OPERATION_TRUNCATE    ? "TRUNCATE"
                         : statements[0].operation == SEQUEL_OPERATION_TRANSACTION ? "TRANSACTION"
                                                                                   : "";
      if (statements[0].operation == SEQUEL_OPERATION_TRANSACTION) {
        size_t start = 0;
        while (start < source.length && isspace((unsigned char)source.data[start]))
          ++start;
        if (start + 5 <= source.length && !_strnicmp(source.data + start, "BEGIN", 5))
          name = "BEGIN";
        else if (start + 6 <= source.length && !_strnicmp(source.data + start, "COMMIT", 6))
          name = "COMMIT";
        else if (start + 8 <= source.length && !_strnicmp(source.data + start, "ROLLBACK", 8))
          name = "ROLLBACK";
      }
      if (*name) {
        snprintf(out->operation, sizeof(out->operation), "%s", name);
      } else {
        size_t start = 0, end;
        while (start < source.length && isspace((unsigned char)source.data[start]))
          ++start;
        end = start;
        while (end < source.length && isalpha((unsigned char)source.data[end]))
          ++end;
        size_t amount = end - start;
        if (amount >= sizeof(out->operation))
          amount = sizeof(out->operation) - 1;
        for (size_t i = 0; i < amount; ++i)
          out->operation[i] = (char)toupper((unsigned char)source.data[start + i]);
        out->operation[amount] = 0;
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
  while (n <= SEQUEL_INPUT_MAX && sql[n])
    ++n;
  if (n > SEQUEL_INPUT_MAX)
    return SEQUEL_ERR_OVERFLOW;
  if (!sequel_utf8_valid(sql, n))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (o.dialect != SEQUEL_MYSQL && sequel_text_find(sql, n, "_utf8mb4'"))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  return inspect_document(sql, n, &o, out);
}
