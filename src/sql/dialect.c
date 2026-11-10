#include <ctype.h>
#include <string.h>

#include <sequel/dialect.h>

static const sequel_keyword_definition_t keywords[] = {
    {"SELECT", 1},  {"INSERT", 2},   {"UPDATE", 3},    {"DELETE", 4},     {"WITH", 5},    {"FROM", 6},
    {"WHERE", 7},   {"GROUP", 8},    {"ORDER", 9},     {"LIMIT", 11},     {"OFFSET", 12}, {"JOIN", 13},
    {"ON", 14},     {"UNION", 15},   {"HAVING", 16},   {"RETURNING", 17}, {"VALUES", 18}, {"TRUE", 19},
    {"FALSE", 20},  {"NULL", 21},    {"EXPLAIN", 22},  {"CREATE", 23},    {"ALTER", 24},  {"DROP", 25},
    {"BEGIN", 26},  {"COMMIT", 27},  {"ROLLBACK", 28}, {"SET", 29},       {"INTO", 30},   {"USING", 31},
    {"WINDOW", 32}, {"QUALIFY", 33}, {"MERGE", 34},
};

static const sequel_dialect_definition_t definitions[] = {
    [SEQUEL_GENERIC] = {.id = SEQUEL_GENERIC,
                        .name = "generic",
                        .supports_bracket_identifiers = true,
                        .supports_named_parameters = true,
                        .supports_numbered_parameters = true,
                        .keywords = keywords,
                        .keyword_count = sizeof(keywords) / sizeof(*keywords)},
    [SEQUEL_SQLITE] = {.id = SEQUEL_SQLITE,
                       .name = "sqlite",
                       .supports_bracket_identifiers = true,
                       .supports_named_parameters = true,
                       .supports_numbered_parameters = true,
                       .keywords = keywords,
                       .keyword_count = sizeof(keywords) / sizeof(*keywords)},
    [SEQUEL_POSTGRESQL] = {.id = SEQUEL_POSTGRESQL,
                           .name = "postgresql",
                           .supports_nested_block_comments = true,
                           .supports_dollar_quoted_strings = true,
                           .supports_numbered_parameters = true,
                           .keywords = keywords,
                           .keyword_count = sizeof(keywords) / sizeof(*keywords)},
    [SEQUEL_DUCKDB] = {.id = SEQUEL_DUCKDB,
                       .name = "duckdb",
                       .supports_bracket_identifiers = true,
                       .supports_numbered_parameters = true,
                       .keywords = keywords,
                       .keyword_count = sizeof(keywords) / sizeof(*keywords)},
    [SEQUEL_MYSQL] = {.id = SEQUEL_MYSQL,
                      .name = "mysql",
                      .supports_backtick_identifiers = true,
                      .supports_named_parameters = true,
                      .keywords = keywords,
                      .keyword_count = sizeof(keywords) / sizeof(*keywords)},
    [SEQUEL_CLICKHOUSE] = {.id = SEQUEL_CLICKHOUSE,
                           .name = "clickhouse",
                           .supports_backtick_identifiers = true,
                           .supports_typed_parameters = true,
                           .keywords = keywords,
                           .keyword_count = sizeof(keywords) / sizeof(*keywords)},
};

const sequel_dialect_definition_t *sequel_dialect_definition(sequel_dialect_t dialect) {
  return dialect >= SEQUEL_GENERIC && dialect <= SEQUEL_CLICKHOUSE ? &definitions[dialect] : NULL;
}

unsigned sequel_dialect_keyword_id(sequel_dialect_t dialect, const char *text, size_t length) {
  const sequel_dialect_definition_t *definition = sequel_dialect_definition(dialect);
  if (!definition || !text)
    return 0;
  for (size_t i = 0; i < definition->keyword_count; ++i) {
    if (strlen(definition->keywords[i].name) != length)
      continue;
    size_t j = 0;
    for (; j < length && toupper((unsigned char)text[j]) == definition->keywords[i].name[j]; ++j)
      ;
    if (j == length)
      return definition->keywords[i].id;
  }
  return 0;
}
