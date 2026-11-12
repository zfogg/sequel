#include <sequel/parser.h>

#include "model.h"

static int sequel_syntax_visit_node(const sequel_syntax_document_t *document, size_t node_index,
                                    const sequel_syntax_visitor_t *visitor) {
  if (node_index >= document->node_count)
    return 0;
  if (visitor->enter_node && !visitor->enter_node(document, node_index, visitor->userdata))
    return 0;
  const sequel_syntax_node_t *node = &document->nodes[node_index];
  for (size_t i = 0; i < node->child_count; ++i)
    if (!sequel_syntax_visit_node(document, document->children[node->first_child + i], visitor))
      return 0;
  return !visitor->leave_node || visitor->leave_node(document, node_index, visitor->userdata);
}

sequel_status_t sequel_syntax_visit(const sequel_syntax_document_t *document, size_t root_node,
                                    const sequel_syntax_visitor_t *visitor) {
  if (!document || !visitor)
    return SEQUEL_ERR_NULL;
  if (root_node >= document->node_count)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  return sequel_syntax_visit_node(document, root_node, visitor) ? SEQUEL_OK : SEQUEL_ERR_CANCELLED;
}
