/** @file
 * @brief Normalized query model accessors and visitor API.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <sequel/parser.h>
#include <sequel/binds.h>
#include <sequel/source.h>
#include <sequel/syntax.h>

SEQUEL_EXTERN_C_BEGIN

/** @brief Opaque owned normalized query document. */
typedef struct sequel_query_document sequel_query_document_t;

/** Identifies the top-level operation represented by a statement. */
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

/** Describes one top-level statement in a normalized query document. */
typedef struct {
  sequel_source_range_t range;
  sequel_operation_t operation;
  size_t syntax_node;
  size_t query_body;
  uint32_t flags;
} sequel_statement_info_t;

/** A clause boundary retained from the parsed token stream. */
typedef struct {
  sequel_source_range_t range;
  size_t statement_index;
  uint32_t keyword_id;
} sequel_clause_info_t;

/** @brief Kind of relation reference discovered in a query scope. */
typedef enum { SEQUEL_RELATION_TABLE = 0, SEQUEL_RELATION_SUBQUERY, SEQUEL_RELATION_CTE, SEQUEL_RELATION_FUNCTION, SEQUEL_RELATION_VALUES, SEQUEL_RELATION_UNKNOWN } sequel_relation_kind_t;

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

/** @brief Flags describing common-table-expression materialization. */
enum { SEQUEL_CTE_FLAG_NONE = 0, SEQUEL_CTE_FLAG_MATERIALIZED = 1u << 0, SEQUEL_CTE_FLAG_NOT_MATERIALIZED = 1u << 1 };

/** @brief Flags describing relation scope membership. */
enum { SEQUEL_RELATION_FLAG_NESTED_SCOPE = 1u << 0, SEQUEL_RELATION_FLAG_PROJECTION_SCOPE = 1u << 1 };

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

/** @brief Join operator represented in the normalized query model. */
typedef enum { SEQUEL_JOIN_INNER = 0, SEQUEL_JOIN_LEFT, SEQUEL_JOIN_RIGHT, SEQUEL_JOIN_FULL, SEQUEL_JOIN_CROSS, SEQUEL_JOIN_NATURAL, SEQUEL_JOIN_LATERAL, SEQUEL_JOIN_UNKNOWN } sequel_join_kind_t;

enum {
  SEQUEL_JOIN_FLAG_OUTER = 1u << 0,
  SEQUEL_JOIN_FLAG_NATURAL = 1u << 1,
  SEQUEL_JOIN_FLAG_LATERAL = 1u << 2,
  SEQUEL_JOIN_FLAG_USING = 1u << 3,
  SEQUEL_JOIN_FLAG_ASOF = 1u << 4,
  SEQUEL_JOIN_FLAG_GLOBAL = 1u << 5,
  SEQUEL_JOIN_FLAG_ANY = 1u << 6,
  SEQUEL_JOIN_FLAG_SEMI = 1u << 7,
  SEQUEL_JOIN_FLAG_ANTI = 1u << 8,
};

typedef struct {
  sequel_join_kind_t kind;
  uint32_t flags;
  sequel_source_range_t range;
  /** Exact source range of the expression after ON; empty when has_condition is false. */
  sequel_source_range_t condition_range;
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
  bool (*clause)(const sequel_query_document_t *document, size_t clause_index, void *userdata);
  bool (*cte)(const sequel_query_document_t *document, size_t cte_index, void *userdata);
  bool (*relation)(const sequel_query_document_t *document, size_t relation_index, void *userdata);
  bool (*parameter)(const sequel_query_document_t *document, size_t parameter_index, void *userdata);
  bool (*join)(const sequel_query_document_t *document, size_t join_index, void *userdata);
  bool (*projection)(const sequel_query_document_t *document, size_t projection_index, void *userdata);
  bool (*predicate)(const sequel_query_document_t *document, size_t predicate_index, void *userdata);
  void *userdata;
} sequel_query_visitor_t;

/**
 * Build a normalized query view that borrows the supplied syntax document.
 * The syntax document and its lexical source must outlive the query document.
 */
sequel_status_t sequel_build_query_document(const sequel_syntax_document_t *syntax, const sequel_parse_options_t *options, sequel_query_document_t **out);
/** Release a query document and its owned indexes. */
void sequel_query_document_free(sequel_query_document_t *document);

/** Return the number of top-level statements. */
size_t sequel_query_statement_count(const sequel_query_document_t *document);

