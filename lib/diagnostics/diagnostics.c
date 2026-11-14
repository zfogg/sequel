#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <sequel.h>
#include "diagnostic_catalog.h"
#include "../resources.h"
#include "../util/text.h"
#include "../util/utf8.h"

sequel_status_t sequel_collect_syntax_diagnostics(const sequel_syntax_document_t *syntax, sequel_diagnostic_list_t *diagnostics) {
  if (!syntax || !diagnostics)
    return SEQUEL_ERR_NULL;
  sequel_diagnostic_list_free(diagnostics);
  size_t count = 0;
  const sequel_parse_issue_t *issues = sequel_syntax_issues(syntax, &count);
  for (size_t i = 0; i < count; ++i) {
    size_t syntax_node = SIZE_MAX;
    size_t node_count = 0;
    const sequel_syntax_node_t *nodes = sequel_syntax_nodes(syntax, &node_count);
    for (size_t node = 0; node < node_count; ++node) {
      if (!sequel_source_range_contains(nodes[node].range, issues[i].range))
        continue;
      if (syntax_node == SIZE_MAX || nodes[node].range.end - nodes[node].range.start < nodes[syntax_node].range.end - nodes[syntax_node].range.start)
        syntax_node = node;
    }
    sequel_diagnostic_code_t code = SEQUEL_DIAG_UNSUPPORTED_LAYOUT;
    switch (issues[i].code) {
    case SEQUEL_PARSE_ISSUE_UNMATCHED_CLOSE:
      code = SEQUEL_DIAG_UNMATCHED_CLOSE;
      break;
    case SEQUEL_PARSE_ISSUE_UNCLOSED_GROUP:
      code = SEQUEL_DIAG_UNCLOSED_GROUP;
      break;
    case SEQUEL_PARSE_ISSUE_UNTERMINATED_TOKEN:
      code = SEQUEL_DIAG_UNTERMINATED_STRING;
      {
        sequel_source_view_t source = sequel_lexed_source(sequel_syntax_lexed(syntax));
        if (issues[i].range.start + 1 < source.length && source.data[issues[i].range.start] == '/' && source.data[issues[i].range.start + 1] == '*')
          code = SEQUEL_DIAG_UNTERMINATED_COMMENT;
      }
      break;
    case SEQUEL_PARSE_ISSUE_UNEXPECTED_TOKEN:
      code = SEQUEL_DIAG_UNSUPPORTED_LAYOUT;
      {
        sequel_source_view_t source = sequel_lexed_source(sequel_syntax_lexed(syntax));
        if (issues[i].range.start < source.length) {
          const char token = source.data[issues[i].range.start];
          if (token == '{')
            code = SEQUEL_DIAG_INVALID_PARAMETER;
          else if (token == ')' || token == ']' || token == '}')
            code = SEQUEL_DIAG_UNMATCHED_CLOSE;
        }
      }
      break;
    }
    sequel_diagnostic_record_t record = {.origin = SEQUEL_DIAGNOSTIC_SYNTAX,
                                         .code = code,
                                         .severity = issues[i].code == SEQUEL_PARSE_ISSUE_UNMATCHED_CLOSE || issues[i].code == SEQUEL_PARSE_ISSUE_UNEXPECTED_TOKEN ? SEQUEL_ERROR : SEQUEL_WARNING,
                                         .range = issues[i].range,
                                         .statement_index = issues[i].statement_index,
                                         .syntax_node = syntax_node,
                                         .incomplete = issues[i].code == SEQUEL_PARSE_ISSUE_UNCLOSED_GROUP || issues[i].code == SEQUEL_PARSE_ISSUE_UNTERMINATED_TOKEN,
                                         .recovered = issues[i].recovery != SEQUEL_RECOVERY_NONE};
    snprintf(record.message, sizeof(record.message), "%s", sequel_diagnostic_code_name(code));
    if (!sequel_diagnostic_list_append(diagnostics, record))
      goto no_memory;
  }
  return SEQUEL_OK;
no_memory:
  sequel_diagnostic_list_free(diagnostics);
  return SEQUEL_ERR_NOMEM;
}
