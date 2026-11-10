#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <sequel/parser.h>
#include <sequel/dialect.h>

#ifndef SEQUEL_INPUT_MAX
#define SEQUEL_INPUT_MAX ((size_t)1024 * 1024 * 1024)
#endif

struct sequel_lexed_document {
  char *source;
  size_t length;
  sequel_dialect_t dialect;
  sequel_token_t *tokens;
  size_t token_count;
};

static int is_word_start(unsigned char c) { return isalpha(c) || c == '_'; }
static int is_word_part(unsigned char c) { return isalnum(c) || c == '_' || c >= 128; }
static int is_operator_char(unsigned char c) {
  return c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '=' || c == '!' || c == '<' || c == '>' ||
         c == '~' || c == '|' || c == '&' || c == '^';
}

static int append_token(sequel_lexed_document_t *document, sequel_token_t token, size_t limit) {
  if (document->token_count >= limit)
    return 0;
  size_t count = document->token_count + 1;
  sequel_token_t *tokens = realloc(document->tokens, count * sizeof(*tokens));
  if (!tokens)
    return 0;
  document->tokens = tokens;
  document->tokens[document->token_count++] = token;
  return 1;
}

static size_t scan_quoted(const char *source, size_t length, size_t start, char quote) {
  size_t i = start + 1;
  while (i < length) {
    if (source[i] == quote) {
      if (i + 1 < length && source[i + 1] == quote) {
        i += 2;
        continue;
      }
      return i + 1;
    }
    ++i;
  }
  return length;
}

void sequel_parse_options_init(sequel_parse_options_t *options) {
  if (!options)
    return;
  *options = (sequel_parse_options_t){.dialect = SEQUEL_GENERIC,
                                      .retain_comments = true,
                                      .retain_whitespace = false,
                                      .retain_source_text = true,
                                      .tolerant = true,
                                      .recover_errors = true,
                                      .parse_vendor_extensions = true,
                                      .max_input_bytes = SEQUEL_INPUT_MAX,
                                      .max_tokens = 0,
                                      .max_nodes = 0,
                                      .max_nesting = 64,
                                      .max_diagnostics = 64};
}

