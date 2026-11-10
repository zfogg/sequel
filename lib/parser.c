#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <sequel/parser.h>

struct sequel_syntax_document {
  const sequel_lexed_document_t *lexed;
  bool owns_lexed;
  sequel_syntax_node_t *nodes;
  size_t node_count;
  size_t *children;
  size_t child_count;
  sequel_parse_issue_t *issues;
  size_t issue_count;
  size_t max_diagnostics;
  size_t max_nodes;
};

static sequel_syntax_kind_t statement_kind(const sequel_lexed_document_t *lexed, size_t token_index) {
  size_t count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &count);
  if (token_index >= count)
    return SEQUEL_SYNTAX_UNKNOWN_STATEMENT;
  const sequel_token_t *token = &tokens[token_index];
  if (token->kind != SEQUEL_TOKEN_KEYWORD)
    return SEQUEL_SYNTAX_UNKNOWN_STATEMENT;
  switch (token->keyword_id) {
  case 1:
    return SEQUEL_SYNTAX_SELECT_STATEMENT;
  case 2:
    return SEQUEL_SYNTAX_INSERT_STATEMENT;
  case 3:
    return SEQUEL_SYNTAX_UPDATE_STATEMENT;
  case 4:
    return SEQUEL_SYNTAX_DELETE_STATEMENT;
  case 22:
    return SEQUEL_SYNTAX_EXPLAIN_STATEMENT;
  case 23:
    return SEQUEL_SYNTAX_CREATE_STATEMENT;
  case 24:
    return SEQUEL_SYNTAX_ALTER_STATEMENT;
  case 25:
    return SEQUEL_SYNTAX_DROP_STATEMENT;
  case 26:
  case 27:
  case 28:
    return SEQUEL_SYNTAX_TRANSACTION_STATEMENT;
  case 34:
    return SEQUEL_SYNTAX_MERGE_STATEMENT;
  default:
    return SEQUEL_SYNTAX_UNKNOWN_STATEMENT;
  }
}

static int append_node(sequel_syntax_document_t *document, sequel_syntax_node_t node) {
  size_t count = document->node_count + 1;
  sequel_syntax_node_t *nodes = realloc(document->nodes, count * sizeof(*nodes));
  if (!nodes)
    return 0;
  document->nodes = nodes;
  document->nodes[document->node_count++] = node;
  return 1;
}

static sequel_syntax_kind_t clause_kind(const sequel_token_t *token) {
  if (token->kind != SEQUEL_TOKEN_KEYWORD)
    return SEQUEL_SYNTAX_ERROR;
  switch (token->keyword_id) {
  case SEQUEL_KEYWORD_SELECT:
    return SEQUEL_SYNTAX_SELECT_LIST;
  case 6:
    return SEQUEL_SYNTAX_FROM_CLAUSE;
  case 7:
    return SEQUEL_SYNTAX_WHERE_CLAUSE;
  case SEQUEL_KEYWORD_PREWHERE:
    return SEQUEL_SYNTAX_WHERE_CLAUSE;
  case 8:
    return SEQUEL_SYNTAX_GROUP_BY_CLAUSE;
  case 9:
    return SEQUEL_SYNTAX_ORDER_BY_CLAUSE;
  case 11:
    return SEQUEL_SYNTAX_LIMIT_CLAUSE;
  case 12:
    return SEQUEL_SYNTAX_OFFSET_CLAUSE;
  case 16:
    return SEQUEL_SYNTAX_HAVING_CLAUSE;
  case 17:
    return SEQUEL_SYNTAX_RETURNING_CLAUSE;
  case 18:
    return SEQUEL_SYNTAX_VALUES_CLAUSE;
  case 29:
    return SEQUEL_SYNTAX_SET_CLAUSE;
  case 30:
    return SEQUEL_SYNTAX_INTO_CLAUSE;
  case 31:
    return SEQUEL_SYNTAX_USING_CLAUSE;
  case 32:
    return SEQUEL_SYNTAX_WINDOW_CLAUSE;
  case 33:
    return SEQUEL_SYNTAX_QUALIFY_CLAUSE;
  default:
    return SEQUEL_SYNTAX_ERROR;
  }
}

