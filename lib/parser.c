#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <sequel/parser.h>

#include "model.h"

static sequel_syntax_kind_t sequel_parser_statement_kind(const sequel_lexed_document_t *lexed, size_t token_index) {
  size_t count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &count);
  sequel_source_view_t source = sequel_lexed_source(lexed);
  while (token_index < count && tokens[token_index].kind == SEQUEL_TOKEN_PUNCTUATION && tokens[token_index].range.end == tokens[token_index].range.start + 1 &&
         source.data[tokens[token_index].range.start] == '(')
    ++token_index;
  if (token_index >= count)
    return SEQUEL_SYNTAX_UNKNOWN_STATEMENT;
  const sequel_token_t *token = &tokens[token_index];
  if (token->kind != SEQUEL_TOKEN_KEYWORD)
    return SEQUEL_SYNTAX_UNKNOWN_STATEMENT;
  switch (token->keyword_id) {
  case SEQUEL_KEYWORD_SELECT:
    return SEQUEL_SYNTAX_SELECT_STATEMENT;
  case SEQUEL_KEYWORD_INSERT:
    return SEQUEL_SYNTAX_INSERT_STATEMENT;
  case SEQUEL_KEYWORD_UPDATE:
    return SEQUEL_SYNTAX_UPDATE_STATEMENT;
  case SEQUEL_KEYWORD_DELETE:
    return SEQUEL_SYNTAX_DELETE_STATEMENT;
  case SEQUEL_KEYWORD_EXPLAIN:
    return SEQUEL_SYNTAX_EXPLAIN_STATEMENT;
  case SEQUEL_KEYWORD_CREATE:
    return SEQUEL_SYNTAX_CREATE_STATEMENT;
  case SEQUEL_KEYWORD_ALTER:
    return SEQUEL_SYNTAX_ALTER_STATEMENT;
  case SEQUEL_KEYWORD_DROP:
    return SEQUEL_SYNTAX_DROP_STATEMENT;
  case SEQUEL_KEYWORD_BEGIN:
  case SEQUEL_KEYWORD_COMMIT:
  case SEQUEL_KEYWORD_ROLLBACK:
    return SEQUEL_SYNTAX_TRANSACTION_STATEMENT;
  case SEQUEL_KEYWORD_MERGE:
    return SEQUEL_SYNTAX_MERGE_STATEMENT;
  default:
    return SEQUEL_SYNTAX_UNKNOWN_STATEMENT;
  }
}

static int sequel_parser_append_node(sequel_syntax_document_t *document, sequel_syntax_node_t node) {
  size_t count = document->node_count + 1;
  sequel_syntax_node_t *nodes = realloc(document->nodes, count * sizeof(*nodes));
  if (!nodes)
    return 0;
  document->nodes = nodes;
  document->nodes[document->node_count++] = node;
  return 1;
}