sequel_status_t sequel_lex(const sequel_source_view_t *source, const sequel_parse_options_t *options,
                           const sequel_cancellation_t *cancellation, sequel_lexed_document_t **out) {
  if (!source || !out)
    return SEQUEL_ERR_NULL;
  *out = NULL;
  sequel_parse_options_t defaults;
  sequel_parse_options_init(&defaults);
  const sequel_parse_options_t *settings = options ? options : &defaults;
  if (source->length > settings->max_input_bytes || source->length > SEQUEL_INPUT_MAX)
    return SEQUEL_ERR_OVERFLOW;
  sequel_lexed_document_t *document = calloc(1, sizeof(*document));
  if (!document)
    return SEQUEL_ERR_NOMEM;
  document->source = malloc(source->length + 1);
  if (!document->source) {
    free(document);
    return SEQUEL_ERR_NOMEM;
  }
  memcpy(document->source, source->data, source->length);
  document->source[source->length] = 0;
  document->length = source->length;
  document->dialect = settings->dialect;
  size_t limit = settings->max_tokens ? settings->max_tokens : (size_t)-1;
  size_t i = 0;
  while (i < source->length) {
    if (cancellation && cancellation->is_cancelled && cancellation->is_cancelled(cancellation->userdata)) {
      sequel_lexed_document_free(document);
      return SEQUEL_ERR_CANCELLED;
    }
    size_t start = i;
    sequel_token_kind_t kind = SEQUEL_TOKEN_PUNCTUATION;
    uint32_t flags = 0;
    if (isspace((unsigned char)source->data[i])) {
      while (i < source->length && isspace((unsigned char)source->data[i]))
        ++i;
      if (!settings->retain_whitespace)
        continue;
      kind = SEQUEL_TOKEN_WHITESPACE;
    } else if (source->data[i] == '-' && i + 1 < source->length && source->data[i + 1] == '-') {
      i += 2;
      while (i < source->length && source->data[i] != '\n')
        ++i;
      kind = SEQUEL_TOKEN_LINE_COMMENT;
      flags = SEQUEL_TOKEN_FLAG_COMMENT;
      if (!settings->retain_comments)
        continue;
    } else if (source->data[i] == '/' && i + 1 < source->length && source->data[i + 1] == '*') {
      i += 2;
      while (i + 1 < source->length && !(source->data[i] == '*' && source->data[i + 1] == '/'))
        ++i;
      i = i + 1 < source->length ? i + 2 : source->length;
      kind = SEQUEL_TOKEN_BLOCK_COMMENT;
      flags = SEQUEL_TOKEN_FLAG_COMMENT;
      if (!settings->retain_comments)
        continue;
    } else if (is_word_start((unsigned char)source->data[i]) || (unsigned char)source->data[i] >= 128) {
      ++i;
      while (i < source->length && is_word_part((unsigned char)source->data[i]))
        ++i;
      unsigned id = sequel_dialect_keyword_id(settings->dialect, source->data + start, i - start);
      kind = id == 19 || id == 20 ? SEQUEL_TOKEN_BOOLEAN
             : id == 21           ? SEQUEL_TOKEN_NULL
             : id                 ? SEQUEL_TOKEN_KEYWORD
                                  : SEQUEL_TOKEN_IDENTIFIER;
      flags = id ? SEQUEL_TOKEN_FLAG_CASE_FOLDED : 0;
      if (kind == SEQUEL_TOKEN_BOOLEAN || kind == SEQUEL_TOKEN_NULL)
        flags |= SEQUEL_TOKEN_FLAG_LITERAL;
    } else if (source->data[i] == '\'' || source->data[i] == '"' || source->data[i] == '`' ||
               (source->data[i] == '[' && sequel_dialect_definition(settings->dialect)->supports_bracket_identifiers)) {
      char quote = source->data[i++];
      i = scan_quoted(source->data, source->length, start, quote == '[' ? ']' : quote);
      kind = quote == '\'' ? SEQUEL_TOKEN_STRING : SEQUEL_TOKEN_QUOTED_IDENTIFIER;
      flags = i == source->length && source->data[i - 1] != (quote == '[' ? ']' : quote)
                  ? SEQUEL_TOKEN_FLAG_UNTERMINATED
                  : 0;
    } else if (isdigit((unsigned char)source->data[i])) {
      ++i;
      while (i < source->length && (isdigit((unsigned char)source->data[i]) || source->data[i] == '.'))
        ++i;
      kind = SEQUEL_TOKEN_NUMBER;
      flags = SEQUEL_TOKEN_FLAG_LITERAL;
    } else if (source->data[i] == '?' || source->data[i] == ':' || source->data[i] == '$' || source->data[i] == '@') {
      ++i;
      while (i < source->length && is_word_part((unsigned char)source->data[i]))
        ++i;
      kind = SEQUEL_TOKEN_PARAMETER;
      flags = SEQUEL_TOKEN_FLAG_BIND;
    } else if (is_operator_char((unsigned char)source->data[i])) {
      ++i;
      while (i < source->length && is_operator_char((unsigned char)source->data[i]))
        ++i;
      kind = SEQUEL_TOKEN_OPERATOR;
    } else {
      ++i;
      kind = ispunct((unsigned char)source->data[start]) ? SEQUEL_TOKEN_PUNCTUATION : SEQUEL_TOKEN_ERROR;
    }
    sequel_token_t token = {.kind = kind,
                            .flags = flags,
                            .range = {start, i},
                            .leading_trivia = {start, start},
                            .trailing_trivia = {i, i},
                            .keyword_id =
                                kind == SEQUEL_TOKEN_KEYWORD
                                    ? sequel_dialect_keyword_id(settings->dialect, source->data + start, i - start)
                                    : 0,
                            .dialect_id = (uint32_t)settings->dialect};
    if (!append_token(document, token, limit)) {
      sequel_lexed_document_free(document);
      return SEQUEL_ERR_LIMIT;
    }
  }
  sequel_token_t eof = {.kind = SEQUEL_TOKEN_EOF, .range = {source->length, source->length}};
  if (!append_token(document, eof, limit)) {
    sequel_lexed_document_free(document);
    return SEQUEL_ERR_LIMIT;
  }
  *out = document;
  return SEQUEL_OK;
}

void sequel_lexed_document_free(sequel_lexed_document_t *document) {
  if (!document)
    return;
  free(document->tokens);
  free(document->source);
  free(document);
}

sequel_source_view_t sequel_lexed_source(const sequel_lexed_document_t *document) {
  return document ? (sequel_source_view_t){document->source, document->length} : (sequel_source_view_t){0};
}

const sequel_token_t *sequel_lexed_tokens(const sequel_lexed_document_t *document, size_t *count) {
  if (count)
    *count = document ? document->token_count : 0;
  return document ? document->tokens : NULL;
}

sequel_status_t sequel_token_text(const sequel_source_view_t *source, const sequel_token_t *token, const char **data,
                                  size_t *length) {
  if (!source || !token || !data || !length)
    return SEQUEL_ERR_NULL;
  if (!sequel_source_range_valid(token->range, source->length))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  *data = source->data + token->range.start;
  *length = token->range.end - token->range.start;
  return SEQUEL_OK;
}
