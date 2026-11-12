#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <sequel/formatter.h>

#include "format_buffer.h"
#include "format_rules.h"
#include "tokens.h"

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

static sequel_status_t sequel_formatter_copy_source(sequel_source_view_t source, sequel_format_mode_t mode,
                                                    sequel_text_result_t *out) {
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

static const char *sequel_formatter_token_text(sequel_source_view_t source, const sequel_token_t *token,
                                               size_t *length) {
  *length = token->range.end - token->range.start;
  return source.data + token->range.start;
}

static int sequel_formatter_is_by_keyword(const sequel_source_view_t source, const sequel_token_t *token) {
  size_t length = 0;
  const char *text = sequel_formatter_token_text(source, token, &length);
  return length == 2 && toupper((unsigned char)text[0]) == 'B' && toupper((unsigned char)text[1]) == 'Y';
}

static int sequel_formatter_is_join_modifier(const sequel_source_view_t source, const sequel_token_t *token) {
  size_t length = 0;
  const char *text = sequel_formatter_token_text(source, token, &length);
  if (length == 4)
    return (toupper((unsigned char)text[0]) == 'L' && toupper((unsigned char)text[1]) == 'E') ||
           (toupper((unsigned char)text[0]) == 'R' && toupper((unsigned char)text[1]) == 'I') ||
           (toupper((unsigned char)text[0]) == 'F' && toupper((unsigned char)text[1]) == 'U') ||
           (toupper((unsigned char)text[0]) == 'C' && toupper((unsigned char)text[1]) == 'R');
  return (length == 5 && toupper((unsigned char)text[0]) == 'O' && toupper((unsigned char)text[1]) == 'U') ||
         (length == 5 && toupper((unsigned char)text[0]) == 'I' && toupper((unsigned char)text[1]) == 'N');
}

static int sequel_formatter_is_join_lead(const sequel_source_view_t source, const sequel_token_t *token) {
  size_t length = 0;
  const char *text = sequel_formatter_token_text(source, token, &length);
  return (sequel_formatter_is_join_modifier(source, token) && length == 4) ||
         (length == 5 && toupper((unsigned char)text[0]) == 'I' && toupper((unsigned char)text[1]) == 'N');
}

static int sequel_formatter_token_is_word(sequel_source_view_t source, const sequel_token_t *token, const char *word) {
  size_t length = 0;
  const char *text = sequel_formatter_token_text(source, token, &length);
  size_t expected = strlen(word);
  if (length != expected)
    return 0;
  for (size_t i = 0; i < length; ++i)
    if (toupper((unsigned char)text[i]) != toupper((unsigned char)word[i]))
      return 0;
  return 1;
}

static int sequel_formatter_is_upsert_on(const sequel_source_view_t source, const sequel_token_t *tokens, size_t index,
                                         size_t count) {
  size_t next = index + 1;
  while (next < count && tokens[next].kind == SEQUEL_TOKEN_WHITESPACE)
    ++next;
  return next < count && sequel_formatter_token_is_word(source, &tokens[next], "DUPLICATE");
}

static int sequel_formatter_has_case_before_matching_close(sequel_source_view_t source, const sequel_token_t *tokens,
                                                           size_t start, size_t count) {
  size_t depth = 0;
  for (size_t i = start + 1; i < count; ++i) {
    const sequel_token_t *token = &tokens[i];
    if (token->kind == SEQUEL_TOKEN_KEYWORD && token->keyword_id == SEQUEL_KEYWORD_CASE)
      return 1;
    if (!sequel_token_is_single_punctuation(token))
      continue;
    char symbol = source.data[token->range.start];
    if (symbol == '(')
      ++depth;
    else if (symbol == ')') {
      if (depth == 0)
        return 0;
      --depth;
    }
  }
  return 0;
}

static int sequel_formatter_no_space_before_punctuation(const char *text, size_t length, int punctuation) {
  return punctuation && length == 1 &&
         (text[0] == '(' || text[0] == '[' || text[0] == '{' || text[0] == '.' || text[0] == ',' || text[0] == ':' ||
          text[0] == ')' || text[0] == ';' || text[0] == ']' || text[0] == '}');
}

static int sequel_formatter_no_space_after_punctuation(sequel_token_kind_t previous, const char *data, size_t length) {
  return previous == SEQUEL_TOKEN_PUNCTUATION && length &&
         (data[length - 1] == '(' || data[length - 1] == '[' || data[length - 1] == '{' || data[length - 1] == '.' ||
          data[length - 1] == ':');
}

static int sequel_formatter_compact_json_operator(const sequel_token_t *token, sequel_source_view_t source,
                                                  const char *data, size_t length) {
  size_t text_length = 0;
  const char *text = sequel_formatter_token_text(source, token, &text_length);
  return token->kind == SEQUEL_TOKEN_OPERATOR &&
         ((text_length && (text[0] == '#' || text[0] == '?' || text[0] == '@')) || (length && data[length - 1] == '#'));
}

static int sequel_formatter_is_as_before_open_paren(const char *data, size_t length, const char *text,
                                                    size_t text_length) {
  return text_length == 1 && text[0] == '(' && length >= 2 && data[length - 2] == 'A' && data[length - 1] == 'S';
}

static int sequel_formatter_colon_before_literal_needs_space(sequel_token_kind_t previous, const char *data,
                                                             size_t length, sequel_token_kind_t current,
                                                             size_t bracket_depth, size_t brace_depth) {
  return (previous == SEQUEL_TOKEN_PUNCTUATION || previous == SEQUEL_TOKEN_OPERATOR) && length &&
         data[length - 1] == ':' && (bracket_depth == 0 || brace_depth > 0) &&
         (current == SEQUEL_TOKEN_NUMBER || current == SEQUEL_TOKEN_STRING || current == SEQUEL_TOKEN_BOOLEAN ||
          current == SEQUEL_TOKEN_NULL);
}

static int sequel_formatter_is_line_comment(const sequel_token_t *token) {
  return token->kind == SEQUEL_TOKEN_LINE_COMMENT;
}

static int sequel_formatter_is_block_comment(const sequel_token_t *token) {
  return token->kind == SEQUEL_TOKEN_BLOCK_COMMENT;
}

static int sequel_formatter_is_open_paren(const sequel_source_view_t source, const sequel_token_t *token) {
  return sequel_token_is_single_punctuation(token) && source.data[token->range.start] == '(';
}

static int sequel_formatter_is_close_paren(const sequel_source_view_t source, const sequel_token_t *token) {
  return sequel_token_is_single_punctuation(token) && source.data[token->range.start] == ')';
}

static int sequel_formatter_is_comma(const sequel_source_view_t source, const sequel_token_t *token) {
  return sequel_token_is_single_punctuation(token) && source.data[token->range.start] == ',';
}

static int sequel_formatter_is_semicolon(const sequel_source_view_t source, const sequel_token_t *token) {
  return sequel_token_is_single_punctuation(token) && source.data[token->range.start] == ';';
}

static int sequel_formatter_is_cast_operator(const char *text, size_t length) {
  return length == 2 && text[0] == ':' && text[1] == ':';
}

static int sequel_formatter_is_range_colon(const char *text, size_t length) { return length == 1 && text[0] == ':'; }

static int sequel_formatter_is_dot(const char *text, size_t length) { return length == 1 && text[0] == '.'; }

static int sequel_formatter_is_open_brace(const char *text, size_t length) { return length == 1 && text[0] == '{'; }

static int sequel_formatter_is_close_brace(const char *text, size_t length) { return length == 1 && text[0] == '}'; }

static int sequel_formatter_is_newline_terminated(const char *data, size_t length) {
  return length && data[length - 1] == '\n';
}

static int sequel_formatter_is_space_terminated(const char *data, size_t length) {
  return length && data[length - 1] == ' ';
}

static int sequel_formatter_is_clause_header(uint32_t keyword) {
  return keyword == SEQUEL_KEYWORD_WHERE || keyword == SEQUEL_KEYWORD_HAVING || keyword == SEQUEL_KEYWORD_SET ||
         keyword == SEQUEL_KEYWORD_VALUES || keyword == SEQUEL_KEYWORD_RETURNING || keyword == SEQUEL_KEYWORD_GROUP ||
         keyword == SEQUEL_KEYWORD_ORDER || keyword == SEQUEL_KEYWORD_QUALIFY;
}

static void sequel_formatter_update_parenthesis_depth(const sequel_source_view_t source, const sequel_token_t *token,
                                                      size_t *depth) {
  if (!sequel_token_is_single_punctuation(token))
    return;
  char symbol = source.data[token->range.start];
  if (symbol == '(')
    ++*depth;
  else if (symbol == ')' && *depth)
    --*depth;
}

static int sequel_formatter_append_comment(char **data, size_t *length, size_t *capacity, sequel_source_view_t source,
                                           const sequel_token_t *token) {
  bool inline_line_comment = *length && ((*data)[*length - 1] == ';' ||
                                         (*length > 1 && (*data)[*length - 1] == '\n' && (*data)[*length - 2] == ';'));
  if (inline_line_comment && *length && (*data)[*length - 1] == '\n')
    (*data)[--*length] = 0;
  if ((token->kind == SEQUEL_TOKEN_LINE_COMMENT && !inline_line_comment
           ? (*length && (*data)[*length - 1] != '\n' && !sequel_format_append_text(data, length, capacity, "\n", 1))
           : !sequel_format_append_space(data, length, capacity)) ||
      !sequel_format_append_text(data, length, capacity, source.data + token->range.start,
                                 token->range.end - token->range.start) ||
      (token->kind == SEQUEL_TOKEN_LINE_COMMENT && !sequel_format_append_text(data, length, capacity, "\n", 1)))
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
  size_t bracket_depth = 0;
  size_t brace_depth = 0;
  size_t query_paren_depth = 0;
  size_t cte_paren_depth = 0;
  bool nested_query = false;
  bool distinct_on_group = false;
  bool between_pending = false;
  bool projection_subquery = false;
  bool window_paren = false;
  bool within_group_paren = false;
  bool cte_just_closed = false;
  size_t case_function_count = 0;
  size_t case_depth = 0;
  bool list_clause_context_active = false;
  bool pending_clause_body = false;
  for (size_t i = 0; i < token_count; ++i) {
    const sequel_token_t *token = &tokens[i];
    if (token->kind == SEQUEL_TOKEN_EOF)
      break;
    if (!sequel_token_has_range(token, source.length))
      goto invalid;
    if (token->kind == SEQUEL_TOKEN_WHITESPACE)
      continue;
    bool follows_closed_cte = cte_just_closed && !sequel_formatter_is_comma(source, token);
    if (follows_closed_cte)
      cte_just_closed = false;
    if ((token->kind == SEQUEL_TOKEN_LINE_COMMENT || token->kind == SEQUEL_TOKEN_BLOCK_COMMENT) &&
        !settings->preserve_comments)
      continue;
    if (sequel_token_kind_is_comment(token->kind)) {
      if (!sequel_formatter_append_comment(&data, &length, &capacity, source, token))
        goto no_memory;
      previous = token->kind;
      continue;
    }
    bool keyword = sequel_token_is_keyword(token);
    bool join_modifier = sequel_formatter_is_join_lead(source, token);
    bool upsert_on = sequel_formatter_token_is_word(source, token, "ON") &&
                     sequel_formatter_is_upsert_on(source, tokens, i, token_count);
    bool window_boundary = window_paren && (sequel_formatter_token_is_word(source, token, "PARTITION") ||
                                            sequel_formatter_token_is_word(source, token, "ORDER"));
    bool values_function = sequel_formatter_token_is_word(source, token, "VALUES") && previous_token &&
                           previous_token->kind == SEQUEL_TOKEN_OPERATOR &&
                           previous_token->range.end > previous_token->range.start &&
                           source.data[previous_token->range.start] == '=';
    bool clause_start =
        sequel_token_is_clause_keyword(token) &&
        !(token->keyword_id == SEQUEL_KEYWORD_JOIN && previous_token &&
          sequel_formatter_is_join_modifier(source, previous_token)) &&
        !(token->keyword_id == SEQUEL_KEYWORD_ON && previous_token &&
          sequel_formatter_token_is_word(source, previous_token, "DISTINCT")) &&
        !(token->keyword_id == SEQUEL_KEYWORD_GROUP && previous_token &&
          sequel_formatter_token_is_word(source, previous_token, "WITHIN")) &&
        !(token->keyword_id == SEQUEL_KEYWORD_ORDER && within_group_paren) && !upsert_on && !values_function &&
        !(token->keyword_id == SEQUEL_KEYWORD_VALUES && paren_depth > 0) &&
        !(window_paren && (token->keyword_id == SEQUEL_KEYWORD_ORDER || token->keyword_id == SEQUEL_KEYWORD_GROUP));
    bool condition_operator =
        sequel_format_is_condition_operator(token, paren_depth, cte_paren_depth) && !between_pending;
    if (keyword && token->keyword_id == SEQUEL_KEYWORD_BETWEEN)
      between_pending = true;
    else if (keyword && token->keyword_id == SEQUEL_KEYWORD_AND && between_pending)
      between_pending = false;
    bool select_start = keyword && token->keyword_id == SEQUEL_KEYWORD_SELECT;
    if (select_start && i + 1 < token_count) {
      size_t next = i + 1;
      while (next < token_count && tokens[next].kind == SEQUEL_TOKEN_WHITESPACE)
        ++next;
      distinct_on_group = next < token_count && sequel_formatter_token_is_word(source, &tokens[next], "DISTINCT");
    }
    bool nested_case_start = keyword && token->keyword_id == SEQUEL_KEYWORD_CASE && previous_token &&
                             previous_token->kind == SEQUEL_TOKEN_KEYWORD &&
                             previous_token->keyword_id == SEQUEL_KEYWORD_THEN;
    bool function_case_start = keyword && token->keyword_id == SEQUEL_KEYWORD_CASE && previous_token &&
                               sequel_token_is_single_punctuation(previous_token) &&
                               source.data[previous_token->range.start] == '(' && paren_depth > 0;
    if (select_start && paren_depth)
      query_paren_depth = paren_depth;
    if (select_start && paren_depth)
      nested_query = true;
    bool ends_case_branch = keyword && sequel_keyword_ends_case_branch(token->keyword_id);
    bool grouped_condition = sequel_formatter_is_open_paren(source, token) && previous_token &&
                             (sequel_formatter_token_is_word(source, previous_token, "AND") ||
                              sequel_formatter_token_is_word(source, previous_token, "OR") ||
                              sequel_formatter_token_is_word(source, previous_token, "NOT"));
    if (pending_clause_body && !upsert_on && !sequel_formatter_is_by_keyword(source, token) && !grouped_condition) {
      if (!sequel_format_append_line_start(&data, &length, &capacity, paren_depth + 1 + (projection_subquery ? 1 : 0)))
        goto no_memory;
      pending_clause_body = false;
    }
    if (select_start || clause_start || upsert_on || window_boundary || condition_operator || ends_case_branch ||
        nested_case_start || function_case_start || join_modifier) {
      size_t indent = (select_start || clause_start || upsert_on) ? paren_depth : paren_depth + 1;
      if (window_boundary)
        indent = paren_depth + 1;
      if (select_start && projection_subquery)
        indent = paren_depth + 1;
      else if (projection_subquery)
        ++indent;
      if (token->keyword_id == SEQUEL_KEYWORD_ON && !upsert_on)
        indent = paren_depth + 1;
      if (join_modifier)
        indent = paren_depth;
      if (nested_case_start)
        indent = paren_depth + case_depth * 2 + 1;
      else if (function_case_start)
        indent = paren_depth + 1;
      else if (token->keyword_id == SEQUEL_KEYWORD_WHEN || token->keyword_id == SEQUEL_KEYWORD_ELSE)
        indent = paren_depth + case_depth * 2;
      else if (token->keyword_id == SEQUEL_KEYWORD_END && case_depth)
        indent = paren_depth + case_depth * 2 - 1;
      if (!sequel_format_append_line_start(&data, &length, &capacity, indent))
        goto no_memory;
      if ((select_start && !distinct_on_group) ||
          (sequel_formatter_is_clause_header(token->keyword_id) && !upsert_on && !window_boundary))
        pending_clause_body = true;
    }
    if (sequel_token_is_single_punctuation(token)) {
      if (sequel_formatter_is_open_paren(source, token) && previous_token &&
          sequel_formatter_is_comma(source, previous_token) && clause != SEQUEL_KEYWORD_VALUES)
        projection_subquery = true;
      if (sequel_formatter_is_open_paren(source, token) && previous_token &&
          sequel_formatter_token_is_word(source, previous_token, "AS"))
        cte_paren_depth = paren_depth + 1;
      sequel_formatter_update_parenthesis_depth(source, token, &paren_depth);
      char symbol = source.data[token->range.start];
      if (symbol == '(' && previous_token && sequel_formatter_token_is_word(source, previous_token, "GROUP")) {
        size_t before_group = i - 1;
        while (before_group > 0 && tokens[before_group].kind == SEQUEL_TOKEN_WHITESPACE)
          --before_group;
        if (sequel_formatter_token_is_word(source, &tokens[before_group], "GROUP")) {
          while (before_group > 0 && tokens[before_group - 1].kind == SEQUEL_TOKEN_WHITESPACE)
            --before_group;
          if (before_group > 0 && sequel_formatter_token_is_word(source, &tokens[before_group - 1], "WITHIN"))
            within_group_paren = true;
        }
      } else if (symbol == ')' && within_group_paren)
        within_group_paren = false;
      if (symbol == '[')
        ++bracket_depth;
      else if (symbol == ']' && bracket_depth)
        --bracket_depth;
      else if (symbol == '{')
        ++brace_depth;
      else if (symbol == '}' && brace_depth)
        --brace_depth;
      if (sequel_formatter_is_close_paren(source, token) && length && data[length - 1] != '\n' &&
          (query_paren_depth == paren_depth + 1 || (nested_query && paren_depth == 0) ||
           (projection_subquery && paren_depth == 0)) &&
          !sequel_format_append_line_start(&data, &length, &capacity,
                                           projection_subquery ? 1 : (paren_depth ? paren_depth - 1 : 0)))
        goto no_memory;
      if (sequel_formatter_is_close_paren(source, token) && query_paren_depth == paren_depth + 1)
        query_paren_depth = 0;
      if (sequel_formatter_is_close_paren(source, token) && cte_paren_depth == paren_depth + 1)
        cte_paren_depth = 0, cte_just_closed = true, pending_clause_body = false;
      if (sequel_formatter_is_close_paren(source, token) && nested_query && paren_depth == 0)
        nested_query = false;
      if (sequel_formatter_is_close_paren(source, token) && projection_subquery && paren_depth == 0)
        projection_subquery = false;
      if (sequel_formatter_is_close_paren(source, token) && window_paren && paren_depth == 0) {
        if (length && data[length - 1] != '\n' && !sequel_format_append_line_start(&data, &length, &capacity, 1))
          goto no_memory;
        window_paren = false;
      }
      if (sequel_formatter_is_close_paren(source, token) && case_function_count) {
        if (length && data[length - 1] != '\n' &&
            !sequel_format_append_line_start(&data, &length, &capacity, paren_depth + 1))
          goto no_memory;
        --case_function_count;
      }
      if (sequel_formatter_is_close_paren(source, token) && distinct_on_group && paren_depth == 0)
        pending_clause_body = true;
    }
    bool punctuation = token->kind == SEQUEL_TOKEN_PUNCTUATION;
    size_t text_length = 0;
    const char *text = sequel_formatter_token_text(source, token, &text_length);
    if (sequel_format_is_literal_replacement(token, settings)) {
      text = "?";
      text_length = 1;
    }
    bool no_space_before = sequel_formatter_no_space_before_punctuation(text, text_length, punctuation);
    if (text_length == 1 && text[0] == ':')
      no_space_before = true;
    if (text_length && (text[0] == '>' || text[0] == '<' || text[0] == '=' || text[0] == '+' || text[0] == '-') &&
        length && data[length - 1] != ' ' && data[length - 1] != '\n' &&
        !sequel_format_append_space(&data, &length, &capacity))
      goto no_memory;
    if (text_length == 1 && text[0] == '(' && previous == SEQUEL_TOKEN_KEYWORD &&
        !(previous_token && sequel_formatter_token_is_word(source, previous_token, "VALUES")))
      no_space_before = false;
    if (sequel_formatter_is_as_before_open_paren(data, length, text, text_length))
      no_space_before = false;
    bool cast_operator = sequel_token_is_operator(token) && sequel_formatter_is_cast_operator(text, text_length);
    bool range_colon = sequel_token_is_operator(token) && sequel_formatter_is_range_colon(text, text_length);
    bool after_cast_operator = previous_token && previous_token->kind == SEQUEL_TOKEN_OPERATOR &&
                               previous_token->range.end == previous_token->range.start + 2 &&
                               source.data[previous_token->range.start] == ':' &&
                               source.data[previous_token->range.start + 1] == ':';
    bool after_range_colon =
        previous_token && previous_token->kind == SEQUEL_TOKEN_OPERATOR &&
        previous_token->range.end == previous_token->range.start + 1 &&
        source.data[previous_token->range.start] == ':' &&
        !sequel_formatter_colon_before_literal_needs_space(previous, data, length, token->kind, bracket_depth,
                                                           brace_depth);
    bool no_space_after_previous =
        sequel_formatter_no_space_after_punctuation(previous, data, length) &&
        !sequel_formatter_colon_before_literal_needs_space(previous, data, length, token->kind, bracket_depth,
                                                           brace_depth);
    if (previous_token && text_length && text[0] == '@') {
      size_t previous_length = 0;
      const char *previous_text = sequel_formatter_token_text(source, previous_token, &previous_length);
      if (previous_length && previous_text[previous_length - 1] == '@')
        no_space_after_previous = true;
    }
    if (previous_token &&
        (previous_token->kind == SEQUEL_TOKEN_PUNCTUATION || previous_token->kind == SEQUEL_TOKEN_OPERATOR) &&
        previous_token->range.end == previous_token->range.start + 1 &&
        source.data[previous_token->range.start] == ':' && previous_token->range.end < source.length &&
        isspace((unsigned char)source.data[previous_token->range.end]) &&
        (token->kind == SEQUEL_TOKEN_NUMBER || token->kind == SEQUEL_TOKEN_STRING ||
         token->kind == SEQUEL_TOKEN_BOOLEAN || token->kind == SEQUEL_TOKEN_NULL)) {
      no_space_after_previous = false;
    }
    if (previous_token && sequel_token_is_operator(previous_token) &&
        previous_token->range.end == previous_token->range.start + 1 && source.data[previous_token->range.start] == '#')
      no_space_after_previous = true;
    if (previous_token && sequel_token_is_operator(previous_token) && token->kind == SEQUEL_TOKEN_NUMBER &&
        previous_token->range.end == previous_token->range.start + 1 && source.data[previous_token->range.start] == '-')
      no_space_after_previous = true;
    if (previous_token && token->kind == SEQUEL_TOKEN_OPERATOR && previous_token->range.end == token->range.start &&
        (sequel_token_is_operator(previous_token) || sequel_token_is_single_punctuation(previous_token)))
      no_space_after_previous = true;
    bool question_after_equals = previous_token && token->kind == SEQUEL_TOKEN_OPERATOR && text_length == 1 &&
                                 text[0] == '?' && previous_token->kind == SEQUEL_TOKEN_OPERATOR &&
                                 previous_token->range.end == previous_token->range.start + 1 &&
                                 source.data[previous_token->range.start] == '=';
    bool adjacent_string_newline =
        previous_token && previous_token->kind == SEQUEL_TOKEN_STRING && token->kind == SEQUEL_TOKEN_STRING &&
        memchr(source.data + previous_token->range.end, '\n', token->range.start - previous_token->range.end);
    if (sequel_token_is_operator(token) && !range_colon && !no_space_after_previous &&
        !sequel_formatter_compact_json_operator(token, source, data, length) && !cast_operator && !range_colon &&
        length && data[length - 1] != ' ' && data[length - 1] != '\n' &&
        !sequel_format_append_space(&data, &length, &capacity))
      goto no_memory;
    if (!cast_operator && !range_colon && !after_cast_operator && !after_range_colon && !no_space_before &&
        (!no_space_after_previous || question_after_equals) &&
        (sequel_token_kind_is_word(previous) || sequel_token_kind_is_word(token->kind) || question_after_equals) &&
        !(follows_closed_cte && previous == SEQUEL_TOKEN_PUNCTUATION && data[length - 1] == ','))
      if (adjacent_string_newline ? !sequel_format_append_text(&data, &length, &capacity, "\n", 1)
                                  : !sequel_format_append_space(&data, &length, &capacity))
        goto no_memory;
    if (!sequel_format_append_text(&data, &length, &capacity, text, text_length))
      goto no_memory;
    if (punctuation && text_length == 1 && text[0] == '(' && !grouped_condition &&
        sequel_formatter_has_case_before_matching_close(source, tokens, i, token_count)) {
      ++case_function_count;
      if (!sequel_format_append_line_start(&data, &length, &capacity, paren_depth + 1))
        goto no_memory;
    }
    if (punctuation && text_length == 1 && text[0] == '(' && previous_token &&
        sequel_formatter_token_is_word(source, previous_token, "OVER")) {
      window_paren = true;
      if (!sequel_format_append_line_start(&data, &length, &capacity, paren_depth + 1))
        goto no_memory;
    }
    if (sequel_formatter_is_comma(source, token) && bracket_depth == 0 &&
        (case_function_count || paren_depth == 0 || paren_depth == query_paren_depth) &&
        (list_clause_context_active || (length >= 2 && data[length - 2] == ')'))) {
      size_t indent = (cte_just_closed || (projection_subquery && length >= 2 && data[length - 2] == ')' &&
                                           clause != SEQUEL_KEYWORD_VALUES && !case_function_count))
                          ? 0
                          : paren_depth + 1;
      if (!sequel_format_append_line_start(&data, &length, &capacity, indent))
        goto no_memory;
    }
    if (sequel_formatter_is_semicolon(source, token) && !sequel_format_append_text(&data, &length, &capacity, "\n", 1))
      goto no_memory;
    previous = token->kind;
    previous_token = token;
    if (select_start || clause_start)
      clause = token->keyword_id;
    if (token->keyword_id == SEQUEL_KEYWORD_CASE)
      ++case_depth;
    else if (token->keyword_id == SEQUEL_KEYWORD_END && case_depth)
      --case_depth;
    list_clause_context_active = sequel_keyword_uses_comma_separated_list(clause);
  }
  if (!data) {
    data = malloc(1);
    if (!data)
      goto no_memory;
    data[0] = 0;
    capacity = 1;
  }
  if (source.length && source.data[source.length - 1] != '\n' && length && data[length - 1] == '\n')
    data[--length] = 0;
  if (sequel_lexed_dialect(lexed) == SEQUEL_DUCKDB && strstr(source.data, "':")) {
    const char *source_key = source.data;
    char *formatted_key = data;
    while ((source_key = strstr(source_key, "':")) != NULL && (formatted_key = strstr(formatted_key, "':")) != NULL) {
      if (source_key[2] != ' ' && source_key[2] != '\t' && source_key[2] != '\n' && formatted_key[2] == ' ') {
        memmove(formatted_key + 2, formatted_key + 3, length - (size_t)(formatted_key - data) - 2);
        --length;
      } else {
        formatted_key += 2;
      }
      source_key += 2;
    }
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
  return sequel_formatter_copy_source(
      sequel_lexed_source(lexed), settings->mode == SEQUEL_FORMAT_SYNTAX_AWARE ? SEQUEL_FORMAT_LEXICAL : settings->mode,
      out);
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
  return sequel_formatter_copy_source(sequel_lexed_source(sequel_syntax_lexed(syntax)),
                                      issues ? SEQUEL_FORMAT_RECOVERED : settings->mode, out);
}
