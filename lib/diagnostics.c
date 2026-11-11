#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <sequel.h>
#include "util/text.h"
#include "util/utf8.h"

sequel_status_t sequel_syntax_diagnostic(const sequel_syntax_document_t *document, size_t issue_index,
                                         sequel_diagnostic_t *diagnostic) {
  if (!document || !diagnostic)
    return SEQUEL_ERR_NULL;
  size_t issue_count = 0;
  const sequel_parse_issue_t *issues = sequel_syntax_issues(document, &issue_count);
  if (issue_index >= issue_count)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  sequel_source_view_t source = sequel_lexed_source(sequel_syntax_lexed(document));
  const sequel_parse_issue_t *issue = &issues[issue_index];
  sequel_diagnostic_code_t code = issue->code == SEQUEL_PARSE_ISSUE_UNMATCHED_CLOSE  ? SEQUEL_DIAG_UNMATCHED_CLOSE
                                  : issue->code == SEQUEL_PARSE_ISSUE_UNCLOSED_GROUP ? SEQUEL_DIAG_UNCLOSED_GROUP
                                                                                     : SEQUEL_DIAG_INVALID_ARGUMENT;
  if (issue->code == SEQUEL_PARSE_ISSUE_UNTERMINATED_TOKEN) {
    size_t token_count = 0;
    const sequel_token_t *tokens = sequel_lexed_tokens(sequel_syntax_lexed(document), &token_count);
    code = SEQUEL_DIAG_UNTERMINATED_STRING;
    for (size_t i = 0; i < token_count; ++i) {
      if (tokens[i].range.start != issue->range.start)
        continue;
      if (tokens[i].kind == SEQUEL_TOKEN_QUOTED_IDENTIFIER)
        code = SEQUEL_DIAG_UNTERMINATED_IDENTIFIER;
      else if (tokens[i].kind == SEQUEL_TOKEN_BLOCK_COMMENT)
        code = SEQUEL_DIAG_UNTERMINATED_COMMENT;
      else if (tokens[i].kind == SEQUEL_TOKEN_PARAMETER)
        code = SEQUEL_DIAG_UNTERMINATED_PARAMETER;
      break;
    }
  }
  if (issue->code == SEQUEL_PARSE_ISSUE_UNEXPECTED_TOKEN) {
    size_t token_count = 0;
    const sequel_token_t *tokens = sequel_lexed_tokens(sequel_syntax_lexed(document), &token_count);
    for (size_t i = 0; i < token_count; ++i) {
      if (tokens[i].range.start != issue->range.start)
        continue;
      if (tokens[i].kind == SEQUEL_TOKEN_PARAMETER)
        code = SEQUEL_DIAG_INVALID_PARAMETER;
      else if (tokens[i].kind == SEQUEL_TOKEN_PUNCTUATION)
        code = SEQUEL_DIAG_UNMATCHED_CLOSE;
    }
  }
  memset(diagnostic, 0, sizeof(*diagnostic));
  diagnostic->code = code;
  diagnostic->severity = issue->code == SEQUEL_PARSE_ISSUE_UNMATCHED_CLOSE ||
                                 issue->code == SEQUEL_PARSE_ISSUE_UNEXPECTED_TOKEN ||
                                 issue->recovery == SEQUEL_RECOVERY_NONE
                             ? SEQUEL_ERROR
                             : SEQUEL_WARNING;
  diagnostic->offset = issue->range.start;
  diagnostic->end_offset = issue->range.end;
  diagnostic->incomplete =
      issue->code == SEQUEL_PARSE_ISSUE_UNCLOSED_GROUP || issue->code == SEQUEL_PARSE_ISSUE_UNTERMINATED_TOKEN;
  sequel_location_t location;
  if (sequel_source_range_position(&source, issue->range, &location) != SEQUEL_OK)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  diagnostic->line = location.start.line;
  diagnostic->column = location.start.column;
  diagnostic->end_line = location.end.line;
  diagnostic->end_column = location.end.column;
  const char *name = sequel_diagnostic_code_name(code);
  snprintf(diagnostic->message, sizeof(diagnostic->message), "%s", name ? name : "SQL parse issue");
  snprintf(diagnostic->explanation, sizeof(diagnostic->explanation),
           "The SQL parser recovered from a structural issue.");
  snprintf(diagnostic->hint, sizeof(diagnostic->hint), "Review the highlighted delimiter or token.");
  return SEQUEL_OK;
}