static int append_child(sequel_syntax_document_t *document, size_t parent, size_t child) {
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

static int append_issue(sequel_syntax_document_t *document, sequel_parse_issue_t issue) {
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

static int expression_precedence(char operator) {
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

static int expression_keyword_precedence(const sequel_token_t *token) {
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

static sequel_syntax_kind_t expression_atom_kind(const sequel_token_t *token) {
  if (token->kind == SEQUEL_TOKEN_PARAMETER)
    return SEQUEL_SYNTAX_PARAMETER;
  if (token->kind == SEQUEL_TOKEN_IDENTIFIER || token->kind == SEQUEL_TOKEN_QUOTED_IDENTIFIER ||
      token->kind == SEQUEL_TOKEN_KEYWORD)
    return SEQUEL_SYNTAX_IDENTIFIER;
  if (token->kind == SEQUEL_TOKEN_STRING || token->kind == SEQUEL_TOKEN_NUMBER || token->kind == SEQUEL_TOKEN_BOOLEAN ||
      token->kind == SEQUEL_TOKEN_NULL)
    return SEQUEL_SYNTAX_LITERAL;
  return SEQUEL_SYNTAX_ERROR;
}

static size_t next_expression_token(const sequel_token_t *tokens, size_t index, size_t end) {
  while (index < end &&
         (tokens[index].kind == SEQUEL_TOKEN_WHITESPACE || tokens[index].kind == SEQUEL_TOKEN_LINE_COMMENT ||
          tokens[index].kind == SEQUEL_TOKEN_BLOCK_COMMENT))
    ++index;
  return index;
}

static int append_expression(sequel_syntax_document_t *document, const sequel_token_t *tokens,
                             const sequel_source_view_t *source, size_t start, size_t end, size_t *out_index) {
  while (start < end &&
         (tokens[start].kind == SEQUEL_TOKEN_WHITESPACE || tokens[start].kind == SEQUEL_TOKEN_LINE_COMMENT ||
          tokens[start].kind == SEQUEL_TOKEN_BLOCK_COMMENT))
    ++start;
  while (end > start &&
         (tokens[end - 1].kind == SEQUEL_TOKEN_WHITESPACE || tokens[end - 1].kind == SEQUEL_TOKEN_LINE_COMMENT ||
          tokens[end - 1].kind == SEQUEL_TOKEN_BLOCK_COMMENT))
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
    if (!append_node(document, node))
      return -1;
    size_t index = document->node_count - 1;
    for (size_t i = start + 1; i < end; ++i) {
      if (tokens[i].kind != SEQUEL_TOKEN_KEYWORD ||
          (tokens[i].keyword_id != SEQUEL_KEYWORD_CAST && tokens[i].keyword_id != SEQUEL_KEYWORD_EXISTS &&
           tokens[i].keyword_id != SEQUEL_KEYWORD_IN))
        continue;
      size_t opening = next_expression_token(tokens, i + 1, end);
      if (opening >= end || tokens[opening].kind != SEQUEL_TOKEN_PUNCTUATION ||
          source->data[tokens[opening].range.start] != '(')
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
        if (!append_node(document, child_node))
          return -1;
        child = document->node_count - 1;
      } else if (append_expression(document, tokens, source, i, closing + 1, &child) < 0)
        return -1;
      if (!append_child(document, index, child))
        return -1;
    }
    *out_index = index;
    return 1;
  }
  if (tokens[start].kind == SEQUEL_TOKEN_KEYWORD &&
      (tokens[start].keyword_id == SEQUEL_KEYWORD_EXISTS || tokens[start].keyword_id == SEQUEL_KEYWORD_CAST)) {
    sequel_syntax_kind_t kind =
        tokens[start].keyword_id == SEQUEL_KEYWORD_EXISTS ? SEQUEL_SYNTAX_EXISTS : SEQUEL_SYNTAX_CAST;
    sequel_syntax_node_t node = {.kind = kind,
                                 .range = {tokens[start].range.start, tokens[end - 1].range.end},
                                 .header_range = tokens[start].range,
                                 .first_token = start,
                                 .token_count = end - start,
                                 .production_id = (uint32_t)kind};
    if (!append_node(document, node))
      return -1;
    size_t index = document->node_count - 1;
    size_t inner = next_expression_token(tokens, start + 1, end);
    if (inner < end) {
      size_t child = 0;
      if (append_expression(document, tokens, source, inner, end, &child) < 0 || !append_child(document, index, child))
        return -1;
    }
    *out_index = index;
    return 1;
  }
  size_t opening = next_expression_token(tokens, start + 1, end);
  if (opening < end &&
      (tokens[start].kind == SEQUEL_TOKEN_IDENTIFIER || tokens[start].kind == SEQUEL_TOKEN_QUOTED_IDENTIFIER) &&
      tokens[opening].kind == SEQUEL_TOKEN_PUNCTUATION && source->data[tokens[opening].range.start] == '(') {
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
    if (closing < end && next_expression_token(tokens, closing + 1, end) == end) {
      sequel_syntax_kind_t call_kind = SEQUEL_SYNTAX_FUNCTION_CALL;
      sequel_syntax_node_t node = {.kind = call_kind,
                                   .range = {tokens[start].range.start, tokens[closing].range.end},
                                   .header_range = tokens[start].range,
                                   .first_token = start,
                                   .token_count = closing + 1 - start,
                                   .production_id = (uint32_t)call_kind};
      if (!append_node(document, node))
        return -1;
      size_t index = document->node_count - 1;
      size_t argument = next_expression_token(tokens, opening + 1, closing);
      if (argument < closing) {
        size_t argument_node = 0;
        if (append_expression(document, tokens, source, argument, closing, &argument_node) < 0 ||
            !append_child(document, index, argument_node))
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
    if (closing < end && next_expression_token(tokens, closing + 1, end) == end) {
      sequel_syntax_node_t node = {.kind = SEQUEL_SYNTAX_GROUP,
                                   .range = {tokens[start].range.start, tokens[closing].range.end},
                                   .header_range = tokens[start].range,
                                   .first_token = start,
                                   .token_count = closing + 1 - start,
                                   .production_id = SEQUEL_SYNTAX_GROUP};
      if (!append_node(document, node))
        return -1;
      size_t index = document->node_count - 1;
      size_t inner = next_expression_token(tokens, start + 1, closing);
      if (inner < closing) {
        size_t inner_node = 0;
        if (append_expression(document, tokens, source, inner, closing, &inner_node) < 0 ||
            !append_child(document, index, inner_node))
          return -1;
      }
      *out_index = index;
      return 1;
    }
  }
  size_t depth = 0, split = SIZE_MAX;
  int precedence = INT_MAX;
  for (size_t i = start; i < end; ++i) {
    if (tokens[i].kind != SEQUEL_TOKEN_PUNCTUATION && tokens[i].kind != SEQUEL_TOKEN_OPERATOR &&
        tokens[i].kind != SEQUEL_TOKEN_KEYWORD)
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
    if (!depth && (tokens[i].kind == SEQUEL_TOKEN_OPERATOR || tokens[i].kind == SEQUEL_TOKEN_KEYWORD)) {
      int candidate =
          tokens[i].kind == SEQUEL_TOKEN_KEYWORD ? expression_keyword_precedence(&tokens[i]) : expression_precedence(c);
      if (candidate && candidate <= precedence) {
        precedence = candidate;
        split = i;
      }
    }
  }
  sequel_syntax_kind_t kind =
      split != SIZE_MAX ? SEQUEL_SYNTAX_BINARY_EXPRESSION : expression_atom_kind(&tokens[start]);
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
  if (split == SIZE_MAX && tokens[start].kind == SEQUEL_TOKEN_OPERATOR)
    kind = SEQUEL_SYNTAX_UNARY_EXPRESSION;
  sequel_syntax_node_t node = {.kind = kind,
                               .range = {tokens[start].range.start, tokens[end - 1].range.end},
                               .header_range = tokens[start].range,
                               .first_token = start,
                               .token_count = end - start,
                               .production_id = (uint32_t)kind};
  if (!append_node(document, node))
    return -1;
  size_t index = document->node_count - 1;
  if (split != SIZE_MAX) {
    size_t left = 0, right = 0;
    if (append_expression(document, tokens, source, start, split, &left) < 0 ||
        append_expression(document, tokens, source, split + 1, end, &right) < 0 ||
        !append_child(document, index, left) || !append_child(document, index, right))
      return -1;
  }
  *out_index = index;
  return 1;
}

sequel_status_t sequel_parse(const sequel_lexed_document_t *lexed, const sequel_parse_options_t *options,
                             const sequel_cancellation_t *cancellation, sequel_syntax_document_t **out) {
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
                    (tokens[i].kind == SEQUEL_TOKEN_PUNCTUATION && tokens[i].range.end == tokens[i].range.start + 1 &&
                     sequel_lexed_source(lexed).data[tokens[i].range.start] == ';');
    if (i < token_count && tokens[i].kind == SEQUEL_TOKEN_PUNCTUATION) {
      sequel_source_view_t source = sequel_lexed_source(lexed);
      char punctuation = source.data[tokens[i].range.start];
      if (punctuation == '(' || punctuation == '[') {
        if (delimiter_depth >= nesting_limit) {
          if (!append_issue(document,
                            (sequel_parse_issue_t){SEQUEL_PARSE_ISSUE_UNEXPECTED_TOKEN, SEQUEL_RECOVERY_OPAQUE_REGION,
                                                   tokens[i].range, SIZE_MAX})) {
            sequel_syntax_document_free(document);
            return SEQUEL_ERR_NOMEM;
          }
        } else {
          delimiter_stack[delimiter_depth++] = i;
        }
      } else if (punctuation == ')' || punctuation == ']') {
        if (!delimiter_depth) {
          if (!append_issue(document,
                            (sequel_parse_issue_t){SEQUEL_PARSE_ISSUE_UNMATCHED_CLOSE, SEQUEL_RECOVERY_SKIPPED_TOKEN,
                                                   tokens[i].range, SIZE_MAX})) {
            sequel_syntax_document_free(document);
            return SEQUEL_ERR_NOMEM;
          }
        } else {
          size_t opening = delimiter_stack[delimiter_depth - 1];
          char expected = punctuation == ')' ? '(' : '[';
          if (source.data[tokens[opening].range.start] != expected) {
            if (!append_issue(document,
                              (sequel_parse_issue_t){SEQUEL_PARSE_ISSUE_UNEXPECTED_TOKEN, SEQUEL_RECOVERY_SKIPPED_TOKEN,
                                                     tokens[i].range, SIZE_MAX})) {
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
      if (!append_issue(document, (sequel_parse_issue_t){SEQUEL_PARSE_ISSUE_UNTERMINATED_TOKEN,
                                                         SEQUEL_RECOVERY_OPAQUE_REGION, tokens[i].range, SIZE_MAX})) {
        sequel_syntax_document_free(document);
        return SEQUEL_ERR_NOMEM;
      }
    }
    if (i < token_count && (tokens[i].flags & SEQUEL_TOKEN_FLAG_RECOVERED)) {
      if (!append_issue(document, (sequel_parse_issue_t){SEQUEL_PARSE_ISSUE_UNEXPECTED_TOKEN,
                                                         SEQUEL_RECOVERY_SKIPPED_TOKEN, tokens[i].range, SIZE_MAX})) {
        sequel_syntax_document_free(document);
        return SEQUEL_ERR_NOMEM;
      }
    }
    if (!boundary)
      continue;
    size_t first = start_token;
    while (first < i &&
           (tokens[first].kind == SEQUEL_TOKEN_WHITESPACE || tokens[first].kind == SEQUEL_TOKEN_LINE_COMMENT ||
            tokens[first].kind == SEQUEL_TOKEN_BLOCK_COMMENT))
      ++first;
    size_t last = i;
    while (last > first &&
           (tokens[last - 1].kind == SEQUEL_TOKEN_WHITESPACE || tokens[last - 1].kind == SEQUEL_TOKEN_LINE_COMMENT ||
            tokens[last - 1].kind == SEQUEL_TOKEN_BLOCK_COMMENT))
      --last;
    if (first < last) {
      sequel_source_view_t source = sequel_lexed_source(lexed);
      sequel_syntax_kind_t statement_kind_value = statement_kind(lexed, first);
      sequel_syntax_node_t node = {.kind = statement_kind_value,
                                   .range = {tokens[first].range.start, tokens[last - 1].range.end},
                                   .header_range = {tokens[first].range.start, tokens[first].range.end},
                                   .first_token = first,
                                   .token_count = last - first,
                                   .production_id = (uint32_t)statement_kind_value};
      (void)source;
      if (!append_node(document, node)) {
        sequel_syntax_document_free(document);
        return SEQUEL_ERR_NOMEM;
      }
      size_t parent = document->node_count - 1;
      for (size_t token_index = first; token_index < last; ++token_index) {
        sequel_syntax_kind_t child_kind = clause_kind(&tokens[token_index]);
        if (child_kind == SEQUEL_SYNTAX_ERROR)
          continue;
        size_t child_end = last;
        for (size_t next = token_index + 1; next < last; ++next) {
          if (clause_kind(&tokens[next]) != SEQUEL_SYNTAX_ERROR) {
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
        if (!append_node(document, child)) {
          sequel_syntax_document_free(document);
          return SEQUEL_ERR_NOMEM;
        }
        size_t child_index = document->node_count - 1;
        if (!append_child(document, parent, child_index)) {
          sequel_syntax_document_free(document);
          return SEQUEL_ERR_NOMEM;
        }
        size_t expression = 0;
        if (append_expression(document, tokens, &source, token_index + 1, child_end, &expression) < 0 ||
            (expression && !append_child(document, child_index, expression))) {
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
    if (!append_issue(document, (sequel_parse_issue_t){SEQUEL_PARSE_ISSUE_UNCLOSED_GROUP,
                                                       SEQUEL_RECOVERY_INSERTED_TOKEN,
                                                       {tokens[opening].range.start, source.length},
                                                       SIZE_MAX})) {
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

void sequel_syntax_document_free(sequel_syntax_document_t *document) {
  if (!document)
    return;
  free(document->nodes);
  free(document->children);
  free(document->issues);
  if (document->owns_lexed)
    sequel_lexed_document_free((sequel_lexed_document_t *)document->lexed);
  free(document);
}

const sequel_parse_issue_t *sequel_syntax_issues(const sequel_syntax_document_t *document, size_t *count) {
  if (count)
    *count = document ? document->issue_count : 0;
  return document ? document->issues : NULL;
}

size_t sequel_syntax_issue_count(const sequel_syntax_document_t *document) {
  return document ? document->issue_count : 0;
}

const sequel_lexed_document_t *sequel_syntax_lexed(const sequel_syntax_document_t *document) {
  return document ? document->lexed : NULL;
}

const sequel_syntax_node_t *sequel_syntax_nodes(const sequel_syntax_document_t *document, size_t *count) {
  if (count)
    *count = document ? document->node_count : 0;
  return document ? document->nodes : NULL;
}

const sequel_syntax_node_t *sequel_syntax_node(const sequel_syntax_document_t *document, size_t node_index) {
  return document && node_index < document->node_count ? &document->nodes[node_index] : NULL;
}

size_t sequel_syntax_node_count(const sequel_syntax_document_t *document) {
  return document ? document->node_count : 0;
}

const size_t *sequel_syntax_children(const sequel_syntax_document_t *document, size_t node_index, size_t *count) {
  if (!document || node_index >= document->node_count) {
    if (count)
      *count = 0;
    return NULL;
  }
  const sequel_syntax_node_t *node = &document->nodes[node_index];
  if (count)
    *count = node->child_count;
  return document->children + node->first_child;
}

const sequel_token_t *sequel_syntax_tokens(const sequel_syntax_document_t *document, size_t node_index, size_t *count) {
  if (count)
    *count = 0;
  if (!document || node_index >= document->node_count)
    return NULL;
  const sequel_syntax_node_t *node = &document->nodes[node_index];
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(document->lexed, &token_count);
  if (node->first_token > token_count || node->token_count > token_count - node->first_token)
    return NULL;
  if (count)
    *count = node->token_count;
  return tokens + node->first_token;
}

static int visit_node(const sequel_syntax_document_t *document, size_t node_index,
                      const sequel_syntax_visitor_t *visitor) {
  if (node_index >= document->node_count)
    return 0;
  if (visitor->enter_node && !visitor->enter_node(document, node_index, visitor->userdata))
    return 1;
  const sequel_syntax_node_t *node = &document->nodes[node_index];
  for (size_t i = 0; i < node->child_count; ++i)
    if (!visit_node(document, document->children[node->first_child + i], visitor))
      return 0;
  return !visitor->leave_node || visitor->leave_node(document, node_index, visitor->userdata);
}

sequel_status_t sequel_syntax_visit(const sequel_syntax_document_t *document, size_t root_node,
                                    const sequel_syntax_visitor_t *visitor) {
  if (!document || !visitor)
    return SEQUEL_ERR_NULL;
  if (root_node >= document->node_count)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  return visit_node(document, root_node, visitor) ? SEQUEL_OK : SEQUEL_ERR_CANCELLED;
}

sequel_status_t sequel_parse_sql(const sequel_source_view_t *source, const sequel_parse_options_t *options,
                                 const sequel_cancellation_t *cancellation, sequel_syntax_document_t **out) {
  sequel_lexed_document_t *lexed = NULL;
  sequel_status_t status = sequel_lex(source, options, cancellation, &lexed);
  if (status != SEQUEL_OK)
    return status;
  status = sequel_parse(lexed, options, cancellation, out);
  if (status != SEQUEL_OK) {
    sequel_lexed_document_free(lexed);
    return status;
  }
  (*out)->owns_lexed = true;
  return SEQUEL_OK;
}

sequel_status_t sequel_reparse(const sequel_syntax_document_t *previous, const sequel_text_edit_t *edit,
                               const sequel_parse_options_t *options, const sequel_cancellation_t *cancellation,
                               sequel_syntax_document_t **out) {
  if (!previous || !edit || !out)
    return SEQUEL_ERR_NULL;
  *out = NULL;
  if (!previous->lexed)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  sequel_source_view_t old_source = sequel_lexed_source(previous->lexed);
  if (edit->start > edit->old_end || edit->old_end > old_source.length ||
      (edit->replacement_length && !edit->replacement))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  size_t removed = edit->old_end - edit->start;
  if (old_source.length - removed > SIZE_MAX - edit->replacement_length)
    return SEQUEL_ERR_OVERFLOW;
  size_t length = old_source.length - removed + edit->replacement_length;
  char *data = malloc(length ? length : 1);
  if (!data)
    return SEQUEL_ERR_NOMEM;
  memcpy(data, old_source.data, edit->start);
  memcpy(data + edit->start, edit->replacement, edit->replacement_length);
  memcpy(data + edit->start + edit->replacement_length, old_source.data + edit->old_end,
         old_source.length - edit->old_end);
  sequel_source_view_t source = {data, length};
  sequel_parse_options_t inherited;
  if (!options) {
    sequel_parse_options_init(&inherited);
    inherited.dialect = sequel_lexed_dialect(previous->lexed);
    options = &inherited;
  }
  sequel_status_t status = sequel_parse_sql(&source, options, cancellation, out);
  free(data);
  return status;
}

sequel_status_t sequel_reparse_impact(size_t old_length, const sequel_text_edit_t *edit,
                                      sequel_reparse_impact_t *impact) {
  if (!edit || !impact)
    return SEQUEL_ERR_NULL;
  if (edit->start > edit->old_end || edit->old_end > old_length || (edit->replacement_length && !edit->replacement))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (old_length - (edit->old_end - edit->start) > SIZE_MAX - edit->replacement_length)
    return SEQUEL_ERR_OVERFLOW;
  impact->old_range = (sequel_source_range_t){edit->start, edit->old_end};
  impact->new_range = (sequel_source_range_t){edit->start, edit->start + edit->replacement_length};
  impact->preserved_prefix = edit->start;
  impact->preserved_suffix = old_length - edit->old_end;
  return SEQUEL_OK;
}
