#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sequel.h>
#include "resources.h"
#include "util/text.h"
#include "util/utf8.h"
#include "diagnostic_catalog.h"

void sequel_diagnostics_position(const char *sql, size_t n, size_t offset, size_t *line, size_t *column);
void sequel_diagnostics_report(const char *sql, size_t n, sequel_diagnostic_t *out, sequel_diagnostic_code_t code, size_t start, size_t end);

sequel_status_t sequel_syntax_diagnostic(const sequel_syntax_document_t *document, size_t issue_index, sequel_diagnostic_t *diagnostic) {
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
  sequel_diagnostics_report(source.data, source.length, diagnostic, code, issue->range.start, issue->range.end);
  diagnostic->severity =
      issue->code == SEQUEL_PARSE_ISSUE_UNMATCHED_CLOSE || issue->code == SEQUEL_PARSE_ISSUE_UNEXPECTED_TOKEN || issue->recovery == SEQUEL_RECOVERY_NONE ? SEQUEL_ERROR : SEQUEL_WARNING;
  if (code >= SEQUEL_DIAG_UNTERMINATED_STRING && code <= SEQUEL_DIAG_UNTERMINATED_PARAMETER)
    diagnostic->end_offset = source.length;
  diagnostic->incomplete = issue->code == SEQUEL_PARSE_ISSUE_UNCLOSED_GROUP || issue->code == SEQUEL_PARSE_ISSUE_UNTERMINATED_TOKEN;
  sequel_location_t location;
  if (sequel_source_view_range_position(&source, issue->range, &location) != SEQUEL_OK)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  sequel_diagnostics_position(source.data, source.length, diagnostic->end_offset, &diagnostic->end_line, &diagnostic->end_column);
  return SEQUEL_OK;
}