/** Return the contiguous top-level statement array, owned by @p document. */
const sequel_statement_info_t *sequel_query_statements(const sequel_query_document_t *document);

/** Return the number of statements in the query's active scope. */
size_t sequel_query_scope_statement_count(const sequel_query_document_t *document);

/** Return statements in the query's active scope, owned by @p document. */
const sequel_statement_info_t *sequel_query_scope_statements(const sequel_query_document_t *document);

/** Return the number of relation references. */
size_t sequel_query_relation_count(const sequel_query_document_t *document);

/** Return all relation references, owned by @p document. */
const sequel_relation_info_t *sequel_query_relations(const sequel_query_document_t *document);

/** Return relation @p index, or NULL when @p index is out of range. */
const sequel_relation_info_t *sequel_query_relation(const sequel_query_document_t *document, size_t index);

/** Return the number of common-table expressions. */
size_t sequel_query_cte_count(const sequel_query_document_t *document);

/** Return all common-table expressions, owned by @p document. */
const sequel_cte_info_t *sequel_query_ctes(const sequel_query_document_t *document);

/** Return whether the outer query uses DISTINCT ON. */
bool sequel_query_outer_distinct_on(const sequel_query_document_t *document);

/** Return whether the outer query has a LIMIT clause. */
bool sequel_query_outer_limit(const sequel_query_document_t *document);

/** Return the source range of the outer LIMIT value and its operands. */
sequel_source_range_t sequel_query_outer_limit_range(const sequel_query_document_t *document);

/** Return the complete outer LIMIT clause, including comma or OFFSET operands. */
sequel_source_range_t sequel_query_outer_limit_clause_range(const sequel_query_document_t *document);

/** Return the source range occupied by the outer LIMIT value only. */
sequel_source_range_t sequel_query_outer_limit_value_range(const sequel_query_document_t *document);

/** Return the source range occupied by the outer query. */
sequel_source_range_t sequel_query_outer_range(const sequel_query_document_t *document);

/** Return the number of parameter references. */
size_t sequel_query_parameter_count(const sequel_query_document_t *document);

/** Return all parameter references, owned by @p document. */
const sequel_parameter_info_t *sequel_query_parameters(const sequel_query_document_t *document);

/** Return parameter @p index, or NULL when @p index is out of range. */
const sequel_parameter_info_t *sequel_query_parameter(const sequel_query_document_t *document, size_t index);

/** Return the number of joins. */
size_t sequel_query_join_count(const sequel_query_document_t *document);

/** Return all joins, owned by @p document. */
const sequel_join_info_t *sequel_query_joins(const sequel_query_document_t *document);

/** Return join @p index, or NULL when @p index is out of range. */
const sequel_join_info_t *sequel_query_join(const sequel_query_document_t *document, size_t index);

/** Return the number of projected expressions. */
size_t sequel_query_projection_count(const sequel_query_document_t *document);

/** Return all projected expressions, owned by @p document. */
const sequel_projection_info_t *sequel_query_projections(const sequel_query_document_t *document);

/** Return projection @p index, or NULL when @p index is out of range. */
const sequel_projection_info_t *sequel_query_projection(const sequel_query_document_t *document, size_t index);

/** Return the number of predicates. */
size_t sequel_query_predicate_count(const sequel_query_document_t *document);

/** Return all predicates, owned by @p document. */
const sequel_predicate_info_t *sequel_query_predicates(const sequel_query_document_t *document);

/** Return predicate @p index, or NULL when @p index is out of range. */
const sequel_predicate_info_t *sequel_query_predicate(const sequel_query_document_t *document, size_t index);

/** Return the number of clauses. */
size_t sequel_query_clause_count(const sequel_query_document_t *document);

/** Return all clauses, owned by @p document. */
const sequel_clause_info_t *sequel_query_clauses(const sequel_query_document_t *document);

/** Return the source view borrowed by @p document. */
sequel_source_view_t sequel_query_source(const sequel_query_document_t *document);

/** Return the dialect retained by the query document. */
sequel_dialect_t sequel_query_dialect(const sequel_query_document_t *document);
/**
 * @brief Query syntax.
 * @param document Argument supplied by the caller.
 */
const sequel_syntax_document_t *sequel_query_syntax(const sequel_query_document_t *document);

/** Visit normalized query entities in document order. */
sequel_status_t sequel_query_visit(const sequel_query_document_t *document, const sequel_query_visitor_t *visitor);

SEQUEL_EXTERN_C_END