static sequel_syntax_kind_t sequel_parser_clause_kind(const sequel_token_t *token) {
  if (token->kind != SEQUEL_TOKEN_KEYWORD)
    return SEQUEL_SYNTAX_ERROR;
  switch (token->keyword_id) {
  case SEQUEL_KEYWORD_SELECT:
    return SEQUEL_SYNTAX_SELECT_LIST;
  case SEQUEL_KEYWORD_FROM:
    return SEQUEL_SYNTAX_FROM_CLAUSE;
  case SEQUEL_KEYWORD_WHERE:
    return SEQUEL_SYNTAX_WHERE_CLAUSE;
  case SEQUEL_KEYWORD_PREWHERE:
    return SEQUEL_SYNTAX_WHERE_CLAUSE;
  case SEQUEL_KEYWORD_GROUP:
    return SEQUEL_SYNTAX_GROUP_BY_CLAUSE;
  case SEQUEL_KEYWORD_ORDER:
    return SEQUEL_SYNTAX_ORDER_BY_CLAUSE;
  case SEQUEL_KEYWORD_LIMIT:
    return SEQUEL_SYNTAX_LIMIT_CLAUSE;
  case SEQUEL_KEYWORD_OFFSET:
    return SEQUEL_SYNTAX_OFFSET_CLAUSE;
  case SEQUEL_KEYWORD_HAVING:
    return SEQUEL_SYNTAX_HAVING_CLAUSE;
  case SEQUEL_KEYWORD_RETURNING:
    return SEQUEL_SYNTAX_RETURNING_CLAUSE;
  case SEQUEL_KEYWORD_VALUES:
    return SEQUEL_SYNTAX_VALUES_CLAUSE;
  case SEQUEL_KEYWORD_SET:
    return SEQUEL_SYNTAX_SET_CLAUSE;
  case SEQUEL_KEYWORD_INTO:
    return SEQUEL_SYNTAX_INTO_CLAUSE;
  case SEQUEL_KEYWORD_USING:
    return SEQUEL_SYNTAX_USING_CLAUSE;
  case SEQUEL_KEYWORD_WINDOW:
    return SEQUEL_SYNTAX_WINDOW_CLAUSE;
  case SEQUEL_KEYWORD_QUALIFY:
    return SEQUEL_SYNTAX_QUALIFY_CLAUSE;
  default:
    return SEQUEL_SYNTAX_ERROR;
  }
}

static int sequel_parser_append_child(sequel_syntax_document_t *document, size_t parent, size_t child) {
  size_t count = document->child_count + 1;
  size_t *children = realloc(document->children, count * sizeof(*children));
  if (!children)
    return 0;
  document->children = children;
  document->children[document->child_count++] = child;
  if (!document->nodes[parent].child_count)
    document->nodes[parent].first_child = count - 1;
  ++document->nodes[parent].child_count;
  return 1;
}

static int sequel_parser_append_issue(sequel_syntax_document_t *document, sequel_parse_issue_t issue) {
  if (document->max_diagnostics && document->issue_count >= document->max_diagnostics)
    return 1;
  size_t count = document->issue_count + 1;
  sequel_parse_issue_t *issues = realloc(document->issues, count * sizeof(*issues));
  if (!issues)
    return 0;
  document->issues = issues;
  document->issues[document->issue_count++] = issue;
  return 1;
}

static int sequel_parser_expression_precedence(char operator) {
  switch (operator) {
  case '|':
    return 1;
  case '&':
    return 2;
  case '=':
  case '<':
  case '>':
  case '!':
    return 3;
  case '+':
  case '-':
    return 4;
  case '*':
  case '/':
  case '%':
    return 5;
  default:
    return 0;
  }
}

static int sequel_parser_expression_keyword_precedence(const sequel_token_t *token) {
  if (token->kind != SEQUEL_TOKEN_KEYWORD)
    return 0;
  switch (token->keyword_id) {
  case SEQUEL_KEYWORD_OR:
    return 1;
  case SEQUEL_KEYWORD_AND:
    return 2;
  case SEQUEL_KEYWORD_IS:
  case SEQUEL_KEYWORD_IN:
  case SEQUEL_KEYWORD_BETWEEN:
  case SEQUEL_KEYWORD_LIKE:
    return 3;
  default:
    return 0;
  }
}

static sequel_syntax_kind_t sequel_parser_expression_atom_kind(const sequel_token_t *token) {
  if (token->kind == SEQUEL_TOKEN_PARAMETER)
    return SEQUEL_SYNTAX_PARAMETER;
  if (token->kind == SEQUEL_TOKEN_IDENTIFIER || token->kind == SEQUEL_TOKEN_QUOTED_IDENTIFIER || token->kind == SEQUEL_TOKEN_KEYWORD)
    return SEQUEL_SYNTAX_IDENTIFIER;
  if (token->kind == SEQUEL_TOKEN_STRING || token->kind == SEQUEL_TOKEN_NUMBER || token->kind == SEQUEL_TOKEN_BOOLEAN || token->kind == SEQUEL_TOKEN_NULL)
    return SEQUEL_SYNTAX_LITERAL;
  return SEQUEL_SYNTAX_ERROR;
}

