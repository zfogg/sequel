#include <sequel/query.h>

static bool expression_syntax_kind(sequel_syntax_kind_t kind) {
  return kind >= SEQUEL_SYNTAX_IDENTIFIER && kind <= SEQUEL_SYNTAX_GROUP;
}

size_t sequel_query_expression_node_for_range(const sequel_syntax_document_t *syntax, sequel_source_range_t range) {
  size_t count = 0;
  const sequel_syntax_node_t *nodes = sequel_syntax_nodes(syntax, &count);
  size_t best = SIZE_MAX;
  size_t best_width = SIZE_MAX;
  for (size_t i = 0; i < count; ++i) {
    if (!expression_syntax_kind(nodes[i].kind) ||
        (!sequel_source_range_contains(range, nodes[i].range) && !sequel_source_range_contains(nodes[i].range, range)))
      continue;
    size_t width = nodes[i].range.end - nodes[i].range.start;
    if (width < best_width) {
      best = i;
      best_width = width;
    }
  }
  return best;
}