bool sequel_diagnostic_list_append(sequel_diagnostic_list_t *diagnostics, sequel_diagnostic_record_t record) {
  if (record.origin >= SEQUEL_DIAGNOSTIC_SEMANTIC && !record.incomplete && !record.recovered)
    record.statement_complete = 1;
  if (diagnostics->count == diagnostics->capacity) {
    size_t capacity = diagnostics->capacity ? diagnostics->capacity * 2 : 8;
    if (capacity < diagnostics->count || capacity > SIZE_MAX / sizeof(*diagnostics->records))
      return 0;
    sequel_diagnostic_record_t *records = realloc(diagnostics->records, capacity * sizeof(*records));
    if (!records)
      return 0;
    diagnostics->records = records;
    diagnostics->capacity = capacity;
  }
  diagnostics->records[diagnostics->count++] = record;
  return 1;
}

static int diagnostic_record_compare(const void *left, const void *right) {
  const sequel_diagnostic_record_t *a = left;
  const sequel_diagnostic_record_t *b = right;
  if (a->range.start != b->range.start)
    return a->range.start < b->range.start ? -1 : 1;
  if (a->range.end != b->range.end)
    return a->range.end < b->range.end ? -1 : 1;
  if (a->origin != b->origin)
    return a->origin < b->origin ? -1 : 1;
  if (a->code != b->code)
    return a->code < b->code ? -1 : 1;
  return 0;
}

void sequel_diagnostic_list_sort(sequel_diagnostic_list_t *diagnostics) {
  if (!diagnostics || diagnostics->count < 2 || !diagnostics->records)
    return;
  qsort(diagnostics->records, diagnostics->count, sizeof(*diagnostics->records), diagnostic_record_compare);
}

void sequel_diagnostic_list_free(sequel_diagnostic_list_t *diagnostics) {
  if (!diagnostics)
    return;
  free(diagnostics->records);
  *diagnostics = (sequel_diagnostic_list_t){0};
}

static sequel_diagnostic_code_t lexical_code(const sequel_token_t *token) {
  if (token->kind == SEQUEL_TOKEN_BLOCK_COMMENT && (token->flags & SEQUEL_TOKEN_FLAG_RECOVERED))
    return SEQUEL_DIAG_NESTING_LIMIT;
  if (token->kind == SEQUEL_TOKEN_BLOCK_COMMENT)
    return SEQUEL_DIAG_UNTERMINATED_COMMENT;
  if (token->kind == SEQUEL_TOKEN_QUOTED_IDENTIFIER)
    return SEQUEL_DIAG_UNTERMINATED_IDENTIFIER;
  if (token->kind == SEQUEL_TOKEN_PARAMETER)
    return SEQUEL_DIAG_UNTERMINATED_PARAMETER;
  return SEQUEL_DIAG_UNTERMINATED_STRING;
}

sequel_status_t sequel_collect_lexical_diagnostics(const sequel_lexed_document_t *lexed,
                                                   sequel_diagnostic_list_t *diagnostics) {
  if (!lexed || !diagnostics)
    return SEQUEL_ERR_NULL;
  sequel_diagnostic_list_free(diagnostics);
  size_t count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &count);
  for (size_t i = 0; i < count; ++i) {
    if (!(tokens[i].flags & SEQUEL_TOKEN_FLAG_UNTERMINATED))
      continue;
    sequel_diagnostic_record_t record = {.origin = SEQUEL_DIAGNOSTIC_LEXICAL,
                                         .code = lexical_code(&tokens[i]),
                                         .severity = SEQUEL_WARNING,
                                         .range = tokens[i].range,
                                         .statement_index = SIZE_MAX,
                                         .syntax_node = SIZE_MAX,
                                         .incomplete = 1,
                                         .recovered = 1};
    if (!sequel_diagnostic_list_append(diagnostics, record)) {
      sequel_diagnostic_list_free(diagnostics);
      return SEQUEL_ERR_NOMEM;
    }
  }
  return SEQUEL_OK;
}

