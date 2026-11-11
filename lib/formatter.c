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

static int append_line_start(char **data, size_t *length, size_t *capacity, size_t indent) {
  while (*length && (*data)[*length - 1] == ' ')
    --*length;
  if (*length && (*data)[*length - 1] != '\n' && !append_text(data, length, capacity, "\n", 1))
    return 0;
  for (size_t i = 0; i < indent; ++i)
    if (!append_text(data, length, capacity, "  ", 2))
      return 0;
  return 1;
}

static int is_clause(uint32_t keyword) {
  return keyword == SEQUEL_KEYWORD_FROM || keyword == SEQUEL_KEYWORD_WHERE || keyword == SEQUEL_KEYWORD_GROUP ||
         keyword == SEQUEL_KEYWORD_HAVING || keyword == SEQUEL_KEYWORD_ORDER || keyword == SEQUEL_KEYWORD_LIMIT ||
         keyword == SEQUEL_KEYWORD_OFFSET || keyword == SEQUEL_KEYWORD_RETURNING || keyword == SEQUEL_KEYWORD_VALUES ||
         keyword == SEQUEL_KEYWORD_SET || keyword == SEQUEL_KEYWORD_UNION;
}

static int is_list_clause(uint32_t keyword) {
  return keyword == SEQUEL_KEYWORD_SELECT || keyword == SEQUEL_KEYWORD_GROUP || keyword == SEQUEL_KEYWORD_ORDER ||
         keyword == SEQUEL_KEYWORD_SET || keyword == SEQUEL_KEYWORD_VALUES || keyword == SEQUEL_KEYWORD_RETURNING;
}

static int is_case_boundary(uint32_t keyword) {
  return keyword == SEQUEL_KEYWORD_WHEN || keyword == SEQUEL_KEYWORD_ELSE || keyword == SEQUEL_KEYWORD_END;
}

static int is_word_token(sequel_token_kind_t kind) {
  return kind == SEQUEL_TOKEN_IDENTIFIER || kind == SEQUEL_TOKEN_KEYWORD || kind == SEQUEL_TOKEN_BOOLEAN ||
         kind == SEQUEL_TOKEN_NULL || kind == SEQUEL_TOKEN_NUMBER || kind == SEQUEL_TOKEN_STRING ||
         kind == SEQUEL_TOKEN_QUOTED_IDENTIFIER || kind == SEQUEL_TOKEN_PARAMETER;
}

static int is_comment_token(sequel_token_kind_t kind) {
  return kind == SEQUEL_TOKEN_LINE_COMMENT || kind == SEQUEL_TOKEN_BLOCK_COMMENT;
}

static int is_single_char_punctuation(const sequel_token_t *token) {
  return token->kind == SEQUEL_TOKEN_PUNCTUATION && token->range.end == token->range.start + 1;
}

static const char *token_text(sequel_source_view_t source, const sequel_token_t *token, size_t *length) {
  *length = token->range.end - token->range.start;
  return source.data + token->range.start;
}

static int is_by_keyword(const sequel_source_view_t source, const sequel_token_t *token) {
  size_t length = 0;
  const char *text = token_text(source, token, &length);
  return token->kind == SEQUEL_TOKEN_KEYWORD && length == 2 && text[0] == 'B' && text[1] == 'Y';
}

static int is_condition_operator(const sequel_token_t *token, size_t paren_depth) {
  return token->kind == SEQUEL_TOKEN_KEYWORD && paren_depth == 0 &&
         (token->keyword_id == SEQUEL_KEYWORD_AND || token->keyword_id == SEQUEL_KEYWORD_OR);
}

static int is_select_start(const sequel_token_t *token, size_t paren_depth) {
  return token->kind == SEQUEL_TOKEN_KEYWORD && token->keyword_id == SEQUEL_KEYWORD_SELECT && paren_depth == 0;
}

static int is_literal_replacement(const sequel_token_t *token, const sequel_format_options_t *settings) {
  return is_literal_token(token) && !is_bind_token(token) && !settings->preserve_literals;
}

static int no_space_before_punctuation(const char *text, size_t length, int punctuation) {
  return punctuation && length == 1 &&
         (text[0] == '(' || text[0] == '.' || text[0] == ',' || text[0] == ':' || text[0] == ')' || text[0] == ';');
}

static int no_space_after_punctuation(sequel_token_kind_t previous, const char *data, size_t length) {
  return previous == SEQUEL_TOKEN_PUNCTUATION && length &&
         (data[length - 1] == '(' || data[length - 1] == '.' || data[length - 1] == ':');
}