bool sequel_diagnostic_list_append(sequel_diagnostic_list_t *diagnostics, sequel_diagnostic_record_t record) {
  if (!record.message[0])
    snprintf(record.message, sizeof(record.message), "%s", sequel_diagnostic_code_name(record.code));
  if (!record.explanation[0])
    snprintf(record.explanation, sizeof(record.explanation), "The SQL analysis pipeline reported this issue.");
  if (!record.hint[0])
    snprintf(record.hint, sizeof(record.hint), "Review the highlighted source range.");
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

static int sequel_diagnostics_record_compare(const void *left, const void *right) {
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
  qsort(diagnostics->records, diagnostics->count, sizeof(*diagnostics->records), sequel_diagnostics_record_compare);
}

void sequel_diagnostic_list_free(sequel_diagnostic_list_t *diagnostics) {
  if (!diagnostics)
    return;
  free(diagnostics->records);
  *diagnostics = (sequel_diagnostic_list_t){0};
}

sequel_status_t sequel_collect_lexical_diagnostics(const sequel_lexed_document_t *lexed, sequel_diagnostic_list_t *diagnostics) {
  if (!lexed || !diagnostics)
    return SEQUEL_ERR_NULL;
  sequel_diagnostic_list_free(diagnostics);
  size_t count = sequel_lexed_diagnostic_count(lexed);
  const sequel_diagnostic_record_t *records = sequel_lexed_diagnostics(lexed);
  for (size_t i = 0; i < count; ++i) {
    if (!sequel_diagnostic_list_append(diagnostics, records[i])) {
      sequel_diagnostic_list_free(diagnostics);
      return SEQUEL_ERR_NOMEM;
    }
  }
  return SEQUEL_OK;
}

static int sequel_diagnostics_query_has_predicate(const sequel_query_document_t *query, size_t statement_index) {
  size_t count = sequel_query_predicate_count(query);
  const sequel_predicate_info_t *predicates = sequel_query_predicates(query);
  for (size_t i = 0; i < count; ++i)
    if (predicates[i].statement_index == statement_index)
      return 1;
  return 0;
}

sequel_status_t sequel_collect_query_diagnostics(const sequel_query_document_t *query, sequel_diagnostic_list_t *diagnostics) {
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
    if ((statements[i].operation == SEQUEL_OPERATION_UPDATE || statements[i].operation == SEQUEL_OPERATION_DELETE) && !sequel_diagnostics_query_has_predicate(query, i)) {
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

sequel_status_t sequel_collect_document_diagnostics(const sequel_lexed_document_t *lexed, const sequel_syntax_document_t *syntax, const sequel_query_document_t *query,
                                                    sequel_diagnostic_list_t *diagnostics) {
  if (!diagnostics)
    return SEQUEL_ERR_NULL;
  sequel_diagnostic_list_free(diagnostics);
  sequel_diagnostic_list_t current = {0};
  sequel_status_t status = SEQUEL_OK;
  if (lexed)
    status = sequel_collect_lexical_diagnostics(lexed, &current);
  if (status == SEQUEL_OK) {
    sequel_diagnostic_list_t stage = {0};
    if (syntax)
      status = sequel_collect_syntax_diagnostics(syntax, &stage);
    if (status == SEQUEL_OK) {
      for (size_t i = 0; i < stage.count; ++i)
        if (!sequel_diagnostic_list_append(&current, stage.records[i]))
          status = SEQUEL_ERR_NOMEM;
    }
    sequel_diagnostic_list_free(&stage);
  }
  if (status == SEQUEL_OK) {
    sequel_diagnostic_list_t stage = {0};
    if (query)
      status = sequel_collect_query_diagnostics(query, &stage);
    if (status == SEQUEL_OK) {
      for (size_t i = 0; i < stage.count; ++i)
        if (!sequel_diagnostic_list_append(&current, stage.records[i]))
          status = SEQUEL_ERR_NOMEM;
    }
    sequel_diagnostic_list_free(&stage);
  }
  if (status != SEQUEL_OK) {
    sequel_diagnostic_list_free(&current);
    return status;
  }
  *diagnostics = current;
  sequel_diagnostic_list_sort(diagnostics);
  return SEQUEL_OK;
}

static sequel_status_t sequel_diagnostics_collect_query_by_origin(const sequel_query_document_t *query, sequel_diagnostic_origin_t origin, sequel_diagnostic_list_t *diagnostics) {
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

sequel_status_t sequel_collect_semantic_diagnostics(const sequel_query_document_t *query, sequel_diagnostic_list_t *diagnostics) {
  return sequel_diagnostics_collect_query_by_origin(query, SEQUEL_DIAGNOSTIC_SEMANTIC, diagnostics);
}

sequel_status_t sequel_collect_policy_diagnostics(const sequel_query_document_t *query, sequel_diagnostic_list_t *diagnostics) {
  return sequel_diagnostics_collect_query_by_origin(query, SEQUEL_DIAGNOSTIC_POLICY, diagnostics);
}

sequel_status_t sequel_collect_portability_diagnostics(const sequel_syntax_document_t *syntax, sequel_dialect_t target, sequel_diagnostic_list_t *diagnostics) {
  if (!syntax || !diagnostics)
    return SEQUEL_ERR_NULL;
  sequel_diagnostic_list_free(diagnostics);
  const sequel_lexed_document_t *lexed = sequel_syntax_lexed(syntax);
  if (!lexed)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (sequel_lexed_dialect(lexed) == target)
    return SEQUEL_OK;
  const sequel_dialect_definition_t *definition = sequel_dialect_definition(target);
  if (!definition)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  size_t count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &count);
  for (size_t i = 0; i < count; ++i) {
    if (!(tokens[i].flags & SEQUEL_TOKEN_FLAG_DIALECT_SPECIFIC))
      continue;
    sequel_diagnostic_record_t record = {
        .origin = SEQUEL_DIAGNOSTIC_PORTABILITY, .code = SEQUEL_DIAG_DIALECT_PORTABILITY, .severity = SEQUEL_WARNING, .range = tokens[i].range, .statement_index = SIZE_MAX, .syntax_node = SIZE_MAX};
    snprintf(record.message, sizeof(record.message), "syntax may not be portable to %s", definition->name);
    snprintf(record.explanation, sizeof(record.explanation), "The source uses syntax marked as specific to the input dialect, which may not be accepted by %s.", definition->name);
    snprintf(record.hint, sizeof(record.hint), "Rewrite the construct using syntax supported by %s or keep the target dialect.", definition->name);
    if (!sequel_diagnostic_list_append(diagnostics, record)) {
      sequel_diagnostic_list_free(diagnostics);
      return SEQUEL_ERR_NOMEM;
    }
  }
  return SEQUEL_OK;
}

/* The scalar diagnostic API shares this canonical collection implementation.
 * It is compiled only in the engine because it uses the engine's resource
 * limits and work accounting. */
static void sequel_diagnostics_position(const char *sql, size_t n, size_t offset, size_t *line, size_t *column) {
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

void sequel_diagnostics_report(const char *sql, size_t n, sequel_diagnostic_t *out, sequel_diagnostic_code_t code, size_t start, size_t end) {
  if (!out)
    return;
  memset(out, 0, sizeof(*out));
  out->code = code;
  out->incomplete = code >= SEQUEL_DIAG_UNTERMINATED_STRING && code <= SEQUEL_DIAG_UNTERMINATED_PARAMETER;
  out->incomplete |= code == SEQUEL_DIAG_UNCLOSED_GROUP;
  out->severity = code == SEQUEL_DIAG_NONE ? SEQUEL_NOTE : code >= SEQUEL_DIAG_EMPTY_STATEMENT ? SEQUEL_WARNING : out->incomplete ? SEQUEL_WARNING : SEQUEL_ERROR;
  out->offset = start;
  out->end_offset = end;
  sequel_diagnostics_position(sql, n, start, &out->line, &out->column);
  sequel_diagnostics_position(sql, n, end, &out->end_line, &out->end_column);
  if (sequel_diagnostic_code_name(code)) {
    snprintf(out->message, sizeof(out->message), "%s", sequel_diagnostic_catalog_message(code));
    snprintf(out->explanation, sizeof(out->explanation), "%s", sequel_diagnostic_catalog_explanation(code));
    snprintf(out->hint, sizeof(out->hint), "%s", sequel_diagnostic_catalog_hint(code));
  }
}

sequel_status_t sequel_diagnostics_validate_input(const char *sql, size_t n, sequel_dialect_t dialect, sequel_diagnostic_t *out) {
  sequel_diagnostics_report(sql, 0, out, SEQUEL_DIAG_NONE, 0, 0);
  if (!sql) {
    sequel_diagnostics_report(NULL, 0, out, SEQUEL_DIAG_NULL_INPUT, 0, 0);
    return SEQUEL_ERR_NULL;
  }
  if (dialect < SEQUEL_GENERIC || dialect > SEQUEL_CLICKHOUSE) {
    sequel_diagnostics_report(sql, n, out, SEQUEL_DIAG_INVALID_ARGUMENT, 0, 0);
    return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  if (n > SEQUEL_INPUT_MAX) {
    sequel_diagnostics_report(sql, SEQUEL_INPUT_MAX, out, SEQUEL_DIAG_INPUT_LIMIT, SEQUEL_INPUT_MAX, SEQUEL_INPUT_MAX);
    return SEQUEL_ERR_OVERFLOW;
  }
  for (size_t i = 0; i < n;) {
    unsigned char c = (unsigned char)sql[i];
    size_t width = c < 0x80 ? 1 : c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
    if (!width || width > n - i || !sequel_utf8_valid(sql + i, width)) {
      sequel_diagnostics_report(sql, n, out, SEQUEL_DIAG_INVALID_UTF8, i, i + 1);
      return SEQUEL_ERR_INVALID_ARGUMENT;
    }
    if ((c < 32 && c != '\t' && c != '\r' && c != '\n') || c == 127 || (width == 2 && c == 0xc2 && (unsigned char)sql[i + 1] <= 0x9f)) {
      sequel_diagnostics_report(sql, n, out, SEQUEL_DIAG_CONTROL_CHARACTER, i, i + width);
      return SEQUEL_ERR_INVALID_ARGUMENT;
    }
    i += width;
  }
  return SEQUEL_OK;
}

size_t sequel_diagnostic_catalog_count(void) { return sequel_diagnostic_catalog_size(); }

sequel_status_t sequel_diagnostic_project(const char *source, size_t source_length, const sequel_diagnostic_record_t *record, sequel_diagnostic_t *out) {
  if (!record || !out)
    return SEQUEL_ERR_NULL;
  sequel_diagnostics_report(source, source_length, out, record->code, record->range.start, record->range.end);
  out->incomplete = record->incomplete != 0;
  out->statement_complete = record->statement_complete != 0;
  return SEQUEL_OK;
}

sequel_status_t sequel_diagnose_n(const char *sql, size_t n, sequel_dialect_t dialect, sequel_diagnostic_t *out) {
  if (!out)
    return SEQUEL_ERR_NULL;
  sequel_status_t status = sequel_diagnostics_validate_input(sql, n, dialect, out);
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
    sequel_diagnostics_report(sql, n, out, SEQUEL_DIAG_UNSUPPORTED_PAYLOAD, (size_t)(format - sql), (size_t)(format - sql) + 6);
    return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  sequel_parse_options_t options;
  sequel_parse_options_init(&options);
  options.dialect = dialect;
  options.retain_comments = true;
  sequel_source_view_t source = {sql, n};
  sequel_syntax_document_t *document = NULL;
  status = sequel_parse_sql(&source, &options, NULL, &document);
  if (status != SEQUEL_OK) {
    size_t block_comment_openings = 0;
    for (size_t i = 0; i + 1 < n; ++i)
      if (sql[i] == '/' && sql[i + 1] == '*')
        ++block_comment_openings;
    if (block_comment_openings > 64) {
      sequel_diagnostics_report(sql, n, out, SEQUEL_DIAG_NESTING_LIMIT, 0, n);
      return SEQUEL_ERR_OVERFLOW;
    }
    return status;
  }
  size_t issue_count = 0;
  const sequel_parse_issue_t *issues = sequel_syntax_issues(document, &issue_count);
  const sequel_lexed_document_t *lexed = sequel_syntax_lexed(document);
  sequel_diagnostic_list_t lexical_diagnostics = {0};
  status = sequel_collect_lexical_diagnostics(lexed, &lexical_diagnostics);
  if (status != SEQUEL_OK) {
    sequel_syntax_document_free(document);
    return status;
  }
  bool has_opaque_recovery = false;
  for (size_t i = 0; i < issue_count; ++i)
    has_opaque_recovery |= issues[i].recovery == SEQUEL_RECOVERY_OPAQUE_REGION;
  size_t block_comment_openings = 0;
  for (size_t i = 0; i + 1 < n; ++i)
    if (sql[i] == '/' && sql[i + 1] == '*')
      ++block_comment_openings;
  if (block_comment_openings > 64) {
    sequel_diagnostics_report(sql, n, out, SEQUEL_DIAG_NESTING_LIMIT, 0, n);
    sequel_diagnostic_list_free(&lexical_diagnostics);
    sequel_syntax_document_free(document);
    return SEQUEL_ERR_OVERFLOW;
  }
  has_opaque_recovery = has_opaque_recovery && block_comment_openings > 64;
  for (size_t i = 0; i < lexical_diagnostics.count; ++i) {
    const sequel_diagnostic_record_t *record = &lexical_diagnostics.records[i];
    if (has_opaque_recovery && record->code == SEQUEL_DIAG_UNTERMINATED_COMMENT)
      continue;
    if (record->code == SEQUEL_DIAG_NESTING_LIMIT) {
      sequel_diagnostics_report(sql, n, out, record->code, record->range.start, record->range.end);
      sequel_diagnostic_list_free(&lexical_diagnostics);
      sequel_syntax_document_free(document);
      return SEQUEL_ERR_OVERFLOW;
    }
    size_t end = record->range.end;
    if (record->code >= SEQUEL_DIAG_UNTERMINATED_STRING && record->code <= SEQUEL_DIAG_UNTERMINATED_PARAMETER)
      end = n;
    sequel_diagnostics_report(sql, n, out, record->code, record->range.start, end);
    sequel_diagnostic_list_free(&lexical_diagnostics);
    sequel_syntax_document_free(document);
    return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  sequel_diagnostic_list_free(&lexical_diagnostics);
  if (issue_count) {
    if (issues[0].recovery == SEQUEL_RECOVERY_OPAQUE_REGION) {
      sequel_diagnostics_report(sql, n, out, SEQUEL_DIAG_NESTING_LIMIT, issues[0].range.start, issues[0].range.end);
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
    if (tokens[i - 1].kind == SEQUEL_TOKEN_EOF || tokens[i - 1].kind == SEQUEL_TOKEN_WHITESPACE || tokens[i - 1].kind == SEQUEL_TOKEN_LINE_COMMENT || tokens[i - 1].kind == SEQUEL_TOKEN_BLOCK_COMMENT)
      continue;
    out->statement_complete = tokens[i - 1].kind == SEQUEL_TOKEN_PUNCTUATION && source.data[tokens[i - 1].range.start] == ';';
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

sequel_status_t sequel_format_ex(const char *sql, sequel_dialect_t dialect, char *out, size_t capacity, size_t *length, sequel_diagnostic_t *diagnostic) {
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
    sequel_diagnostic_code_t code = status == SEQUEL_ERR_NOMEM                            ? SEQUEL_DIAG_OUT_OF_MEMORY
                                    : status == SEQUEL_ERR_OVERFLOW                       ? SEQUEL_DIAG_OUTPUT_LIMIT
                                    : !length || (!out && capacity) || (out && !capacity) ? SEQUEL_DIAG_INVALID_ARGUMENT
                                                                                          : SEQUEL_DIAG_UNSUPPORTED_LAYOUT;
    sequel_diagnostics_report(sql, strlen(sql), diagnostic, code, 0, 0);
  }
  return status;
}
