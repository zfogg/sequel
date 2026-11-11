#include <stdlib.h>
#include <string.h>

#include <sequel/formatter.h>

void sequel_format_options_init(sequel_format_options_t *options) {
  if (options)
    *options = (sequel_format_options_t){
        .mode = SEQUEL_FORMAT_SYNTAX_AWARE, .preserve_comments = true, .preserve_literals = true};
}

void sequel_text_result_free(sequel_text_result_t *result) {
  if (!result)
    return;
  free(result->data);
  *result = (sequel_text_result_t){0};
}

static sequel_status_t copy_source(sequel_source_view_t source, sequel_format_mode_t mode, sequel_text_result_t *out) {
  if (source.length == SIZE_MAX)
    return SEQUEL_ERR_OVERFLOW;
  char *data = malloc(source.length + 1);
  if (!data)
    return SEQUEL_ERR_NOMEM;
  memcpy(data, source.data, source.length);
  data[source.length] = 0;
  sequel_text_result_free(out);
  out->data = data;
  out->length = source.length;
  out->capacity = source.length + 1;
  out->mode = mode;
  return SEQUEL_OK;
}

static int append_text(char **data, size_t *length, size_t *capacity, const char *text, size_t amount) {
  if (amount > SIZE_MAX - *length - 1)
    return 0;
  size_t required = *length + amount + 1;
  if (required > *capacity) {
    size_t next = *capacity ? *capacity : 64;
    while (next < required) {
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
  memcpy(*data + *length, text, amount);
  *length += amount;
  (*data)[*length] = 0;
  return 1;
}

static int append_space(char **data, size_t *length, size_t *capacity) {
  return !*length || (*data)[*length - 1] == ' ' || (*data)[*length - 1] == '\n' ||
         append_text(data, length, capacity, " ", 1);
}

static int is_word_token(sequel_token_kind_t kind) {
  return kind == SEQUEL_TOKEN_IDENTIFIER || kind == SEQUEL_TOKEN_KEYWORD || kind == SEQUEL_TOKEN_BOOLEAN ||
         kind == SEQUEL_TOKEN_NULL || kind == SEQUEL_TOKEN_NUMBER || kind == SEQUEL_TOKEN_STRING ||
         kind == SEQUEL_TOKEN_QUOTED_IDENTIFIER || kind == SEQUEL_TOKEN_PARAMETER;
}

static sequel_status_t format_lexed(const sequel_lexed_document_t *lexed, const sequel_format_options_t *settings,
                                    sequel_text_result_t *out) {
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  sequel_source_view_t source = sequel_lexed_source(lexed);
  char *data = NULL;
  size_t length = 0, capacity = 0;
  sequel_token_kind_t previous = SEQUEL_TOKEN_EOF;
  for (size_t i = 0; i < token_count; ++i) {
    const sequel_token_t *token = &tokens[i];
    if (token->kind == SEQUEL_TOKEN_EOF)
      break;
    if (token->range.end > source.length || token->range.start > token->range.end)
      goto invalid;
    if (token->kind == SEQUEL_TOKEN_WHITESPACE)
      continue;
    if ((token->kind == SEQUEL_TOKEN_LINE_COMMENT || token->kind == SEQUEL_TOKEN_BLOCK_COMMENT) &&
        !settings->preserve_comments)
      continue;
    if (token->kind == SEQUEL_TOKEN_LINE_COMMENT || token->kind == SEQUEL_TOKEN_BLOCK_COMMENT) {
      if (!append_space(&data, &length, &capacity) ||
          !append_text(&data, &length, &capacity, source.data + token->range.start,
                       token->range.end - token->range.start) ||
          (token->kind == SEQUEL_TOKEN_LINE_COMMENT && !append_text(&data, &length, &capacity, "\n", 1)))
        goto no_memory;
      previous = token->kind;
      continue;
    }
    bool literal = token->flags & SEQUEL_TOKEN_FLAG_LITERAL;
    bool punctuation = token->kind == SEQUEL_TOKEN_PUNCTUATION;
    const char *text = source.data + token->range.start;
    size_t text_length = token->range.end - token->range.start;
    if (literal && !(token->flags & SEQUEL_TOKEN_FLAG_BIND) && !settings->preserve_literals) {
      text = "?";
      text_length = 1;
    }
    bool no_space_before = punctuation && text_length == 1 && (text[0] == ',' || text[0] == ')' || text[0] == ';');
    bool no_space_after_previous =
        previous == SEQUEL_TOKEN_PUNCTUATION && length && (data[length - 1] == '(' || data[length - 1] == '.');
    if (!no_space_before && !no_space_after_previous &&
        (is_word_token(previous) || is_word_token(token->kind) ||
         (previous == SEQUEL_TOKEN_PUNCTUATION && data[length - 1] == ',')))
      if (!append_space(&data, &length, &capacity))
        goto no_memory;
    if (!append_text(&data, &length, &capacity, text, text_length))
      goto no_memory;
    if (punctuation && text_length == 1 && text[0] == ';' && !append_text(&data, &length, &capacity, "\n", 1))
      goto no_memory;
    previous = token->kind;
  }
  if (!data) {
    data = malloc(1);
    if (!data)
      goto no_memory;
    data[0] = 0;
    capacity = 1;
  }
  sequel_text_result_free(out);
  *out = (sequel_text_result_t){data, length, capacity, settings->mode};
  return SEQUEL_OK;
invalid:
  free(data);
  return SEQUEL_ERR_INVALID_ARGUMENT;
no_memory:
  free(data);
  return SEQUEL_ERR_NOMEM;
}

sequel_status_t sequel_format_tokens(const sequel_lexed_document_t *lexed, const sequel_format_options_t *options,
                                     sequel_text_result_t *out) {
  if (!lexed || !out)
    return SEQUEL_ERR_NULL;
  sequel_format_options_t defaults;
  sequel_format_options_init(&defaults);
  const sequel_format_options_t *settings = options ? options : &defaults;
  if (settings->mode == SEQUEL_FORMAT_SYNTAX_AWARE)
    return format_lexed(lexed, settings, out);
  return copy_source(sequel_lexed_source(lexed),
                     settings->mode == SEQUEL_FORMAT_SYNTAX_AWARE ? SEQUEL_FORMAT_LEXICAL : settings->mode, out);
}

sequel_status_t sequel_format_syntax(const sequel_syntax_document_t *syntax, const sequel_format_options_t *options,
                                     sequel_text_result_t *out) {
  if (!syntax || !out)
    return SEQUEL_ERR_NULL;
  sequel_format_options_t defaults;
  sequel_format_options_init(&defaults);
  const sequel_format_options_t *settings = options ? options : &defaults;
  size_t issues = 0;
  (void)sequel_syntax_issues(syntax, &issues);
  if (!issues && settings->mode == SEQUEL_FORMAT_SYNTAX_AWARE)
    return format_lexed(sequel_syntax_lexed(syntax), settings, out);
  return copy_source(sequel_lexed_source(sequel_syntax_lexed(syntax)),
                     issues ? SEQUEL_FORMAT_RECOVERED : settings->mode, out);
}
