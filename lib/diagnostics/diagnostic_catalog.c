#include <stddef.h>

#include <sequel.h>

#include "diagnostic_catalog.h"

typedef struct {
  const char *name, *message, *explanation, *hint;
} sequel_diagnostic_catalog_entry_t;

static const sequel_diagnostic_catalog_entry_t diagnostic_messages[] = {
    {"none", "No lexical or delimiter issues found",
     "This checks quoting, comments, delimiters and resource limits. It does not validate SQL grammar or execute a "
     "database query.",
     "End a complete interactive statement with a semicolon."},
    {"null_input", "SQL input is NULL", "The API requires a SQL input buffer.", "Pass an empty string to represent empty input."},
    {"invalid_argument", "Invalid API argument", "The dialect, options or output buffer arguments are invalid.", "Use a supported dialect and initialize options with the provided initializer."},
    {"input_limit", "SQL exceeds the configured input limit", "Input is bounded to keep parsing and rendering resource use predictable.", "Split the input into smaller SQL documents."},
    {"invalid_utf8", "Invalid UTF-8 input", "SQL text must be valid UTF-8; this byte begins an invalid or truncated sequence.", "Decode the source in its original encoding and convert it to UTF-8."},
    {"control_character", "Unsupported control character", "NUL and terminal control characters cannot be rendered safely. Tabs, CR and LF are supported.",
     "Remove the control character or represent it with a SQL escape sequence."},
    {"token_limit", "Token limit diagnostic", "The token stream exceeded the configured storage limit.", "Use the current library to process large token streams."},
    {"nesting_limit", "Nesting exceeds 64 levels", "Delimiter, parameter-type or nested-comment depth is bounded to prevent excessive parser work.",
     "Reduce nesting or split the expression into smaller parts."},
    {"unterminated_string", "Unfinished string literal",
     "The string starts here but its closing quote or dollar tag has not been entered yet. Semicolons inside the "
     "string do not finish a statement.",
     "Add the matching closing quote or exact dollar tag; use --tolerant to highlight while editing."},
    {"unterminated_identifier", "Unfinished quoted identifier", "A double quote, backtick, bracket or dialect-specific quote opened an identifier that is not closed yet.",
     "Add the matching closing identifier delimiter."},
    {"unterminated_comment", "Unfinished block comment", "A /* comment has no matching */. Nested comments must each be closed.",
     "Add */ for each open comment; --tolerant can preview the unfinished comment."},
    {"unterminated_parameter", "Unfinished bind parameter", "A dialect-specific parameter has an opening delimiter but no closing delimiter yet.",
     "Close the parameter, for example {name:UInt64} or $name(suffix)."},
    {"invalid_parameter", "Malformed bind parameter", "The parameter does not follow the dialect's name/type syntax.", "For ClickHouse use {name:Type}, for example {user_id:UInt64}."},
    {"unmatched_close", "Unexpected closing delimiter", "This closing parenthesis or bracket has no matching opening delimiter.", "Remove it or add the corresponding opening delimiter."},
    {"unclosed_group", "Unfinished delimiter group", "This opening parenthesis or bracket has not been closed yet.",
     "Add the matching closing delimiter before formatting or completing the statement."},
    {"unsupported_payload", "ClickHouse INSERT FORMAT payload is not SQL", "Bytes after INSERT ... FORMAT belong to a data format such as CSV or JSON, not to the SQL lexer.",
     "Separate the SQL header from its payload; process the payload with the appropriate data-format tool."},
    {"unsupported_layout", "SQL layout cannot be formatted",
     "Lexical input is readable, but the formatter cannot safely lay out this construct. This is not a database syntax "
     "verdict.",
     "Check CASE/END pairs or highlight the SQL without formatting it."},
    {"output_limit", "Output buffer is too small", "The transformed text needs more space than the supplied output buffer.", "Measure with out=NULL and capacity=0, then allocate length+1 bytes."},
    {"out_of_memory", "Cannot allocate working memory", "The library could not allocate its parsing or rendering buffers.", "Reduce the input size or retry after freeing memory."},
    {"resource_limit", "Analysis resource budget exceeded", "The per-call memory, output or work budget stopped analysis.", "Increase the relevant budget or analyze a smaller document."},
    {"cancelled", "Analysis cancelled", "The caller cancelled SQL analysis before it completed.", "Retry only if analysis is still needed."},
    {"ambiguous_literal", "Ambiguous literal syntax", "Dialect or escape modes disagree about literal boundaries. Analysis omitted SQL to avoid exposing fragments.",
     "Use the correct dialect and an unambiguous supported quote form."},
    {"empty_statement", "Empty SQL statement", "A statement boundary contains no executable tokens.", "Remove the empty statement or keep it only when the target driver explicitly permits it."},
    {"multiple_statements", "Multiple SQL statements", "The input contains more than one nonempty statement.", "Split statements before execution when the driver expects one statement per call."},
    {"mixed_operations", "Mixed SQL operations", "The document combines different top-level SQL operations.", "Analyze or execute each operation as a separate statement."},
    {"select_star", "Wildcard projection", "SELECT * couples the query to the current table shape.", "Name the required columns explicitly when a stable result contract matters."},
    {"unbounded_write", "Unbounded write", "UPDATE or DELETE has no detectable WHERE clause.", "Add an explicit predicate or require an intentional full-table operation."},
    {"large_in_list", "Large IN list", "The query contains a large literal or bind list.", "Prefer a temporary table, array parameter, or bulk join for large inputs."},
    {"deep_query_nesting", "Deep query nesting", "Nested expressions exceed the conservative query-shape threshold.", "Split the query into named CTEs or smaller operations."},
    {"excessive_joins", "Many joins", "The query contains many JOIN clauses and may be difficult to maintain or optimize.",
     "Review join selectivity and consider a purpose-built intermediate relation."},
    {"repeated_predicate", "Repeated predicate", "The same predicate token sequence appears more than once.", "Factor the predicate into a CTE or a named condition when practical."},
    {"unknown_operation", "Unknown SQL operation", "No recognized top-level SQL operation was found.", "Use a supported statement form or provide an application-owned operation name."},
    {"no_referenced_table", "No referenced table", "The statement has no detectable table reference.", "Confirm that the statement is intentionally relation-free or use an explicit source."},
    {"dialect_portability", "Dialect portability warning", "The query uses syntax that may not be portable to the selected dialect.", "Verify the construct against every target database dialect."},
    {"sqlc_annotation", "Malformed SQLC annotation", "A SQLC name annotation does not have the expected directive form.", "Use a complete -- name: Operation :one|:many|:exec annotation."}};