static int query_has_predicate(const sequel_query_document_t *query, size_t statement_index) {
  size_t count = sequel_query_predicate_count(query);
  const sequel_predicate_info_t *predicates = sequel_query_predicates(query);
  for (size_t i = 0; i < count; ++i)
    if (predicates[i].statement_index == statement_index)
      return 1;
  return 0;
}

sequel_status_t sequel_collect_query_diagnostics(const sequel_query_document_t *query,
                                                 sequel_diagnostic_list_t *diagnostics) {
  if (!query || !diagnostics)
    return SEQUEL_ERR_NULL;
  sequel_diagnostic_list_free(diagnostics);
  size_t projection_count = sequel_query_projection_count(query);
  const sequel_projection_info_t *projections = sequel_query_projections(query);
  for (size_t i = 0; i < projection_count; ++i) {
    if (!projections[i].wildcard)
      continue;
    sequel_diagnostic_record_t record = {.origin = SEQUEL_DIAGNOSTIC_POLICY,
                                         .code = SEQUEL_DIAG_SELECT_STAR,
                                         .severity = SEQUEL_WARNING,
                                         .range = projections[i].range,
                                         .statement_index = projections[i].statement_index,
                                         .syntax_node = projections[i].expression_node,
                                         .query_item = i};
    if (!sequel_diagnostic_list_append(diagnostics, record))
      goto no_memory;
  }
  size_t statement_count = sequel_query_statement_count(query);
  const sequel_statement_info_t *statements = sequel_query_statements(query);
  if (statement_count > 1) {
    sequel_diagnostic_record_t record = {.origin = SEQUEL_DIAGNOSTIC_POLICY,
                                         .code = SEQUEL_DIAG_MULTIPLE_STATEMENTS,
                                         .severity = SEQUEL_WARNING,
                                         .range = statements[1].range,
                                         .statement_index = 1,
                                         .syntax_node = statements[1].syntax_node};
    if (!sequel_diagnostic_list_append(diagnostics, record))
      goto no_memory;
  }
  for (size_t i = 0; i < statement_count; ++i) {
    if (statements[i].operation == SEQUEL_OPERATION_UNKNOWN) {
      sequel_diagnostic_record_t record = {.origin = SEQUEL_DIAGNOSTIC_SEMANTIC,
                                           .code = SEQUEL_DIAG_UNKNOWN_OPERATION,
                                           .severity = SEQUEL_WARNING,
                                           .range = statements[i].range,
                                           .statement_index = i,
                                           .syntax_node = statements[i].syntax_node};
      if (!sequel_diagnostic_list_append(diagnostics, record))
        goto no_memory;
    }
    if ((statements[i].operation == SEQUEL_OPERATION_UPDATE || statements[i].operation == SEQUEL_OPERATION_DELETE) &&
        !query_has_predicate(query, i)) {
      sequel_diagnostic_record_t record = {.origin = SEQUEL_DIAGNOSTIC_POLICY,
                                           .code = SEQUEL_DIAG_UNBOUNDED_WRITE,
                                           .severity = SEQUEL_ERROR,
                                           .range = statements[i].range,
                                           .statement_index = i,
                                           .syntax_node = statements[i].syntax_node};
      if (!sequel_diagnostic_list_append(diagnostics, record))
        goto no_memory;
    }
  }
  size_t relation_count = sequel_query_relation_count(query);
  const sequel_relation_info_t *relations = sequel_query_relations(query);
  for (size_t i = 0; i < statement_count; ++i) {
    if (statements[i].operation == SEQUEL_OPERATION_TRANSACTION || statements[i].operation == SEQUEL_OPERATION_UNKNOWN)
      continue;
    int has_relation = 0;
    for (size_t j = 0; j < relation_count; ++j)
      has_relation |= relations[j].statement_index == i;
    if (!has_relation) {
      sequel_diagnostic_record_t record = {.origin = SEQUEL_DIAGNOSTIC_SEMANTIC,
                                           .code = SEQUEL_DIAG_NO_REFERENCED_TABLE,
                                           .severity = SEQUEL_NOTE,
                                           .range = statements[i].range,
                                           .statement_index = i,
                                           .syntax_node = statements[i].syntax_node};
      if (!sequel_diagnostic_list_append(diagnostics, record))
        goto no_memory;
    }
  }
  if (sequel_query_join_count(query) > 8) {
    const sequel_join_info_t *joins = sequel_query_joins(query);
    sequel_diagnostic_record_t record = {.origin = SEQUEL_DIAGNOSTIC_POLICY,
                                         .code = SEQUEL_DIAG_EXCESSIVE_JOINS,
                                         .severity = SEQUEL_WARNING,
                                         .range = joins[8].range,
                                         .statement_index = SIZE_MAX,
                                         .syntax_node = joins[8].condition_node,
                                         .query_item = 8};
    if (!sequel_diagnostic_list_append(diagnostics, record))
      goto no_memory;
  }
  return SEQUEL_OK;
no_memory:
  sequel_diagnostic_list_free(diagnostics);
  return SEQUEL_ERR_NOMEM;
}