static int colon_before_literal_needs_space(sequel_token_kind_t previous, const char *data, size_t length,
                                            sequel_token_kind_t current) {
  return previous == SEQUEL_TOKEN_PUNCTUATION && length && data[length - 1] == ':' &&
         (current == SEQUEL_TOKEN_NUMBER || current == SEQUEL_TOKEN_STRING || current == SEQUEL_TOKEN_BOOLEAN ||
          current == SEQUEL_TOKEN_NULL);
}

static int is_line_comment(const sequel_token_t *token) { return token->kind == SEQUEL_TOKEN_LINE_COMMENT; }

static int is_block_comment(const sequel_token_t *token) { return token->kind == SEQUEL_TOKEN_BLOCK_COMMENT; }

static int is_keyword_token(const sequel_token_t *token) { return token->kind == SEQUEL_TOKEN_KEYWORD; }

static int is_operator_token(const sequel_token_t *token) { return token->kind == SEQUEL_TOKEN_OPERATOR; }

static int is_bind_token(const sequel_token_t *token) { return (token->flags & SEQUEL_TOKEN_FLAG_BIND) != 0; }

static int is_literal_token(const sequel_token_t *token) { return (token->flags & SEQUEL_TOKEN_FLAG_LITERAL) != 0; }

static int is_eof_token(const sequel_token_t *token) { return token->kind == SEQUEL_TOKEN_EOF; }

static int has_valid_range(const sequel_token_t *token, size_t source_length) {
  return token->range.start <= token->range.end && token->range.end <= source_length;
}

static int is_open_paren(const sequel_source_view_t source, const sequel_token_t *token) {
  return is_single_char_punctuation(token) && source.data[token->range.start] == '(';
}

static int is_close_paren(const sequel_source_view_t source, const sequel_token_t *token) {
  return is_single_char_punctuation(token) && source.data[token->range.start] == ')';
}

static int is_comma(const sequel_source_view_t source, const sequel_token_t *token) {
  return is_single_char_punctuation(token) && source.data[token->range.start] == ',';
}

static int is_semicolon(const sequel_source_view_t source, const sequel_token_t *token) {
  return is_single_char_punctuation(token) && source.data[token->range.start] == ';';
}

static int is_cast_operator(const char *text, size_t length) { return length == 2 && text[0] == ':' && text[1] == ':'; }

static int is_range_colon(const char *text, size_t length) { return length == 1 && text[0] == ':'; }

static int is_dot(const char *text, size_t length) { return length == 1 && text[0] == '.'; }

static int is_open_brace(const char *text, size_t length) { return length == 1 && text[0] == '{'; }

static int is_close_brace(const char *text, size_t length) { return length == 1 && text[0] == '}'; }

static int is_newline_terminated(const char *data, size_t length) { return length && data[length - 1] == '\n'; }

static int is_space_terminated(const char *data, size_t length) { return length && data[length - 1] == ' '; }

static int is_clause_header(uint32_t keyword) {
  return keyword == SEQUEL_KEYWORD_WHERE || keyword == SEQUEL_KEYWORD_HAVING || keyword == SEQUEL_KEYWORD_SET ||
         keyword == SEQUEL_KEYWORD_VALUES || keyword == SEQUEL_KEYWORD_RETURNING;
}

static void update_parenthesis_depth(const sequel_source_view_t source, const sequel_token_t *token, size_t *depth) {
  if (!is_single_char_punctuation(token))
    return;
  char symbol = source.data[token->range.start];
  if (symbol == '(')
    ++*depth;
  else if (symbol == ')' && *depth)
    --*depth;
}

static int append_comment(char **data, size_t *length, size_t *capacity, sequel_source_view_t source,
                          const sequel_token_t *token) {
  bool inline_line_comment = *length && ((*data)[*length - 1] == ';' ||
                                         (*length > 1 && (*data)[*length - 1] == '\n' && (*data)[*length - 2] == ';'));
  if (inline_line_comment && *length && (*data)[*length - 1] == '\n')
    (*data)[--*length] = 0;
  if ((token->kind == SEQUEL_TOKEN_LINE_COMMENT && !inline_line_comment
           ? (*length && (*data)[*length - 1] != '\n' && !append_text(data, length, capacity, "\n", 1))
           : !append_space(data, length, capacity)) ||
      !append_text(data, length, capacity, source.data + token->range.start, token->range.end - token->range.start) ||
      (token->kind == SEQUEL_TOKEN_LINE_COMMENT && !append_text(data, length, capacity, "\n", 1)))
    return 0;
  return 1;
}