static size_t sequel_parser_next_expression_token(const sequel_token_t *tokens, size_t index, size_t end) {
  while (index < end && (tokens[index].kind == SEQUEL_TOKEN_WHITESPACE || tokens[index].kind == SEQUEL_TOKEN_LINE_COMMENT || tokens[index].kind == SEQUEL_TOKEN_BLOCK_COMMENT))
    ++index;
  return index;
}

static int sequel_parser_append_expression(sequel_syntax_document_t *document, const sequel_token_t *tokens, const sequel_source_view_t *source, size_t start, size_t end, size_t *out_index) {
  while (start < end && (tokens[start].kind == SEQUEL_TOKEN_WHITESPACE || tokens[start].kind == SEQUEL_TOKEN_LINE_COMMENT || tokens[start].kind == SEQUEL_TOKEN_BLOCK_COMMENT))
    ++start;
  while (end > start && (tokens[end - 1].kind == SEQUEL_TOKEN_WHITESPACE || tokens[end - 1].kind == SEQUEL_TOKEN_LINE_COMMENT || tokens[end - 1].kind == SEQUEL_TOKEN_BLOCK_COMMENT))
    --end;
  if (start >= end)
    return 0;
  if (tokens[start].kind == SEQUEL_TOKEN_KEYWORD && tokens[start].keyword_id == SEQUEL_KEYWORD_CASE) {
    sequel_syntax_node_t node = {.kind = SEQUEL_SYNTAX_CASE,
                                 .range = {tokens[start].range.start, tokens[end - 1].range.end},
                                 .header_range = tokens[start].range,
                                 .first_token = start,
                                 .token_count = end - start,
                                 .production_id = SEQUEL_SYNTAX_CASE};
    if (!sequel_parser_append_node(document, node))
      return -1;
    size_t index = document->node_count - 1;
    for (size_t i = start + 1; i < end; ++i) {
      if (tokens[i].kind != SEQUEL_TOKEN_KEYWORD || (tokens[i].keyword_id != SEQUEL_KEYWORD_CAST && tokens[i].keyword_id != SEQUEL_KEYWORD_EXISTS && tokens[i].keyword_id != SEQUEL_KEYWORD_IN))
        continue;
      size_t opening = sequel_parser_next_expression_token(tokens, i + 1, end);
      if (opening >= end || tokens[opening].kind != SEQUEL_TOKEN_PUNCTUATION || source->data[tokens[opening].range.start] != '(')
        continue;
      size_t depth = 0, closing = end;
      for (size_t j = opening; j < end; ++j) {
        if (tokens[j].kind != SEQUEL_TOKEN_PUNCTUATION)
          continue;
        char punctuation = source->data[tokens[j].range.start];
        if (punctuation == '(')
          ++depth;
        else if (punctuation == ')' && depth && --depth == 0) {
          closing = j;
          break;
        }
      }
      if (closing == end)
        continue;
      size_t child = 0;
      if (tokens[i].keyword_id == SEQUEL_KEYWORD_IN) {
        sequel_syntax_node_t child_node = {.kind = SEQUEL_SYNTAX_IN_LIST,
                                           .range = {tokens[i - 1].range.start, tokens[closing].range.end},
                                           .header_range = tokens[i].range,
                                           .first_token = i - 1,
                                           .token_count = closing + 1 - (i - 1),
                                           .production_id = SEQUEL_SYNTAX_IN_LIST};
        if (!sequel_parser_append_node(document, child_node))
          return -1;
        child = document->node_count - 1;
      } else if (sequel_parser_append_expression(document, tokens, source, i, closing + 1, &child) < 0)
        return -1;
      if (!sequel_parser_append_child(document, index, child))
        return -1;
    }
    *out_index = index;
    return 1;
  }
  if (tokens[start].kind == SEQUEL_TOKEN_KEYWORD && (tokens[start].keyword_id == SEQUEL_KEYWORD_EXISTS || tokens[start].keyword_id == SEQUEL_KEYWORD_CAST)) {
    sequel_syntax_kind_t kind = tokens[start].keyword_id == SEQUEL_KEYWORD_EXISTS ? SEQUEL_SYNTAX_EXISTS : SEQUEL_SYNTAX_CAST;
    sequel_syntax_node_t node = {.kind = kind,
                                 .range = {tokens[start].range.start, tokens[end - 1].range.end},
                                 .header_range = tokens[start].range,
                                 .first_token = start,
                                 .token_count = end - start,
                                 .production_id = (uint32_t)kind};
    if (!sequel_parser_append_node(document, node))
      return -1;
    size_t index = document->node_count - 1;
    size_t inner = sequel_parser_next_expression_token(tokens, start + 1, end);
    if (inner < end) {
      size_t child = 0;
      if (sequel_parser_append_expression(document, tokens, source, inner, end, &child) < 0 || !sequel_parser_append_child(document, index, child))
        return -1;
    }
    *out_index = index;
    return 1;
  }
  size_t opening = sequel_parser_next_expression_token(tokens, start + 1, end);
  if (opening < end && (tokens[start].kind == SEQUEL_TOKEN_IDENTIFIER || tokens[start].kind == SEQUEL_TOKEN_QUOTED_IDENTIFIER) && tokens[opening].kind == SEQUEL_TOKEN_PUNCTUATION &&
      source->data[tokens[opening].range.start] == '(') {
    size_t closing = end;
    size_t group_depth = 0;
    for (size_t i = opening; i < end; ++i) {
      if (tokens[i].kind != SEQUEL_TOKEN_PUNCTUATION)
        continue;
      char punctuation = source->data[tokens[i].range.start];
      if (punctuation == '(')
        ++group_depth;
      else if (punctuation == ')' && group_depth && --group_depth == 0) {
        closing = i;
        break;
      }
    }
    if (closing < end) {
      size_t over = sequel_parser_next_expression_token(tokens, closing + 1, end);
      if (over < end && tokens[over].kind == SEQUEL_TOKEN_KEYWORD && tokens[over].keyword_id == SEQUEL_KEYWORD_OVER) {
        sequel_syntax_node_t node = {.kind = SEQUEL_SYNTAX_WINDOW_EXPRESSION,
                                     .range = {tokens[start].range.start, tokens[end - 1].range.end},
                                     .header_range = tokens[over].range,
                                     .first_token = start,
                                     .token_count = end - start,
                                     .production_id = SEQUEL_SYNTAX_WINDOW_EXPRESSION};
        if (!sequel_parser_append_node(document, node))
          return -1;
        size_t index = document->node_count - 1;
        size_t function_node = 0;
        if (sequel_parser_append_expression(document, tokens, source, start, closing + 1, &function_node) < 0 || !sequel_parser_append_child(document, index, function_node))
          return -1;
        size_t frame = sequel_parser_next_expression_token(tokens, over + 1, end);
        if (frame < end) {
          size_t frame_node = 0;
          if (sequel_parser_append_expression(document, tokens, source, frame, end, &frame_node) < 0 || !sequel_parser_append_child(document, index, frame_node))
            return -1;
        }
        *out_index = index;
        return 1;
      }
    }
    if (closing < end && sequel_parser_next_expression_token(tokens, closing + 1, end) == end) {
      sequel_syntax_kind_t call_kind = SEQUEL_SYNTAX_FUNCTION_CALL;
      sequel_syntax_node_t node = {.kind = call_kind,
                                   .range = {tokens[start].range.start, tokens[closing].range.end},
                                   .header_range = tokens[start].range,
                                   .first_token = start,
                                   .token_count = closing + 1 - start,
                                   .production_id = (uint32_t)call_kind};
      if (!sequel_parser_append_node(document, node))
        return -1;
      size_t index = document->node_count - 1;
      size_t argument = sequel_parser_next_expression_token(tokens, opening + 1, closing);
      if (argument < closing) {
        size_t argument_node = 0;
        if (sequel_parser_append_expression(document, tokens, source, argument, closing, &argument_node) < 0 || !sequel_parser_append_child(document, index, argument_node))
          return -1;
      }
      *out_index = index;
      return 1;
    }
  }
  if (tokens[start].kind == SEQUEL_TOKEN_PUNCTUATION && source->data[tokens[start].range.start] == '(') {
    size_t closing = end;
    size_t group_depth = 0;
    for (size_t i = start; i < end; ++i) {
      if (tokens[i].kind != SEQUEL_TOKEN_PUNCTUATION)
        continue;
      char punctuation = source->data[tokens[i].range.start];
      if (punctuation == '(')
        ++group_depth;
      else if (punctuation == ')' && group_depth && --group_depth == 0) {
        closing = i;
        break;
      }
    }
    if (closing < end && sequel_parser_next_expression_token(tokens, closing + 1, end) == end) {
      sequel_syntax_node_t node = {.kind = SEQUEL_SYNTAX_GROUP,
                                   .range = {tokens[start].range.start, tokens[closing].range.end},
                                   .header_range = tokens[start].range,
                                   .first_token = start,
                                   .token_count = closing + 1 - start,
                                   .production_id = SEQUEL_SYNTAX_GROUP};
      if (!sequel_parser_append_node(document, node))
        return -1;
      size_t index = document->node_count - 1;
      size_t inner = sequel_parser_next_expression_token(tokens, start + 1, closing);
      if (inner < closing) {
        size_t inner_node = 0;
        if (sequel_parser_append_expression(document, tokens, source, inner, closing, &inner_node) < 0 || !sequel_parser_append_child(document, index, inner_node))
          return -1;
      }
      *out_index = index;
      return 1;
    }
  }
  size_t depth = 0, split = SIZE_MAX;
  int precedence = INT_MAX;
  for (size_t i = start; i < end; ++i) {
    if (tokens[i].kind != SEQUEL_TOKEN_PUNCTUATION && tokens[i].kind != SEQUEL_TOKEN_OPERATOR && tokens[i].kind != SEQUEL_TOKEN_KEYWORD)
      continue;
    char c = source->data[tokens[i].range.start];
    if (c == '(') {
      ++depth;
      continue;
    }
    if (c == ')' && depth) {
      --depth;
      continue;
    }
    if (!depth && i != start && (tokens[i].kind == SEQUEL_TOKEN_OPERATOR || tokens[i].kind == SEQUEL_TOKEN_KEYWORD)) {
      int candidate = tokens[i].kind == SEQUEL_TOKEN_KEYWORD ? sequel_parser_expression_keyword_precedence(&tokens[i]) : sequel_parser_expression_precedence(c);
      if (candidate && candidate <= precedence) {
        precedence = candidate;
        split = i;
      }
    }
  }
  sequel_syntax_kind_t kind = split != SIZE_MAX ? SEQUEL_SYNTAX_BINARY_EXPRESSION : sequel_parser_expression_atom_kind(&tokens[start]);
  if (split != SIZE_MAX && tokens[split].kind == SEQUEL_TOKEN_KEYWORD) {
    switch (tokens[split].keyword_id) {
    case SEQUEL_KEYWORD_IN:
      kind = SEQUEL_SYNTAX_IN_LIST;
      break;
    case SEQUEL_KEYWORD_BETWEEN:
      kind = SEQUEL_SYNTAX_BETWEEN;
      break;
    case SEQUEL_KEYWORD_IS:
      kind = SEQUEL_SYNTAX_IS_NULL;
      break;
    default:
      break;
    }
  }
  bool unary = split == SIZE_MAX && (tokens[start].kind == SEQUEL_TOKEN_OPERATOR || (tokens[start].kind == SEQUEL_TOKEN_KEYWORD && tokens[start].keyword_id == SEQUEL_KEYWORD_NOT));
  if (unary)
    kind = SEQUEL_SYNTAX_UNARY_EXPRESSION;
  sequel_syntax_node_t node = {.kind = kind,
                               .range = {tokens[start].range.start, tokens[end - 1].range.end},
                               .header_range = tokens[start].range,
                               .first_token = start,
                               .token_count = end - start,
                               .production_id = (uint32_t)kind};
  if (!sequel_parser_append_node(document, node))
    return -1;
  size_t index = document->node_count - 1;
  if (split != SIZE_MAX) {
    size_t left = 0, right = 0;
    if (sequel_parser_append_expression(document, tokens, source, start, split, &left) < 0 || sequel_parser_append_expression(document, tokens, source, split + 1, end, &right) < 0 ||
        !sequel_parser_append_child(document, index, left) || !sequel_parser_append_child(document, index, right))
      return -1;
  } else if (unary) {
    size_t operand = sequel_parser_next_expression_token(tokens, start + 1, end);
    size_t child = 0;
    if (operand < end && (sequel_parser_append_expression(document, tokens, source, operand, end, &child) < 0 || !sequel_parser_append_child(document, index, child)))
      return -1;
  }
  *out_index = index;
  return 1;
}