static sequel_status_t collect_query_diagnostics_by_origin(const sequel_query_document_t *query,
                                                           sequel_diagnostic_origin_t origin,
                                                           sequel_diagnostic_list_t *diagnostics) {
  if (!query || !diagnostics)
    return SEQUEL_ERR_NULL;
  sequel_diagnostic_list_t all = {0};
  sequel_status_t status = sequel_collect_query_diagnostics(query, &all);
  if (status != SEQUEL_OK)
    return status;
  sequel_diagnostic_list_free(diagnostics);
  for (size_t i = 0; i < all.count; ++i) {
    if (all.records[i].origin != origin)
      continue;
    if (!sequel_diagnostic_list_append(diagnostics, all.records[i])) {
      sequel_diagnostic_list_free(&all);
      sequel_diagnostic_list_free(diagnostics);
      return SEQUEL_ERR_NOMEM;
    }
  }
  sequel_diagnostic_list_free(&all);
  return SEQUEL_OK;
}

sequel_status_t sequel_collect_semantic_diagnostics(const sequel_query_document_t *query,
                                                    sequel_diagnostic_list_t *diagnostics) {
  return collect_query_diagnostics_by_origin(query, SEQUEL_DIAGNOSTIC_SEMANTIC, diagnostics);
}

sequel_status_t sequel_collect_policy_diagnostics(const sequel_query_document_t *query,
                                                  sequel_diagnostic_list_t *diagnostics) {
  return collect_query_diagnostics_by_origin(query, SEQUEL_DIAGNOSTIC_POLICY, diagnostics);
}

/* The scalar diagnostic API shares this canonical collection implementation.
 * It is compiled only in the engine because it uses the engine's resource
 * limits and work accounting. */