const char *sequel_diagnostic_code_name(sequel_diagnostic_code_t code) {
  return code >= 0 && (size_t)code < sizeof(diagnostic_messages) / sizeof(diagnostic_messages[0]) ? diagnostic_messages[code].name : NULL;
}

static const sequel_diagnostic_catalog_entry_t *sequel_diagnostic_catalog_entry(sequel_diagnostic_code_t code) {
  return code >= 0 && (size_t)code < sizeof(diagnostic_messages) / sizeof(diagnostic_messages[0]) ? &diagnostic_messages[code] : NULL;
}

const char *sequel_diagnostic_catalog_message(sequel_diagnostic_code_t code) {
  const sequel_diagnostic_catalog_entry_t *entry = sequel_diagnostic_catalog_entry(code);
  return entry ? entry->message : "";
}

const char *sequel_diagnostic_catalog_explanation(sequel_diagnostic_code_t code) {
  const sequel_diagnostic_catalog_entry_t *entry = sequel_diagnostic_catalog_entry(code);
  return entry ? entry->explanation : "";
}

const char *sequel_diagnostic_catalog_hint(sequel_diagnostic_code_t code) {
  const sequel_diagnostic_catalog_entry_t *entry = sequel_diagnostic_catalog_entry(code);
  return entry ? entry->hint : "";
}

size_t sequel_diagnostic_catalog_size(void) { return sizeof(diagnostic_messages) / sizeof(diagnostic_messages[0]); }
