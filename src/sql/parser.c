#include <stdlib.h>
#include <string.h>

#include <sequel/parser.h>

struct sequel_syntax_document {
  const sequel_lexed_document_t *lexed;
  bool owns_lexed;
  sequel_syntax_node_t *nodes;
  size_t node_count;
  size_t *children;
  size_t child_count;
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
  case 6:
    return SEQUEL_SYNTAX_FROM_CLAUSE;
  case 7:
    return SEQUEL_SYNTAX_WHERE_CLAUSE;
  case 8:
    return SEQUEL_SYNTAX_GROUP_BY_CLAUSE;
  case 9:
    return SEQUEL_SYNTAX_ORDER_BY_CLAUSE;
  case 11:
    return SEQUEL_SYNTAX_LIMIT_CLAUSE;
  case 12:
    return SEQUEL_SYNTAX_OFFSET_CLAUSE;
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

sequel_status_t sequel_parse(const sequel_lexed_document_t *lexed, const sequel_parse_options_t *options,
                             const sequel_cancellation_t *cancellation, sequel_syntax_document_t **out) {
  if (!lexed || !out)
    return SEQUEL_ERR_NULL;
  *out = NULL;
  sequel_syntax_document_t *document = calloc(1, sizeof(*document));
  if (!document)
    return SEQUEL_ERR_NOMEM;
  document->lexed = lexed;
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  size_t start_token = 0;
  for (size_t i = 0; i <= token_count; ++i) {
    if (cancellation && cancellation->is_cancelled && cancellation->is_cancelled(cancellation->userdata)) {
      sequel_syntax_document_free(document);
      return SEQUEL_ERR_CANCELLED;
    }
    bool boundary = i == token_count || tokens[i].kind == SEQUEL_TOKEN_EOF ||
                    (tokens[i].kind == SEQUEL_TOKEN_PUNCTUATION && tokens[i].range.end == tokens[i].range.start + 1 &&
                     sequel_lexed_source(lexed).data[tokens[i].range.start] == ';');
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
      for (size_t token_index = first + 1; token_index < last; ++token_index) {
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
        if (!append_child(document, parent, document->node_count - 1)) {
          sequel_syntax_document_free(document);
          return SEQUEL_ERR_NOMEM;
        }
      }
    }
    if (i == token_count || tokens[i].kind == SEQUEL_TOKEN_EOF)
      break;
    start_token = i + (i < token_count && tokens[i].kind != SEQUEL_TOKEN_EOF ? 1 : 0);
  }
  (void)options;
  *out = document;
  return SEQUEL_OK;
}

void sequel_syntax_document_free(sequel_syntax_document_t *document) {
  if (!document)
    return;
  free(document->nodes);
  free(document->children);
  if (document->owns_lexed)
    sequel_lexed_document_free((sequel_lexed_document_t *)document->lexed);
  free(document);
}

const sequel_lexed_document_t *sequel_syntax_lexed(const sequel_syntax_document_t *document) {
  return document ? document->lexed : NULL;
}

const sequel_syntax_node_t *sequel_syntax_nodes(const sequel_syntax_document_t *document, size_t *count) {
  if (count)
    *count = document ? document->node_count : 0;
  return document ? document->nodes : NULL;
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
  sequel_status_t status = sequel_parse_sql(&source, options, cancellation, out);
  free(data);
  return status;
}