static const struct {
  const char *name, *message, *explanation, *hint;
} diagnostic_messages[] = {
    {"none", "No lexical or delimiter issues found",
     "This checks quoting, comments, delimiters and resource limits. It does not validate SQL grammar or execute a "
     "database query.",
     "End a complete interactive statement with a semicolon."},
    {"null_input", "SQL input is NULL", "The API requires a SQL input buffer.",
     "Pass an empty string to represent empty input."},
    {"invalid_argument", "Invalid API argument", "The dialect, options or output buffer arguments are invalid.",
     "Use a supported dialect and initialize options with the provided initializer."},
    {"input_limit", "SQL exceeds the configured input limit",
     "Input is bounded to keep parsing and rendering resource use predictable.",
     "Split the input into smaller SQL documents."},
    {"invalid_utf8", "Invalid UTF-8 input",
     "SQL text must be valid UTF-8; this byte begins an invalid or truncated sequence.",
     "Decode the source in its original encoding and convert it to UTF-8."},
    {"control_character", "Unsupported control character",
     "NUL and terminal control characters cannot be rendered safely. Tabs, CR and LF are supported.",
     "Remove the control character or represent it with a SQL escape sequence."},
    {"token_limit", "Token limit diagnostic", "The token stream exceeded the configured storage limit.",
     "Use the current library to process large token streams."},
    {"nesting_limit", "Nesting exceeds 64 levels",
     "Delimiter, parameter-type or nested-comment depth is bounded to prevent excessive parser work.",
     "Reduce nesting or split the expression into smaller parts."},
    {"unterminated_string", "Unfinished string literal",
     "The string starts here but its closing quote or dollar tag has not been entered yet. Semicolons inside the "
     "string do not finish a statement.",
     "Add the matching closing quote or exact dollar tag; use --tolerant to highlight while editing."},
    {"unterminated_identifier", "Unfinished quoted identifier",
     "A double quote, backtick, bracket or dialect-specific quote opened an identifier that is not closed yet.",
     "Add the matching closing identifier delimiter."},
    {"unterminated_comment", "Unfinished block comment",
     "A /* comment has no matching */. Nested comments must each be closed.",
     "Add */ for each open comment; --tolerant can preview the unfinished comment."},
    {"unterminated_parameter", "Unfinished bind parameter",
     "A dialect-specific parameter has an opening delimiter but no closing delimiter yet.",
     "Close the parameter, for example {name:UInt64} or $name(suffix)."},
    {"invalid_parameter", "Malformed bind parameter", "The parameter does not follow the dialect's name/type syntax.",
     "For ClickHouse use {name:Type}, for example {user_id:UInt64}."},
    {"unmatched_close", "Unexpected closing delimiter",
     "This closing parenthesis or bracket has no matching opening delimiter.",
     "Remove it or add the corresponding opening delimiter."},
    {"unclosed_group", "Unfinished delimiter group", "This opening parenthesis or bracket has not been closed yet.",
     "Add the matching closing delimiter before formatting or completing the statement."},
    {"unsupported_payload", "ClickHouse INSERT FORMAT payload is not SQL",
     "Bytes after INSERT ... FORMAT belong to a data format such as CSV or JSON, not to the SQL lexer.",
     "Separate the SQL header from its payload; process the payload with the appropriate data-format tool."},
    {"unsupported_layout", "SQL layout cannot be formatted",
     "Lexical input is readable, but the formatter cannot safely lay out this construct. This is not a database syntax "
     "verdict.",
     "Check CASE/END pairs or highlight the SQL without formatting it."},
    {"output_limit", "Output buffer is too small",
     "The transformed text needs more space than the supplied output buffer.",
     "Measure with out=NULL and capacity=0, then allocate length+1 bytes."},
    {"out_of_memory", "Cannot allocate working memory",
     "The library could not allocate its parsing or rendering buffers.",
     "Reduce the input size or retry after freeing memory."},
    {"resource_limit", "Analysis resource budget exceeded",
     "The per-call memory, output or work budget stopped analysis.",
     "Increase the relevant budget or analyze a smaller document."},
    {"cancelled", "Analysis cancelled", "The caller cancelled SQL analysis before it completed.",
     "Retry only if analysis is still needed."},
    {"ambiguous_literal", "Ambiguous literal syntax",
     "Dialect or escape modes disagree about literal boundaries. Analysis omitted SQL to avoid exposing fragments.",
     "Use the correct dialect and an unambiguous supported quote form."},
    {"empty_statement", "Empty SQL statement", "A statement boundary contains no executable tokens.",
     "Remove the empty statement or keep it only when the target driver explicitly permits it."},
    {"multiple_statements", "Multiple SQL statements", "The input contains more than one nonempty statement.",
     "Split statements before execution when the driver expects one statement per call."},
    {"mixed_operations", "Mixed SQL operations", "The document combines different top-level SQL operations.",
     "Analyze or execute each operation as a separate statement."},
    {"select_star", "Wildcard projection", "SELECT * couples the query to the current table shape.",
     "Name the required columns explicitly when a stable result contract matters."},
    {"unbounded_write", "Unbounded write", "UPDATE or DELETE has no detectable WHERE clause.",
     "Add an explicit predicate or require an intentional full-table operation."},
    {"large_in_list", "Large IN list", "The query contains a large literal or bind list.",
     "Prefer a temporary table, array parameter, or bulk join for large inputs."},
    {"deep_query_nesting", "Deep query nesting", "Nested expressions exceed the conservative query-shape threshold.",
     "Split the query into named CTEs or smaller operations."},
    {"excessive_joins", "Many joins",
     "The query contains many JOIN clauses and may be difficult to maintain or optimize.",
     "Review join selectivity and consider a purpose-built intermediate relation."},
    {"repeated_predicate", "Repeated predicate", "The same predicate token sequence appears more than once.",
     "Factor the predicate into a CTE or a named condition when practical."},
    {"unknown_operation", "Unknown SQL operation", "No recognized top-level SQL operation was found.",
     "Use a supported statement form or provide an application-owned operation name."},
    {"no_referenced_table", "No referenced table", "The statement has no detectable table reference.",
     "Confirm that the statement is intentionally relation-free or use an explicit source."},
    {"dialect_portability", "Dialect portability warning",
     "The query uses syntax that may not be portable to the selected dialect.",
     "Verify the construct against every target database dialect."},
    {"sqlc_annotation", "Malformed SQLC annotation",
     "A SQLC name annotation does not have the expected directive form.",
     "Use a complete -- name: Operation :one|:many|:exec annotation."}};

