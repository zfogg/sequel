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
