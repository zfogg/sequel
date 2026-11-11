#pragma once

#include <stdbool.h>
#include <stddef.h>

/** Dialect-specific lexical and syntactic rules. */
typedef enum {
  SEQUEL_GENERIC,
  SEQUEL_SQLITE,
  SEQUEL_POSTGRESQL,
  SEQUEL_DUCKDB,
  SEQUEL_MYSQL,
  SEQUEL_CLICKHOUSE
} sequel_dialect_t;

typedef struct {
  const char *name;
  unsigned id;
} sequel_keyword_definition_t;

typedef struct {
  const char *text;
  unsigned precedence;
  bool right_associative;
  bool unary;
} sequel_operator_definition_t;

typedef struct {
  const char *name;
  unsigned keyword_id;
  bool repeatable;
} sequel_clause_definition_t;

/** Stable keyword identifiers shared by lexer, parser and query consumers. */
enum {
  SEQUEL_KEYWORD_SELECT = 1,
  SEQUEL_KEYWORD_INSERT = 2,
  SEQUEL_KEYWORD_UPDATE = 3,
  SEQUEL_KEYWORD_DELETE = 4,
  SEQUEL_KEYWORD_WITH = 5,
  SEQUEL_KEYWORD_FROM = 6,
  SEQUEL_KEYWORD_WHERE = 7,
  SEQUEL_KEYWORD_GROUP = 8,
  SEQUEL_KEYWORD_ORDER = 9,
  SEQUEL_KEYWORD_LIMIT = 11,
  SEQUEL_KEYWORD_OFFSET = 12,
  SEQUEL_KEYWORD_JOIN = 13,
  SEQUEL_KEYWORD_ON = 14,
  SEQUEL_KEYWORD_UNION = 15,
  SEQUEL_KEYWORD_HAVING = 16,
  SEQUEL_KEYWORD_RETURNING = 17,
  SEQUEL_KEYWORD_VALUES = 18,
  SEQUEL_KEYWORD_SET = 29,
  SEQUEL_KEYWORD_INTO = 30,
  SEQUEL_KEYWORD_USING = 31,
  SEQUEL_KEYWORD_WINDOW = 32,
  SEQUEL_KEYWORD_QUALIFY = 33,
  SEQUEL_KEYWORD_AND = 35,
  SEQUEL_KEYWORD_OR = 36,
  SEQUEL_KEYWORD_IN = 37,
  SEQUEL_KEYWORD_BETWEEN = 38,
  SEQUEL_KEYWORD_LIKE = 39,
  SEQUEL_KEYWORD_IS = 40,
  SEQUEL_KEYWORD_NOT = 41,
  SEQUEL_KEYWORD_CASE = 42,
  SEQUEL_KEYWORD_WHEN = 43,
  SEQUEL_KEYWORD_THEN = 44,
  SEQUEL_KEYWORD_ELSE = 45,
  SEQUEL_KEYWORD_END = 46,
  SEQUEL_KEYWORD_CAST = 47,
  SEQUEL_KEYWORD_EXISTS = 48,
  SEQUEL_KEYWORD_OVER = 49,
  SEQUEL_KEYWORD_AS = 50,
  SEQUEL_KEYWORD_PREWHERE = 51
};

typedef struct {
  sequel_dialect_t id;
  const char *name;
  bool supports_nested_block_comments;
  bool supports_dollar_quoted_strings;
  bool supports_backtick_identifiers;
  bool supports_bracket_identifiers;
  bool supports_named_parameters;
  bool supports_numbered_parameters;
  bool supports_typed_parameters;
  const sequel_keyword_definition_t *keywords;
  size_t keyword_count;
  const sequel_operator_definition_t *operators;
  size_t operator_count;
  const sequel_clause_definition_t *clauses;
  size_t clause_count;
} sequel_dialect_definition_t;

const sequel_dialect_definition_t *sequel_dialect_definition(sequel_dialect_t dialect);
const char *sequel_dialect_name(sequel_dialect_t dialect);
unsigned sequel_dialect_keyword_id(sequel_dialect_t dialect, const char *text, size_t length);
const sequel_operator_definition_t *sequel_dialect_operator(sequel_dialect_t dialect, const char *text, size_t length);
const sequel_clause_definition_t *sequel_dialect_clause(sequel_dialect_t dialect, unsigned keyword_id);