const char *sequel_diagnostic_code_name(sequel_diagnostic_code_t code) {
  return code >= 0 && (size_t)code < sizeof(diagnostic_messages) / sizeof(diagnostic_messages[0])
             ? diagnostic_messages[code].name
             : NULL;
}

static void diagnostic_position(const char *sql, size_t n, size_t offset, size_t *line, size_t *column) {
  *line = *column = 1;
  if (!sql)
    return;
  if (offset > n)
    offset = n;
  for (size_t i = 0; i < offset; ++i) {
    unsigned char c = (unsigned char)sql[i];
    if (c == '\r') {
      ++*line;
      *column = 1;
      if (i + 1 < offset && sql[i + 1] == '\n')
        ++i;
    } else if (c == '\n') {
      ++*line;
      *column = 1;
    } else if ((c & 0xc0) != 0x80)
      ++*column;
  }
}

void diagnostic_report(const char *sql, size_t n, sequel_diagnostic_t *out, sequel_diagnostic_code_t code, size_t start,
                       size_t end) {
  if (!out)
    return;
  memset(out, 0, sizeof(*out));
  out->code = code;
  out->incomplete = code >= SEQUEL_DIAG_UNTERMINATED_STRING && code <= SEQUEL_DIAG_UNTERMINATED_PARAMETER;
  out->incomplete |= code == SEQUEL_DIAG_UNCLOSED_GROUP;
  out->severity = code == SEQUEL_DIAG_NONE              ? SEQUEL_NOTE
                  : code >= SEQUEL_DIAG_EMPTY_STATEMENT ? SEQUEL_WARNING
                  : out->incomplete                     ? SEQUEL_WARNING
                                                        : SEQUEL_ERROR;
  out->offset = start;
  out->end_offset = end;
  diagnostic_position(sql, n, start, &out->line, &out->column);
  diagnostic_position(sql, n, end, &out->end_line, &out->end_column);
  if (sequel_diagnostic_code_name(code)) {
    snprintf(out->message, sizeof(out->message), "%s", diagnostic_messages[code].message);
    snprintf(out->explanation, sizeof(out->explanation), "%s", diagnostic_messages[code].explanation);
    snprintf(out->hint, sizeof(out->hint), "%s", diagnostic_messages[code].hint);
  }
}

