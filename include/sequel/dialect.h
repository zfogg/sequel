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
} sequel_dialect_definition_t;

const sequel_dialect_definition_t *sequel_dialect_definition(sequel_dialect_t dialect);
unsigned sequel_dialect_keyword_id(sequel_dialect_t dialect, const char *text, size_t length);
