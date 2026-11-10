#include <stdlib.h>

#include <sequel/diagnostics.h>

static int append_record(sequel_diagnostic_list_t *diagnostics, sequel_diagnostic_record_t record) {
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

void sequel_diagnostic_list_free(sequel_diagnostic_list_t *diagnostics) {
  if (!diagnostics)
    return;
  free(diagnostics->records);
  *diagnostics = (sequel_diagnostic_list_t){0};
}

static sequel_diagnostic_code_t lexical_code(const sequel_token_t *token) {
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
    if (!append_record(diagnostics, record)) {
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
                                         .syntax_node = projections[i].expression_node};
    if (!append_record(diagnostics, record)) {
      sequel_diagnostic_list_free(diagnostics);
      return SEQUEL_ERR_NOMEM;
    }
  }
  size_t statement_count = sequel_query_statement_count(query);
  const sequel_statement_info_t *statements = sequel_query_statements(query);
  for (size_t i = 0; i < statement_count; ++i) {
    if ((statements[i].operation != SEQUEL_OPERATION_UPDATE && statements[i].operation != SEQUEL_OPERATION_DELETE) ||
        query_has_predicate(query, i))
      continue;
    sequel_diagnostic_record_t record = {.origin = SEQUEL_DIAGNOSTIC_POLICY,
                                         .code = SEQUEL_DIAG_UNBOUNDED_WRITE,
                                         .severity = SEQUEL_ERROR,
                                         .range = statements[i].range,
                                         .statement_index = i,
                                         .syntax_node = statements[i].syntax_node};
    if (!append_record(diagnostics, record)) {
      sequel_diagnostic_list_free(diagnostics);
      return SEQUEL_ERR_NOMEM;
    }
  }
  return SEQUEL_OK;
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
    if (!append_record(diagnostics, record)) {
      sequel_diagnostic_list_free(diagnostics);
      return SEQUEL_ERR_NOMEM;
    }
  }
  return SEQUEL_OK;
}
