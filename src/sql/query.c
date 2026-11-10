#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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

static size_t statement_for_range(const sequel_query_document_t *document, sequel_source_range_t range) {
  for (size_t i = 0; i < document->statement_count; ++i)
    if (sequel_source_range_contains(document->statements[i].range, range))
      return i;
  return SIZE_MAX;
}

static sequel_bind_style_t bind_style(const char *text, size_t length, size_t *number) {
  *number = 0;
  if (!length)
    return SEQUEL_BIND_UNKNOWN;
  if (text[0] == '?') {
    if (length == 1)
      return SEQUEL_BIND_POSITIONAL;
    for (size_t i = 1; i < length; ++i) {
      if (!isdigit((unsigned char)text[i]))
        return SEQUEL_BIND_UNKNOWN;
      *number = *number * 10 + (size_t)(text[i] - '0');
    }
    return *number ? SEQUEL_BIND_NUMBERED : SEQUEL_BIND_UNKNOWN;
  }
  if (text[0] == '$') {
    if (length == 1)
      return SEQUEL_BIND_UNKNOWN;
    bool numeric = true;
    for (size_t i = 1; i < length; ++i) {
      if (!isdigit((unsigned char)text[i]))
        numeric = false;
      else
        *number = *number * 10 + (size_t)(text[i] - '0');
    }
    return numeric && *number ? SEQUEL_BIND_NUMBERED : SEQUEL_BIND_NAMED;
  }
  if (text[0] == ':' || text[0] == '@')
    return length > 1 ? SEQUEL_BIND_NAMED : SEQUEL_BIND_UNKNOWN;
  return SEQUEL_BIND_UNKNOWN;
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
    sequel_syntax_kind_t kind = nodes[i].kind;
    if (kind < SEQUEL_SYNTAX_SELECT_STATEMENT || kind > SEQUEL_SYNTAX_UNKNOWN_STATEMENT)
      continue;
    sequel_statement_info_t statement = {
        .range = nodes[i].range, .operation = operation(kind), .syntax_node = i, .query_body = SIZE_MAX};
    if (!append((void **)&document->statements, &document->statement_count, sizeof(statement), &statement)) {
      sequel_query_document_free(document);
      return SEQUEL_ERR_NOMEM;
    }
  }
  const sequel_lexed_document_t *lexed = sequel_syntax_lexed(syntax);
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  sequel_source_view_t source = sequel_lexed_source(lexed);
  size_t ordinal = 0;
  for (size_t i = 0; i < token_count; ++i) {
    if (tokens[i].kind != SEQUEL_TOKEN_PARAMETER)
      continue;
    const char *text = source.data + tokens[i].range.start;
    size_t length = tokens[i].range.end - tokens[i].range.start;
    size_t explicit_number = 0;
    sequel_bind_style_t style = bind_style(text, length, &explicit_number);
    if (style == SEQUEL_BIND_UNKNOWN)
      continue;
    sequel_parameter_info_t parameter = {
        .range = tokens[i].range,
        .style = style,
        .statement_index = statement_for_range(document, tokens[i].range),
        .ordinal = ++ordinal,
        .explicit_number = explicit_number,
        .name_range = {tokens[i].range.start + (style == SEQUEL_BIND_POSITIONAL ? 0 : 1), tokens[i].range.end},
        .type_range = {tokens[i].range.end, tokens[i].range.end},
        .identity = explicit_number ? explicit_number : ordinal};
    if (!append((void **)&document->parameters, &document->parameter_count, sizeof(parameter), &parameter)) {
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
