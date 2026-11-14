#include <ctype.h>
#include <string.h>

#include <sequel/dialect.h>

static const sequel_keyword_definition_t keywords[] = {
    {"SELECT", 1},   {"INSERT", 2},   {"UPDATE", 3}, {"DELETE", 4}, {"WITH", 5},    {"FROM", 6},       {"WHERE", 7},     {"GROUP", 8}, {"ORDER", 9},  {"LIMIT", 11},
    {"OFFSET", 12},  {"JOIN", 13},    {"ON", 14},    {"UNION", 15}, {"HAVING", 16}, {"RETURNING", 17}, {"VALUES", 18},   {"TRUE", 19}, {"FALSE", 20}, {"NULL", 21},
    {"EXPLAIN", 22}, {"CREATE", 23},  {"ALTER", 24}, {"DROP", 25},  {"BEGIN", 26},  {"COMMIT", 27},    {"ROLLBACK", 28}, {"SET", 29},  {"INTO", 30},  {"USING", 31},
    {"WINDOW", 32},  {"QUALIFY", 33}, {"MERGE", 34}, {"AND", 35},   {"OR", 36},     {"IN", 37},        {"BETWEEN", 38},  {"LIKE", 39}, {"IS", 40},    {"NOT", 41},
    {"CASE", 42},    {"WHEN", 43},    {"THEN", 44},  {"ELSE", 45},  {"END", 46},    {"CAST", 47},      {"EXISTS", 48},   {"OVER", 49}, {"AS", 50},    {"PREWHERE", 51},
};

static const sequel_operator_definition_t operators[] = {
    {"OR", 10, false, false}, {"AND", 20, false, false}, {"=", 30, false, false},  {"<>", 30, false, false}, {"!=", 30, false, false}, {"<", 30, false, false},
    {">", 30, false, false},  {"<=", 30, false, false},  {">=", 30, false, false}, {"+", 40, false, false},  {"-", 40, false, false},  {"*", 50, false, false},
    {"/", 50, false, false},  {"%", 50, false, false},   {"+", 60, true, true},    {"-", 60, true, true},
};

static const sequel_clause_definition_t clauses[] = {
    {"FROM", SEQUEL_KEYWORD_FROM, false},     {"WHERE", SEQUEL_KEYWORD_WHERE, false},         {"GROUP", SEQUEL_KEYWORD_GROUP, false}, {"HAVING", SEQUEL_KEYWORD_HAVING, false},
    {"WINDOW", SEQUEL_KEYWORD_WINDOW, false}, {"QUALIFY", SEQUEL_KEYWORD_QUALIFY, false},     {"ORDER", SEQUEL_KEYWORD_ORDER, false}, {"LIMIT", SEQUEL_KEYWORD_LIMIT, false},
    {"OFFSET", SEQUEL_KEYWORD_OFFSET, false}, {"RETURNING", SEQUEL_KEYWORD_RETURNING, false}, {"USING", SEQUEL_KEYWORD_USING, false}, {"SET", SEQUEL_KEYWORD_SET, false},
};