sequel_status_t sequel_parse(const sequel_lexed_document_t *lexed, const sequel_parse_options_t *options, const sequel_cancellation_t *cancellation, sequel_syntax_document_t **out) {
  if (!lexed || !out)
    return SEQUEL_ERR_NULL;
  *out = NULL;
  sequel_syntax_document_t *document = calloc(1, sizeof(*document));
  if (!document)
    return SEQUEL_ERR_NOMEM;
  document->lexed = lexed;
  document->max_diagnostics = options && options->max_diagnostics ? options->max_diagnostics : 64;
  document->max_nodes = options && options->max_nodes ? options->max_nodes : SIZE_MAX;
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  size_t start_token = 0;
  size_t delimiter_stack[64];
  size_t delimiter_depth = 0;
  size_t nesting_limit = options && options->max_nesting ? options->max_nesting : 64;
  if (nesting_limit > sizeof(delimiter_stack) / sizeof(delimiter_stack[0]))
    nesting_limit = sizeof(delimiter_stack) / sizeof(delimiter_stack[0]);
  for (size_t i = 0; i <= token_count; ++i) {
    if (cancellation && cancellation->is_cancelled && cancellation->is_cancelled(cancellation->userdata)) {
      sequel_syntax_document_free(document);
      return SEQUEL_ERR_CANCELLED;
    }
    bool boundary = i == token_count || tokens[i].kind == SEQUEL_TOKEN_EOF ||
                    (tokens[i].kind == SEQUEL_TOKEN_PUNCTUATION && tokens[i].range.end == tokens[i].range.start + 1 && sequel_lexed_source(lexed).data[tokens[i].range.start] == ';');
    if (i < token_count && tokens[i].kind == SEQUEL_TOKEN_PUNCTUATION) {
      sequel_source_view_t source = sequel_lexed_source(lexed);
      char punctuation = source.data[tokens[i].range.start];
      if (punctuation == '(' || punctuation == '[') {
        if (delimiter_depth >= nesting_limit) {
          if (!sequel_parser_append_issue(document, (sequel_parse_issue_t){SEQUEL_PARSE_ISSUE_UNEXPECTED_TOKEN, SEQUEL_RECOVERY_OPAQUE_REGION, tokens[i].range, SIZE_MAX})) {
            sequel_syntax_document_free(document);
            return SEQUEL_ERR_NOMEM;
          }
        } else {
          delimiter_stack[delimiter_depth++] = i;
        }
      } else if (punctuation == ')' || punctuation == ']') {
        if (!delimiter_depth) {
          if (!sequel_parser_append_issue(document, (sequel_parse_issue_t){SEQUEL_PARSE_ISSUE_UNMATCHED_CLOSE, SEQUEL_RECOVERY_SKIPPED_TOKEN, tokens[i].range, SIZE_MAX})) {
            sequel_syntax_document_free(document);
            return SEQUEL_ERR_NOMEM;
          }
        } else {
          size_t opening = delimiter_stack[delimiter_depth - 1];
          char expected = punctuation == ')' ? '(' : '[';
          if (source.data[tokens[opening].range.start] != expected) {
            if (!sequel_parser_append_issue(document, (sequel_parse_issue_t){SEQUEL_PARSE_ISSUE_UNEXPECTED_TOKEN, SEQUEL_RECOVERY_SKIPPED_TOKEN, tokens[i].range, SIZE_MAX})) {
              sequel_syntax_document_free(document);
              return SEQUEL_ERR_NOMEM;
            }
          } else {
            --delimiter_depth;
          }
        }
      }
    }
    if (i < token_count && (tokens[i].flags & SEQUEL_TOKEN_FLAG_UNTERMINATED)) {
      if (!sequel_parser_append_issue(document, (sequel_parse_issue_t){SEQUEL_PARSE_ISSUE_UNTERMINATED_TOKEN, SEQUEL_RECOVERY_OPAQUE_REGION, tokens[i].range, SIZE_MAX})) {
        sequel_syntax_document_free(document);
        return SEQUEL_ERR_NOMEM;
      }
    }
    if (i < token_count && (tokens[i].flags & SEQUEL_TOKEN_FLAG_RECOVERED)) {
      if (!sequel_parser_append_issue(document, (sequel_parse_issue_t){SEQUEL_PARSE_ISSUE_UNEXPECTED_TOKEN, SEQUEL_RECOVERY_SKIPPED_TOKEN, tokens[i].range, SIZE_MAX})) {
        sequel_syntax_document_free(document);
        return SEQUEL_ERR_NOMEM;
      }
    }
    if (!boundary)
      continue;
    size_t first = start_token;
    while (first < i && (tokens[first].kind == SEQUEL_TOKEN_WHITESPACE || tokens[first].kind == SEQUEL_TOKEN_LINE_COMMENT || tokens[first].kind == SEQUEL_TOKEN_BLOCK_COMMENT))
      ++first;
    size_t last = i;
    while (last > first && (tokens[last - 1].kind == SEQUEL_TOKEN_WHITESPACE || tokens[last - 1].kind == SEQUEL_TOKEN_LINE_COMMENT || tokens[last - 1].kind == SEQUEL_TOKEN_BLOCK_COMMENT))
      --last;
    if (first < last) {
      sequel_source_view_t source = sequel_lexed_source(lexed);
      sequel_syntax_kind_t statement_kind_value = sequel_parser_statement_kind(lexed, first);
      sequel_syntax_node_t node = {.kind = statement_kind_value,
                                   .range = {tokens[first].range.start, tokens[last - 1].range.end},
                                   .header_range = {tokens[first].range.start, tokens[first].range.end},
                                   .first_token = first,
                                   .token_count = last - first,
                                   .production_id = (uint32_t)statement_kind_value};
      (void)source;
      if (!sequel_parser_append_node(document, node)) {
        sequel_syntax_document_free(document);
        return SEQUEL_ERR_NOMEM;
      }
      size_t parent = document->node_count - 1;
      for (size_t token_index = first; token_index < last; ++token_index) {
        sequel_syntax_kind_t child_kind = sequel_parser_clause_kind(&tokens[token_index]);
        if (child_kind == SEQUEL_SYNTAX_ERROR)
          continue;
        size_t child_end = last;
        for (size_t next = token_index + 1; next < last; ++next) {
          if (sequel_parser_clause_kind(&tokens[next]) != SEQUEL_SYNTAX_ERROR) {
            child_end = next;
            break;
          }
        }
        sequel_syntax_node_t child = {.kind = child_kind,
                                      .range = {tokens[token_index].range.start, tokens[child_end - 1].range.end},
                                      .header_range = tokens[token_index].range,
                                      .first_token = token_index,
                                      .token_count = child_end - token_index,
                                      .production_id = (uint32_t)child_kind};
        if (!sequel_parser_append_node(document, child)) {
          sequel_syntax_document_free(document);
          return SEQUEL_ERR_NOMEM;
        }
        size_t child_index = document->node_count - 1;
        if (!sequel_parser_append_child(document, parent, child_index)) {
          sequel_syntax_document_free(document);
          return SEQUEL_ERR_NOMEM;
        }
        size_t expression = 0;
        if (sequel_parser_append_expression(document, tokens, &source, token_index + 1, child_end, &expression) < 0 || (expression && !sequel_parser_append_child(document, child_index, expression))) {
          sequel_syntax_document_free(document);
          return SEQUEL_ERR_NOMEM;
        }
      }
      for (size_t candidate = first; candidate + 1 < last; ++candidate) {
        if (tokens[candidate].kind != SEQUEL_TOKEN_PUNCTUATION || source.data[tokens[candidate].range.start] != '(')
          continue;
        size_t inner = candidate + 1;
        while (inner < last && (tokens[inner].kind == SEQUEL_TOKEN_WHITESPACE || tokens[inner].kind == SEQUEL_TOKEN_LINE_COMMENT || tokens[inner].kind == SEQUEL_TOKEN_BLOCK_COMMENT))
          ++inner;
        if (inner >= last || tokens[inner].kind != SEQUEL_TOKEN_KEYWORD || (tokens[inner].keyword_id != SEQUEL_KEYWORD_SELECT && tokens[inner].keyword_id != SEQUEL_KEYWORD_WITH))
          continue;
        size_t depth = 0, closing = last;
        for (size_t scan = candidate; scan < last; ++scan) {
          if (tokens[scan].kind != SEQUEL_TOKEN_PUNCTUATION)
            continue;
          char punctuation = source.data[tokens[scan].range.start];
          if (punctuation == '(')
            ++depth;
          else if (punctuation == ')' && depth && --depth == 0) {
            closing = scan;
            break;
          }
        }
        if (closing == last)
          continue;
        sequel_syntax_node_t subquery = {.kind = SEQUEL_SYNTAX_SUBQUERY,
                                         .range = {tokens[candidate].range.start, tokens[closing].range.end},
                                         .header_range = tokens[inner].range,
                                         .first_token = candidate,
                                         .token_count = closing + 1 - candidate,
                                         .production_id = SEQUEL_SYNTAX_SUBQUERY};
        if (!sequel_parser_append_node(document, subquery) || !sequel_parser_append_child(document, parent, document->node_count - 1)) {
          sequel_syntax_document_free(document);
          return SEQUEL_ERR_NOMEM;
        }
      }
    }
    if (i == token_count || tokens[i].kind == SEQUEL_TOKEN_EOF)
      break;
    start_token = i + (i < token_count && tokens[i].kind != SEQUEL_TOKEN_EOF ? 1 : 0);
  }
  if (delimiter_depth) {
    sequel_source_view_t source = sequel_lexed_source(lexed);
    size_t opening = delimiter_stack[delimiter_depth - 1];
    if (!sequel_parser_append_issue(document, (sequel_parse_issue_t){SEQUEL_PARSE_ISSUE_UNCLOSED_GROUP, SEQUEL_RECOVERY_INSERTED_TOKEN, {tokens[opening].range.start, source.length}, SIZE_MAX})) {
      sequel_syntax_document_free(document);
      return SEQUEL_ERR_NOMEM;
    }
  }
  if (document->node_count > document->max_nodes) {
    sequel_syntax_document_free(document);
    return SEQUEL_ERR_LIMIT;
  }
  *out = document;
  return SEQUEL_OK;
}
