#include <stdlib.h>

#include <sequel.h>

#include "model.h"

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

size_t sequel_syntax_issue_count(const sequel_syntax_document_t *document) { return document ? document->issue_count : 0; }

const sequel_lexed_document_t *sequel_syntax_lexed(const sequel_syntax_document_t *document) { return document ? document->lexed : NULL; }

const sequel_syntax_node_t *sequel_syntax_nodes(const sequel_syntax_document_t *document, size_t *count) {
  if (count)
    *count = document ? document->node_count : 0;
  return document ? document->nodes : NULL;
}

const sequel_syntax_node_t *sequel_syntax_node(const sequel_syntax_document_t *document, size_t node_index) {
  return document && node_index < document->node_count ? &document->nodes[node_index] : NULL;
}

size_t sequel_syntax_node_count(const sequel_syntax_document_t *document) { return document ? document->node_count : 0; }

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
