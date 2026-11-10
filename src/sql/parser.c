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
      sequel_syntax_kind_t kind = statement_kind(lexed, first);
      sequel_syntax_node_t node = {.kind = SEQUEL_SYNTAX_STATEMENT,
                                   .range = {tokens[first].range.start, tokens[last - 1].range.end},
                                   .header_range = {tokens[first].range.start, tokens[first].range.end},
                                   .first_token = first,
                                   .token_count = last - first,
                                   .production_id = (uint32_t)kind};
      (void)source;
      if (!append_node(document, node)) {
        sequel_syntax_document_free(document);
        return SEQUEL_ERR_NOMEM;
      }
    }
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