static const sequel_dialect_definition_t definitions[] = {
    [SEQUEL_GENERIC] = {.id = SEQUEL_GENERIC,
                        .name = "generic",
                        .supports_bracket_identifiers = true,
                        .supports_named_parameters = true,
                        .supports_numbered_parameters = true,
                        .keywords = keywords,
                        .keyword_count = sizeof(keywords) / sizeof(*keywords),
                        .operators = operators,
                        .operator_count = sizeof(operators) / sizeof(*operators),
                        .clauses = clauses,
                        .clause_count = sizeof(clauses) / sizeof(*clauses)},
    [SEQUEL_SQLITE] = {.id = SEQUEL_SQLITE,
                       .name = "sqlite",
                       .supports_bracket_identifiers = true,
                       .supports_named_parameters = true,
                       .supports_numbered_parameters = true,
                       .keywords = keywords,
                       .keyword_count = sizeof(keywords) / sizeof(*keywords),
                       .operators = operators,
                       .operator_count = sizeof(operators) / sizeof(*operators),
                       .clauses = clauses,
                       .clause_count = sizeof(clauses) / sizeof(*clauses)},
    [SEQUEL_POSTGRESQL] = {.id = SEQUEL_POSTGRESQL,
                           .name = "postgresql",
                           .supports_nested_block_comments = true,
                           .supports_dollar_quoted_strings = true,
                           .supports_numbered_parameters = true,
                           .keywords = keywords,
                           .keyword_count = sizeof(keywords) / sizeof(*keywords),
                           .operators = operators,
                           .operator_count = sizeof(operators) / sizeof(*operators),
                           .clauses = clauses,
                           .clause_count = sizeof(clauses) / sizeof(*clauses)},
    [SEQUEL_DUCKDB] = {.id = SEQUEL_DUCKDB,
                       .name = "duckdb",
                       .supports_numbered_parameters = true,
                       .keywords = keywords,
                       .keyword_count = sizeof(keywords) / sizeof(*keywords),
                       .operators = operators,
                       .operator_count = sizeof(operators) / sizeof(*operators),
                       .clauses = clauses,
                       .clause_count = sizeof(clauses) / sizeof(*clauses)},
    [SEQUEL_MYSQL] = {.id = SEQUEL_MYSQL,
                      .name = "mysql",
                      .supports_backtick_identifiers = true,
                      .supports_named_parameters = true,
                      .keywords = keywords,
                      .keyword_count = sizeof(keywords) / sizeof(*keywords),
                      .operators = operators,
                      .operator_count = sizeof(operators) / sizeof(*operators),
                      .clauses = clauses,
                      .clause_count = sizeof(clauses) / sizeof(*clauses)},
    [SEQUEL_CLICKHOUSE] = {.id = SEQUEL_CLICKHOUSE,
                           .name = "clickhouse",
                           .supports_backtick_identifiers = true,
                           .supports_typed_parameters = true,
                           .keywords = keywords,
                           .keyword_count = sizeof(keywords) / sizeof(*keywords),
                           .operators = operators,
                           .operator_count = sizeof(operators) / sizeof(*operators),
                           .clauses = clauses,
                           .clause_count = sizeof(clauses) / sizeof(*clauses)},
};

const sequel_dialect_definition_t *sequel_dialect_definition(sequel_dialect_t dialect) { return dialect >= SEQUEL_GENERIC && dialect <= SEQUEL_CLICKHOUSE ? &definitions[dialect] : NULL; }

const char *sequel_dialect_name(sequel_dialect_t dialect) {
  static const char *names[] = {"generic", "sqlite", "postgresql", "duckdb", "mysql", "clickhouse"};
  return dialect >= SEQUEL_GENERIC && dialect <= SEQUEL_CLICKHOUSE ? names[dialect] : "invalid";
}

const sequel_operator_definition_t *sequel_dialect_operator(sequel_dialect_t dialect, const char *text, size_t length) {
  const sequel_dialect_definition_t *definition = sequel_dialect_definition(dialect);
  if (!definition || !text)
    return NULL;
  for (size_t i = 0; i < definition->operator_count; ++i)
    if (strlen(definition->operators[i].text) == length && !memcmp(definition->operators[i].text, text, length))
      return &definition->operators[i];
  return NULL;
}

const sequel_clause_definition_t *sequel_dialect_clause(sequel_dialect_t dialect, unsigned keyword_id) {
  const sequel_dialect_definition_t *definition = sequel_dialect_definition(dialect);
  if (!definition)
    return NULL;
  for (size_t i = 0; i < definition->clause_count; ++i)
    if (definition->clauses[i].keyword_id == keyword_id)
      return &definition->clauses[i];
  return NULL;
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
