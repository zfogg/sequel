#include <stdio.h>
#include <string.h>

#include <sequel.h>

static void position_at(const char *sql, size_t length, size_t offset, size_t *line, size_t *column) {
  *line = 1;
  *column = 1;
  if (offset > length)
    offset = length;
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
    } else if ((c & 0xc0) != 0x80) {
      ++*column;
    }
  }
}

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
  memset(diagnostic, 0, sizeof(*diagnostic));
  diagnostic->code = code;
  diagnostic->severity = issue->recovery == SEQUEL_RECOVERY_NONE ? SEQUEL_ERROR : SEQUEL_WARNING;
  diagnostic->offset = issue->range.start;
  diagnostic->end_offset = issue->range.end;
  diagnostic->incomplete =
      issue->code == SEQUEL_PARSE_ISSUE_UNCLOSED_GROUP || issue->code == SEQUEL_PARSE_ISSUE_UNTERMINATED_TOKEN;
  position_at(source.data, source.length, diagnostic->offset, &diagnostic->line, &diagnostic->column);
  position_at(source.data, source.length, diagnostic->end_offset, &diagnostic->end_line, &diagnostic->end_column);
  const char *name = sequel_diagnostic_code_name(code);
  snprintf(diagnostic->message, sizeof(diagnostic->message), "%s", name ? name : "SQL parse issue");
  snprintf(diagnostic->explanation, sizeof(diagnostic->explanation),
           "The SQL parser recovered from a structural issue.");
  snprintf(diagnostic->hint, sizeof(diagnostic->hint), "Review the highlighted delimiter or token.");
  return SEQUEL_OK;
}