static sequel_status_t format_lexed(const sequel_lexed_document_t *lexed, const sequel_format_options_t *settings,
                                    sequel_text_result_t *out) {
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  sequel_source_view_t source = sequel_lexed_source(lexed);
  char *data = NULL;
  size_t length = 0, capacity = 0;
  sequel_token_kind_t previous = SEQUEL_TOKEN_EOF;
  const sequel_token_t *previous_token = NULL;
  uint32_t clause = 0;
  size_t paren_depth = 0;
  bool list_clause = false;
  bool pending_clause_body = false;
  for (size_t i = 0; i < token_count; ++i) {
    const sequel_token_t *token = &tokens[i];
    if (is_eof_token(token))
      break;
    if (!has_valid_range(token, source.length))
      goto invalid;
    if (token->kind == SEQUEL_TOKEN_WHITESPACE)
      continue;
    if ((token->kind == SEQUEL_TOKEN_LINE_COMMENT || token->kind == SEQUEL_TOKEN_BLOCK_COMMENT) &&
        !settings->preserve_comments)
      continue;
    if (is_comment_token(token->kind)) {
      if (!append_comment(&data, &length, &capacity, source, token))
        goto no_memory;
      previous = token->kind;
      continue;
    }
    bool keyword = is_keyword_token(token);
    bool clause_start = keyword && is_clause(token->keyword_id) && paren_depth == 0;
    bool condition_operator = is_condition_operator(token, paren_depth);
    bool select_start = is_select_start(token, paren_depth);
    bool case_boundary = keyword && is_case_boundary(token->keyword_id);
    if (pending_clause_body && !is_by_keyword(source, token)) {
      if (!append_line_start(&data, &length, &capacity, 1))
        goto no_memory;
      pending_clause_body = false;
    }
    if (select_start || clause_start || condition_operator || case_boundary) {
      size_t indent = (select_start || clause_start) ? 0 : 1;
      if (is_case_boundary(token->keyword_id))
        indent = 1;
      if (!append_line_start(&data, &length, &capacity, indent))
        goto no_memory;
      if (select_start || is_clause_header(token->keyword_id))
        pending_clause_body = true;
    }
    if (is_single_char_punctuation(token)) {
      update_parenthesis_depth(source, token, &paren_depth);
      char symbol = source.data[token->range.start];
      if (is_comma(source, token) && paren_depth == 0 && list_clause &&
          !append_line_start(&data, &length, &capacity, 1))
        goto no_memory;
    }
    bool punctuation = token->kind == SEQUEL_TOKEN_PUNCTUATION;
    size_t text_length = 0;
    const char *text = token_text(source, token, &text_length);
    if (is_literal_replacement(token, settings)) {
      text = "?";
      text_length = 1;
    }
    bool no_space_before = no_space_before_punctuation(text, text_length, punctuation);
    bool cast_operator = is_operator_token(token) && is_cast_operator(text, text_length);
    bool range_colon = is_operator_token(token) && is_range_colon(text, text_length);
    bool after_cast_operator = previous_token && previous_token->kind == SEQUEL_TOKEN_OPERATOR &&
                               previous_token->range.end == previous_token->range.start + 2 &&
                               source.data[previous_token->range.start] == ':' &&
                               source.data[previous_token->range.start + 1] == ':';
    bool after_range_colon = previous_token && previous_token->kind == SEQUEL_TOKEN_OPERATOR &&
                             previous_token->range.end == previous_token->range.start + 1 &&
                             source.data[previous_token->range.start] == ':';
    bool no_space_after_previous = no_space_after_punctuation(previous, data, length) &&
                                   !colon_before_literal_needs_space(previous, data, length, token->kind);
    if (!cast_operator && !range_colon && !after_cast_operator && !after_range_colon && !no_space_before &&
        !no_space_after_previous &&
        (is_word_token(previous) || is_word_token(token->kind) ||
         (previous == SEQUEL_TOKEN_PUNCTUATION && data[length - 1] == ',')))
      if (!append_space(&data, &length, &capacity))
        goto no_memory;
    if (!append_text(&data, &length, &capacity, text, text_length))
      goto no_memory;
    if (is_semicolon(source, token) && !append_text(&data, &length, &capacity, "\n", 1))
      goto no_memory;
    previous = token->kind;
    previous_token = token;
    if (select_start || clause_start)
      clause = token->keyword_id;
    list_clause = is_list_clause(clause);
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
