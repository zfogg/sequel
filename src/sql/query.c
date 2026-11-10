#include <stdlib.h>
#include <string.h>

#include <sequel/query.h>

struct sequel_query_document {
  const sequel_syntax_document_t *syntax;
  sequel_statement_info_t *statements;
  size_t statement_count;
  sequel_relation_info_t *relations;
  size_t relation_count;
  sequel_parameter_info_t *parameters;
  size_t parameter_count;
};

static int append(void **array, size_t *count, size_t size, const void *value) {
  void *grown = realloc(*array, (*count + 1) * size);
  if (!grown)
    return 0;
  *array = grown;
  memcpy((char *)grown + *count * size, value, size);
  ++*count;
  return 1;
}

static sequel_operation_t operation(sequel_syntax_kind_t kind) {
  switch (kind) {
  case SEQUEL_SYNTAX_SELECT_STATEMENT:
    return SEQUEL_OPERATION_SELECT;
  case SEQUEL_SYNTAX_INSERT_STATEMENT:
    return SEQUEL_OPERATION_INSERT;
  case SEQUEL_SYNTAX_UPDATE_STATEMENT:
    return SEQUEL_OPERATION_UPDATE;
  case SEQUEL_SYNTAX_DELETE_STATEMENT:
    return SEQUEL_OPERATION_DELETE;
  default:
    return SEQUEL_OPERATION_UNKNOWN;
  }
}

sequel_status_t sequel_build_query_document(const sequel_syntax_document_t *syntax,
                                            const sequel_parse_options_t *options, sequel_query_document_t **out) {
  if (!syntax || !out)
    return SEQUEL_ERR_NULL;
  *out = NULL;
  sequel_query_document_t *document = calloc(1, sizeof(*document));
  if (!document)
    return SEQUEL_ERR_NOMEM;
  document->syntax = syntax;
  size_t node_count = 0;
  const sequel_syntax_node_t *nodes = sequel_syntax_nodes(syntax, &node_count);
  size_t max_nodes = options && options->max_nodes ? options->max_nodes : (size_t)-1;
  for (size_t i = 0; i < node_count && document->statement_count < max_nodes; ++i) {
    sequel_statement_info_t statement = {.range = nodes[i].range,
                                         .operation = operation((sequel_syntax_kind_t)nodes[i].production_id),
                                         .syntax_node = i,
                                         .query_body = SIZE_MAX};
    if (!append((void **)&document->statements, &document->statement_count, sizeof(statement), &statement)) {
      sequel_query_document_free(document);
      return SEQUEL_ERR_NOMEM;
    }
  }
  *out = document;
  return SEQUEL_OK;
}

void sequel_query_document_free(sequel_query_document_t *document) {
  if (!document)
    return;
  free(document->statements);
  free(document->relations);
  free(document->parameters);
  free(document);
}

size_t sequel_query_statement_count(const sequel_query_document_t *document) {
  return document ? document->statement_count : 0;
}
const sequel_statement_info_t *sequel_query_statements(const sequel_query_document_t *document) {
  return document ? document->statements : NULL;
}
size_t sequel_query_relation_count(const sequel_query_document_t *document) {
  return document ? document->relation_count : 0;
}
const sequel_relation_info_t *sequel_query_relations(const sequel_query_document_t *document) {
  return document ? document->relations : NULL;
}
size_t sequel_query_parameter_count(const sequel_query_document_t *document) {
  return document ? document->parameter_count : 0;
}
const sequel_parameter_info_t *sequel_query_parameters(const sequel_query_document_t *document) {
  return document ? document->parameters : NULL;
}
