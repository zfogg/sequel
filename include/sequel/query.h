#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <sequel/parser.h>
#include <sequel/binds.h>
#include <sequel/source.h>
#include <sequel/syntax.h>

typedef struct sequel_query_document sequel_query_document_t;

typedef enum {
  SEQUEL_OPERATION_UNKNOWN = 0,
  SEQUEL_OPERATION_SELECT,
  SEQUEL_OPERATION_INSERT,
  SEQUEL_OPERATION_UPDATE,
  SEQUEL_OPERATION_DELETE,
  SEQUEL_OPERATION_MERGE,
  SEQUEL_OPERATION_CREATE,
  SEQUEL_OPERATION_ALTER,
  SEQUEL_OPERATION_DROP,
  SEQUEL_OPERATION_TRANSACTION,
  SEQUEL_OPERATION_EXPLAIN
} sequel_operation_t;

typedef struct {
  sequel_source_range_t range;
  sequel_operation_t operation;
  size_t syntax_node;
  size_t query_body;
  uint32_t flags;
} sequel_statement_info_t;

typedef enum {
  SEQUEL_RELATION_TABLE = 0,
  SEQUEL_RELATION_SUBQUERY,
  SEQUEL_RELATION_CTE,
  SEQUEL_RELATION_FUNCTION,
  SEQUEL_RELATION_VALUES,
  SEQUEL_RELATION_UNKNOWN
} sequel_relation_kind_t;

typedef struct {
  sequel_relation_kind_t kind;
  sequel_source_range_t range;
  sequel_source_range_t name_range;
  sequel_source_range_t alias_range;
  size_t statement_index;
  size_t syntax_node;
  size_t parent_relation;
  size_t subquery_statement;
  uint32_t flags;
} sequel_relation_info_t;

typedef struct {
  sequel_source_range_t range;
  sequel_bind_style_t style;
  size_t statement_index;
  size_t ordinal;
  size_t explicit_number;
  sequel_source_range_t name_range;
  sequel_source_range_t type_range;
  uint64_t identity;
} sequel_parameter_info_t;

sequel_status_t sequel_build_query_document(const sequel_syntax_document_t *syntax,
                                            const sequel_parse_options_t *options, sequel_query_document_t **out);
void sequel_query_document_free(sequel_query_document_t *document);
size_t sequel_query_statement_count(const sequel_query_document_t *document);
const sequel_statement_info_t *sequel_query_statements(const sequel_query_document_t *document);
size_t sequel_query_relation_count(const sequel_query_document_t *document);
const sequel_relation_info_t *sequel_query_relations(const sequel_query_document_t *document);
size_t sequel_query_parameter_count(const sequel_query_document_t *document);
const sequel_parameter_info_t *sequel_query_parameters(const sequel_query_document_t *document);
