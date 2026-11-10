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
  sequel_source_t *source_object;
  sequel_token_t *tokens;
  size_t token_count;
};

static int is_word_start(unsigned char c) { return isalpha(c) || c == '_'; }

static int is_word_part(unsigned char c) { return isalnum(c) || c == '_' || c >= 128; }

static int is_operator_char(unsigned char c) {
  return c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '=' || c == '!' || c == '<' || c == '>' ||
         c == '~' || c == '|' || c == '&' || c == '^' || c == '@';
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
    if (source[i] == '\\' && i + 1 < length) {
      i += 2;
      continue;
    }
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

static int quoted_token_is_terminated(const char *source, size_t start, size_t end, size_t length, char quote) {
  if (end <= start + 1 || end != length || source[end - 1] != quote)
    return 0;
  size_t backslashes = 0;
  for (size_t i = end - 1; i > start && source[i - 1] == '\\'; --i)
    ++backslashes;
  return (backslashes & 1u) == 0;
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
                                      .max_diagnostics = 64,
                                      .enable_incremental_metadata = true};
}

sequel_status_t sequel_lex(const sequel_source_view_t *source, const sequel_parse_options_t *options,
                           const sequel_cancellation_t *cancellation, sequel_lexed_document_t **out) {
  if (!source || !out)
    return SEQUEL_ERR_NULL;
  *out = NULL;
  sequel_parse_options_t defaults;
  sequel_parse_options_init(&defaults);
  const sequel_parse_options_t *settings = options ? options : &defaults;
  if (!sequel_dialect_definition(settings->dialect))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (source->length > settings->max_input_bytes || source->length > SEQUEL_INPUT_MAX)
    return SEQUEL_ERR_OVERFLOW;
  if ((settings->max_memory_bytes && source->length + 1 > settings->max_memory_bytes) ||
      (settings->max_work_units && source->length > settings->max_work_units))
    return SEQUEL_ERR_LIMIT;
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
  sequel_source_options_t source_options = {
      .view = {document->source, document->length}, .storage = SEQUEL_SOURCE_BORROWED, .dialect = document->dialect};
  if (sequel_source_create(&source_options.view, &source_options, &document->source_object) != SEQUEL_OK) {
    free(document->source);
    free(document);
    return SEQUEL_ERR_NOMEM;
  }
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
      while (i < source->length && source->data[i] != '\n' && source->data[i] != '\r')
        ++i;
      kind = SEQUEL_TOKEN_LINE_COMMENT;
      flags = SEQUEL_TOKEN_FLAG_COMMENT;
      if (!settings->retain_comments)
        continue;
    } else if (source->data[i] == '/' && i + 1 < source->length && source->data[i + 1] == '*') {
      i += 2;
      size_t depth = 1;
      while (i + 1 < source->length && depth) {
        if (source->data[i] == '/' && source->data[i + 1] == '*') {
          ++depth;
          i += 2;
        } else if (source->data[i] == '*' && source->data[i + 1] == '/') {
          --depth;
          i += 2;
        } else {
          ++i;
        }
      }
      int terminated = depth == 0;
      i = terminated ? i : source->length;
      kind = SEQUEL_TOKEN_BLOCK_COMMENT;
      flags = SEQUEL_TOKEN_FLAG_COMMENT;
      if (depth > 64)
        flags |= SEQUEL_TOKEN_FLAG_RECOVERED;
      if (!terminated)
        flags |= SEQUEL_TOKEN_FLAG_UNTERMINATED;
      if (!settings->retain_comments)
        continue;
    } else if (source->data[i] == '#' &&
               (settings->dialect == SEQUEL_MYSQL || settings->dialect == SEQUEL_CLICKHOUSE)) {
      ++i;
      while (i < source->length && source->data[i] != '\n' && source->data[i] != '\r')
        ++i;
      kind = SEQUEL_TOKEN_LINE_COMMENT;
      flags = SEQUEL_TOKEN_FLAG_COMMENT;
      if (!settings->retain_comments)
        continue;
    } else if (settings->dialect == SEQUEL_MYSQL && source->data[i] == '_' && i + 1 < source->length) {
      size_t prefix_end = i + 1;
      while (prefix_end < source->length && is_word_part((unsigned char)source->data[prefix_end]))
        ++prefix_end;
      if (prefix_end < source->length && source->data[prefix_end] == '\'') {
        i = scan_quoted(source->data, source->length, prefix_end, '\'');
        kind = SEQUEL_TOKEN_STRING;
        flags = SEQUEL_TOKEN_FLAG_LITERAL | SEQUEL_TOKEN_FLAG_DIALECT_SPECIFIC;
        if (i == source->length && !quoted_token_is_terminated(source->data, prefix_end, i, source->length, '\''))
          flags |= SEQUEL_TOKEN_FLAG_UNTERMINATED;
      } else {
        ++i;
        while (i < source->length && is_word_part((unsigned char)source->data[i]))
          ++i;
        kind = SEQUEL_TOKEN_IDENTIFIER;
      }
    } else if (settings->dialect == SEQUEL_CLICKHOUSE && i + 2 < source->length &&
               (unsigned char)source->data[i] == 0xe2 && (unsigned char)source->data[i + 1] == 0x80 &&
               ((unsigned char)source->data[i + 2] == 0x98 || (unsigned char)source->data[i + 2] == 0x9b ||
                (unsigned char)source->data[i + 2] == 0x9c || (unsigned char)source->data[i + 2] == 0x9d)) {
      unsigned char opener = (unsigned char)source->data[i + 2];
      unsigned char closer = opener == 0x98 ? 0x99 : opener == 0x9b ? 0x9d : opener == 0x9c ? 0x9d : opener;
      i += 3;
      while (i + 2 < source->length &&
             !((unsigned char)source->data[i] == 0xe2 && (unsigned char)source->data[i + 1] == 0x80 &&
               (unsigned char)source->data[i + 2] == closer))
        ++i;
      if (i + 2 < source->length)
        i += 3;
      kind = opener == 0x98 ? SEQUEL_TOKEN_STRING : SEQUEL_TOKEN_QUOTED_IDENTIFIER;
      flags = opener == 0x98 ? SEQUEL_TOKEN_FLAG_LITERAL : 0;
      if (i == source->length && (unsigned char)source->data[i - 1] != closer)
        flags |= SEQUEL_TOKEN_FLAG_UNTERMINATED;
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
      kind = quote == '\'' || (quote == '"' && settings->dialect == SEQUEL_MYSQL) ? SEQUEL_TOKEN_STRING
                                                                                  : SEQUEL_TOKEN_QUOTED_IDENTIFIER;
      char closing_quote = quote == '[' ? ']' : quote;
      flags = (kind == SEQUEL_TOKEN_STRING ? SEQUEL_TOKEN_FLAG_LITERAL : 0) |
              ((quote == '"' && settings->dialect == SEQUEL_MYSQL) ? SEQUEL_TOKEN_FLAG_DIALECT_SPECIFIC : 0);
      if (i == source->length && !quoted_token_is_terminated(source->data, start, i, source->length, closing_quote))
        flags |= SEQUEL_TOKEN_FLAG_UNTERMINATED;
    } else if (isdigit((unsigned char)source->data[i]) ||
               (source->data[i] == '.' && i + 1 < source->length && isdigit((unsigned char)source->data[i + 1]))) {
      ++i;
      if (source->data[start] == '0' && i < source->length && (source->data[i] == 'x' || source->data[i] == 'X'))
        ++i;
      while (i < source->length && (isalnum((unsigned char)source->data[i]) || source->data[i] == '.' ||
                                    source->data[i] == '_' || source->data[i] == '+' || source->data[i] == '-'))
        ++i;
      if (i < source->length && (source->data[i] == 'e' || source->data[i] == 'E')) {
        size_t exponent = i++;
        if (i < source->length && (source->data[i] == '+' || source->data[i] == '-'))
          ++i;
        size_t exponent_digits = i;
        while (i < source->length && (isdigit((unsigned char)source->data[i]) || source->data[i] == '_'))
          ++i;
        if (i == exponent_digits)
          i = exponent;
      }
      kind = SEQUEL_TOKEN_NUMBER;
      flags = SEQUEL_TOKEN_FLAG_LITERAL;
    } else if (source->data[i] == '$' &&
               (settings->dialect == SEQUEL_GENERIC || settings->dialect == SEQUEL_POSTGRESQL ||
                settings->dialect == SEQUEL_DUCKDB || settings->dialect == SEQUEL_SQLITE)) {
      size_t tag_end = i + 1;
      while (tag_end < source->length &&
             (isalnum((unsigned char)source->data[tag_end]) || source->data[tag_end] == '_'))
        ++tag_end;
      if (tag_end < source->length && source->data[tag_end] == '$') {
        size_t content = tag_end + 1;
        size_t close = content;
        while (close + (tag_end - i + 1) <= source->length &&
               memcmp(source->data + close, source->data + i, tag_end - i + 1) != 0)
          ++close;
        int terminated = close + (tag_end - start + 1) <= source->length;
        i = terminated ? close + (tag_end - start + 1) : source->length;
        kind = SEQUEL_TOKEN_STRING;
        flags = SEQUEL_TOKEN_FLAG_LITERAL | SEQUEL_TOKEN_FLAG_DIALECT_SPECIFIC |
                (terminated ? 0 : SEQUEL_TOKEN_FLAG_UNTERMINATED);
      } else {
        ++i;
        while (i < source->length && is_word_part((unsigned char)source->data[i]))
          ++i;
        if (settings->dialect == SEQUEL_SQLITE && source->data[start] == '$') {
          if (memchr(source->data + start, '(', source->length - start)) {
            while (i < source->length && source->data[i] != ')' && source->data[i] != ';')
              ++i;
            if (i < source->length && source->data[i] == ')')
              ++i;
            else
              flags |= SEQUEL_TOKEN_FLAG_UNTERMINATED | SEQUEL_TOKEN_FLAG_DIALECT_SPECIFIC;
          }
        }
        kind = SEQUEL_TOKEN_PARAMETER;
        flags |= SEQUEL_TOKEN_FLAG_BIND;
      }
    } else if (source->data[i] == '{' && settings->dialect == SEQUEL_CLICKHOUSE) {
      ++i;
      while (i < source->length && source->data[i] != '}')
        ++i;
      int terminated = i < source->length;
      if (terminated)
        ++i;
      kind = SEQUEL_TOKEN_PARAMETER;
      flags = SEQUEL_TOKEN_FLAG_BIND | SEQUEL_TOKEN_FLAG_DIALECT_SPECIFIC |
              (terminated ? 0 : SEQUEL_TOKEN_FLAG_UNTERMINATED);
      if (terminated && !memchr(source->data + start, ':', i - start))
        flags |= SEQUEL_TOKEN_FLAG_RECOVERED;
    } else if (source->data[i] == '%' && settings->dialect == SEQUEL_MYSQL && i + 1 < source->length &&
               (source->data[i + 1] == 's' || source->data[i + 1] == '(')) {
      i += 1;
      if (source->data[i] == 's') {
        ++i;
      } else {
        ++i;
        size_t name_start = i;
        while (i < source->length && source->data[i] != ')')
          ++i;
        if (i < source->length && i + 1 < source->length && source->data[i + 1] == 's') {
          ++i;
          ++i;
        } else {
          i = source->length;
          flags |= SEQUEL_TOKEN_FLAG_UNTERMINATED;
        }
        if (i == name_start)
          flags |= SEQUEL_TOKEN_FLAG_RECOVERED;
      }
      kind = SEQUEL_TOKEN_PARAMETER;
      flags |= SEQUEL_TOKEN_FLAG_BIND | SEQUEL_TOKEN_FLAG_DIALECT_SPECIFIC;
    } else if (source->data[i] == ':' && i + 1 < source->length && source->data[i + 1] == ':') {
      i += 2;
      kind = SEQUEL_TOKEN_OPERATOR;
    } else if ((source->data[i] == '?' && (settings->dialect == SEQUEL_SQLITE || settings->dialect == SEQUEL_MYSQL)) ||
               source->data[i] == '$' ||
               (source->data[i] == ':' && i > 0 && source->data[i - 1] != ':' && i + 1 < source->length &&
                (is_word_start((unsigned char)source->data[i + 1]) ||
                 (settings->dialect == SEQUEL_SQLITE && isdigit((unsigned char)source->data[i + 1])))) ||
               (source->data[i] == '@' &&
                (i + 1 >= source->length || is_word_part((unsigned char)source->data[i + 1]) ||
                 (settings->dialect == SEQUEL_MYSQL && source->data[i + 1] == '@')))) {
      ++i;
      if (source->data[start] == '@' && i < source->length && source->data[i] == '@')
        ++i;
      while (i < source->length && is_word_part((unsigned char)source->data[i]))
        ++i;
      kind = SEQUEL_TOKEN_PARAMETER;
      flags = SEQUEL_TOKEN_FLAG_BIND;
      if (settings->dialect == SEQUEL_SQLITE && source->data[start] == '$') {
        while (i < source->length && !isspace((unsigned char)source->data[i]) && source->data[i] != ';')
          ++i;
        if (memchr(source->data + start, '(', i - start) && !memchr(source->data + start, ')', i - start))
          flags |= SEQUEL_TOKEN_FLAG_UNTERMINATED;
      }
    } else if (source->data[i] == '%' && i + 1 < source->length &&
               (source->data[i + 1] == 's' || source->data[i + 1] == '(')) {
      i += 2;
      if (source->data[start + 1] == '(') {
        while (i < source->length && source->data[i] != ')')
          ++i;
        if (i < source->length)
          ++i;
        if (i < source->length && source->data[i] == 's')
          ++i;
      }
      kind = SEQUEL_TOKEN_PARAMETER;
      flags = SEQUEL_TOKEN_FLAG_BIND | SEQUEL_TOKEN_FLAG_DIALECT_SPECIFIC;
    } else if (source->data[i] == '?') {
      ++i;
      while (i < source->length && is_operator_char((unsigned char)source->data[i]))
        ++i;
      kind = SEQUEL_TOKEN_OPERATOR;
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
  sequel_source_free(document->source_object);
  free(document->source);
  free(document);
}

sequel_source_view_t sequel_lexed_source(const sequel_lexed_document_t *document) {
  return document ? (sequel_source_view_t){document->source, document->length} : (sequel_source_view_t){0};
}

const sequel_source_t *sequel_lexed_source_object(const sequel_lexed_document_t *document) {
  return document ? document->source_object : NULL;
}

const sequel_token_t *sequel_lexed_tokens(const sequel_lexed_document_t *document, size_t *count) {
  if (count)
    *count = document ? document->token_count : 0;
  return document ? document->tokens : NULL;
}

size_t sequel_lexed_token_count(const sequel_lexed_document_t *document) {
  return document ? document->token_count : 0;
}

sequel_dialect_t sequel_lexed_dialect(const sequel_lexed_document_t *document) {
  return document ? document->dialect : SEQUEL_GENERIC;
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