sequel_status_t diagnostic_input(const char *sql, size_t n, sequel_dialect_t dialect, sequel_diagnostic_t *out) {
  diagnostic_report(sql, 0, out, SEQUEL_DIAG_NONE, 0, 0);
  if (!sql) {
    diagnostic_report(NULL, 0, out, SEQUEL_DIAG_NULL_INPUT, 0, 0);
    return SEQUEL_ERR_NULL;
  }
  if (dialect < SEQUEL_GENERIC || dialect > SEQUEL_CLICKHOUSE) {
    diagnostic_report(sql, n, out, SEQUEL_DIAG_INVALID_ARGUMENT, 0, 0);
    return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  if (n > SEQUEL_INPUT_MAX) {
    diagnostic_report(sql, SEQUEL_INPUT_MAX, out, SEQUEL_DIAG_INPUT_LIMIT, SEQUEL_INPUT_MAX, SEQUEL_INPUT_MAX);
    return SEQUEL_ERR_OVERFLOW;
  }
  for (size_t i = 0; i < n;) {
    unsigned char c = (unsigned char)sql[i];
    size_t width = c < 0x80                 ? 1
                   : c >= 0xc2 && c <= 0xdf ? 2
                   : c >= 0xe0 && c <= 0xef ? 3
                   : c >= 0xf0 && c <= 0xf4 ? 4
                                            : 0;
    if (!width || width > n - i || !sequel_utf8_valid(sql + i, width)) {
      diagnostic_report(sql, n, out, SEQUEL_DIAG_INVALID_UTF8, i, i + 1);
      return SEQUEL_ERR_INVALID_ARGUMENT;
    }
    if ((c < 32 && c != '\t' && c != '\r' && c != '\n') || c == 127 ||
        (width == 2 && c == 0xc2 && (unsigned char)sql[i + 1] <= 0x9f)) {
      diagnostic_report(sql, n, out, SEQUEL_DIAG_CONTROL_CHARACTER, i, i + width);
      return SEQUEL_ERR_INVALID_ARGUMENT;
    }
    i += width;
  }
  return SEQUEL_OK;
}

size_t sequel_diagnostic_catalog_count(void) { return sizeof(diagnostic_messages) / sizeof(diagnostic_messages[0]); }

sequel_status_t sequel_diagnose_n(const char *sql, size_t n, sequel_dialect_t dialect, sequel_diagnostic_t *out) {
  if (!out)
    return SEQUEL_ERR_NULL;
  sequel_status_t status = diagnostic_input(sql, n, dialect, out);
  if (status != SEQUEL_OK)
    return status;
  bool only_trivia = true;
  for (size_t i = 0; i < n; ++i) {
    if (!isspace((unsigned char)sql[i])) {
      only_trivia = false;
      break;
    }
  }
  if (only_trivia)
    return SEQUEL_OK;
  if (dialect == SEQUEL_CLICKHOUSE && sequel_text_find(sql, n, "INSERT") && sequel_text_find(sql, n, " FORMAT ")) {
    const char *format = sequel_text_find(sql, n, " FORMAT ") + 1;
    diagnostic_report(sql, n, out, SEQUEL_DIAG_UNSUPPORTED_PAYLOAD, (size_t)(format - sql), (size_t)(format - sql) + 6);
    return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  sequel_parse_options_t options;
  sequel_parse_options_init(&options);
  options.dialect = dialect;
  options.retain_comments = true;
  sequel_source_view_t source = {sql, n};
  sequel_syntax_document_t *document = NULL;
  status = sequel_parse_sql(&source, &options, NULL, &document);
  if (status != SEQUEL_OK)
    return status;
  size_t issue_count = 0;
  const sequel_parse_issue_t *issues = sequel_syntax_issues(document, &issue_count);
  const sequel_lexed_document_t *lexed = sequel_syntax_lexed(document);
  sequel_diagnostic_list_t lexical_diagnostics = {0};
  status = sequel_collect_lexical_diagnostics(lexed, &lexical_diagnostics);
  if (status != SEQUEL_OK) {
    sequel_syntax_document_free(document);
    return status;
  }
  for (size_t i = 0; i < lexical_diagnostics.count; ++i) {
    const sequel_diagnostic_record_t *record = &lexical_diagnostics.records[i];
    if (record->code == SEQUEL_DIAG_NESTING_LIMIT) {
      diagnostic_report(sql, n, out, record->code, record->range.start, record->range.end);
      sequel_diagnostic_list_free(&lexical_diagnostics);
      sequel_syntax_document_free(document);
      return SEQUEL_ERR_OVERFLOW;
    }
    diagnostic_report(sql, n, out, record->code, record->range.start, record->range.end);
    sequel_diagnostic_list_free(&lexical_diagnostics);
    sequel_syntax_document_free(document);
    return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  sequel_diagnostic_list_free(&lexical_diagnostics);
  if (issue_count) {
    if (issues[0].recovery == SEQUEL_RECOVERY_OPAQUE_REGION) {
      diagnostic_report(sql, n, out, SEQUEL_DIAG_NESTING_LIMIT, issues[0].range.start, issues[0].range.end);
      sequel_syntax_document_free(document);
      return SEQUEL_ERR_OVERFLOW;
    }
    status = sequel_syntax_diagnostic(document, 0, out);
    sequel_syntax_document_free(document);
    return status == SEQUEL_OK ? SEQUEL_ERR_INVALID_ARGUMENT : status;
  }
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  for (size_t i = token_count; i > 0; --i) {
    if (tokens[i - 1].kind == SEQUEL_TOKEN_EOF || tokens[i - 1].kind == SEQUEL_TOKEN_WHITESPACE ||
        tokens[i - 1].kind == SEQUEL_TOKEN_LINE_COMMENT || tokens[i - 1].kind == SEQUEL_TOKEN_BLOCK_COMMENT)
      continue;
    out->statement_complete =
        tokens[i - 1].kind == SEQUEL_TOKEN_PUNCTUATION && source.data[tokens[i - 1].range.start] == ';';
    break;
  }
  sequel_syntax_document_free(document);
  return SEQUEL_OK;
}

sequel_status_t sequel_diagnose(const char *sql, sequel_dialect_t dialect, sequel_diagnostic_t *out) {
  size_t n = 0;
  if (sql)
    while (n <= SEQUEL_INPUT_MAX && sql[n])
      ++n;
  return sequel_diagnose_n(sql, n, dialect, out);
}

sequel_status_t sequel_format_ex(const char *sql, sequel_dialect_t dialect, char *out, size_t capacity, size_t *length,
                                 sequel_diagnostic_t *diagnostic) {
  sequel_diagnostic_t local;
  if (!diagnostic)
    diagnostic = &local;
  if (out && capacity)
    *out = 0;
  if (length)
    *length = 0;
  sequel_status_t status = sequel_diagnose(sql, dialect, diagnostic);
  if (status != SEQUEL_OK)
    return status;
  status = sequel_format(sql, dialect, out, capacity, length);
  if (status != SEQUEL_OK) {
    sequel_diagnostic_code_t code = status == SEQUEL_ERR_NOMEM      ? SEQUEL_DIAG_OUT_OF_MEMORY
                                    : status == SEQUEL_ERR_OVERFLOW ? SEQUEL_DIAG_OUTPUT_LIMIT
                                    : !length || (!out && capacity) || (out && !capacity)
                                        ? SEQUEL_DIAG_INVALID_ARGUMENT
                                        : SEQUEL_DIAG_UNSUPPORTED_LAYOUT;
    diagnostic_report(sql, strlen(sql), diagnostic, code, 0, 0);
  }
  return status;
}

sequel_status_t sequel_collect_syntax_diagnostics(const sequel_syntax_document_t *syntax,
                                                  sequel_diagnostic_list_t *diagnostics) {
  if (!syntax || !diagnostics)
    return SEQUEL_ERR_NULL;
  sequel_diagnostic_list_free(diagnostics);
  size_t count = 0;
  const sequel_parse_issue_t *issues = sequel_syntax_issues(syntax, &count);
  for (size_t i = 0; i < count; ++i) {
    sequel_diagnostic_record_t record = {
        .origin = SEQUEL_DIAGNOSTIC_SYNTAX,
        .code = issues[i].code == SEQUEL_PARSE_ISSUE_UNMATCHED_CLOSE  ? SEQUEL_DIAG_UNMATCHED_CLOSE
                : issues[i].code == SEQUEL_PARSE_ISSUE_UNCLOSED_GROUP ? SEQUEL_DIAG_UNCLOSED_GROUP
                                                                      : SEQUEL_DIAG_INVALID_ARGUMENT,
        .severity = issues[i].code == SEQUEL_PARSE_ISSUE_UNMATCHED_CLOSE ||
                            issues[i].code == SEQUEL_PARSE_ISSUE_UNEXPECTED_TOKEN
                        ? SEQUEL_ERROR
                        : SEQUEL_WARNING,
        .range = issues[i].range,
        .statement_index = issues[i].statement_index,
        .syntax_node = SIZE_MAX,
        .incomplete = issues[i].code == SEQUEL_PARSE_ISSUE_UNCLOSED_GROUP ||
                      issues[i].code == SEQUEL_PARSE_ISSUE_UNTERMINATED_TOKEN,
        .recovered = issues[i].recovery != SEQUEL_RECOVERY_NONE};
    if (!sequel_diagnostic_list_append(diagnostics, record))
      goto no_memory;
  }
  return SEQUEL_OK;
no_memory:
  sequel_diagnostic_list_free(diagnostics);
  return SEQUEL_ERR_NOMEM;
}
