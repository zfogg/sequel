#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <sequel/parser.h>
#include <sequel/binds.h>
#include <sequel/source.h>
#include <sequel/syntax.h>

SEQUEL_EXTERN_C_BEGIN

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
  SEQUEL_OPERATION_EXPLAIN,
  SEQUEL_OPERATION_OPTIMIZE,
  SEQUEL_OPERATION_TRUNCATE
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
  sequel_source_range_t name_range;
  sequel_source_range_t body_range;
  size_t statement_index;
  size_t syntax_node;
  uint32_t flags;
} sequel_cte_info_t;

enum { SEQUEL_RELATION_FLAG_NESTED_SCOPE = 1u << 0 };

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

typedef enum {
  SEQUEL_JOIN_INNER = 0,
  SEQUEL_JOIN_LEFT,
  SEQUEL_JOIN_RIGHT,
  SEQUEL_JOIN_FULL,
  SEQUEL_JOIN_CROSS,
  SEQUEL_JOIN_NATURAL,
  SEQUEL_JOIN_LATERAL,
  SEQUEL_JOIN_UNKNOWN
} sequel_join_kind_t;

typedef struct {
  sequel_join_kind_t kind;
  sequel_source_range_t range;
  size_t left_relation;
  size_t right_relation;
  size_t condition_node;
  bool has_condition;
} sequel_join_info_t;

typedef struct {
  sequel_source_range_t range;
  size_t statement_index;
  size_t expression_node;
  bool wildcard;
  sequel_source_range_t alias_range;
} sequel_projection_info_t;

typedef struct {
  sequel_source_range_t range;
  size_t statement_index;
  size_t expression_node;
  uint64_t normalized_hash;
  uint32_t depth;
} sequel_predicate_info_t;

typedef struct {
  bool (*statement)(const sequel_query_document_t *document, size_t statement_index, void *userdata);
  bool (*relation)(const sequel_query_document_t *document, size_t relation_index, void *userdata);
  bool (*parameter)(const sequel_query_document_t *document, size_t parameter_index, void *userdata);
  bool (*join)(const sequel_query_document_t *document, size_t join_index, void *userdata);
  bool (*projection)(const sequel_query_document_t *document, size_t projection_index, void *userdata);
  bool (*predicate)(const sequel_query_document_t *document, size_t predicate_index, void *userdata);
  void *userdata;
} sequel_query_visitor_t;

sequel_status_t sequel_build_query_document(const sequel_syntax_document_t *syntax,
                                            const sequel_parse_options_t *options, sequel_query_document_t **out);
void sequel_query_document_free(sequel_query_document_t *document);
size_t sequel_query_statement_count(const sequel_query_document_t *document);
const sequel_statement_info_t *sequel_query_statements(const sequel_query_document_t *document);
size_t sequel_query_relation_count(const sequel_query_document_t *document);
const sequel_relation_info_t *sequel_query_relations(const sequel_query_document_t *document);
size_t sequel_query_cte_count(const sequel_query_document_t *document);
const sequel_cte_info_t *sequel_query_ctes(const sequel_query_document_t *document);
bool sequel_query_outer_distinct_on(const sequel_query_document_t *document);
sequel_source_range_t sequel_query_outer_range(const sequel_query_document_t *document);
size_t sequel_query_parameter_count(const sequel_query_document_t *document);
const sequel_parameter_info_t *sequel_query_parameters(const sequel_query_document_t *document);
size_t sequel_query_join_count(const sequel_query_document_t *document);
const sequel_join_info_t *sequel_query_joins(const sequel_query_document_t *document);
size_t sequel_query_projection_count(const sequel_query_document_t *document);
const sequel_projection_info_t *sequel_query_projections(const sequel_query_document_t *document);
size_t sequel_query_predicate_count(const sequel_query_document_t *document);
const sequel_predicate_info_t *sequel_query_predicates(const sequel_query_document_t *document);
sequel_source_view_t sequel_query_source(const sequel_query_document_t *document);
const sequel_syntax_document_t *sequel_query_syntax(const sequel_query_document_t *document);

sequel_status_t sequel_query_visit(const sequel_query_document_t *document, const sequel_query_visitor_t *visitor);

SEQUEL_EXTERN_C_END
