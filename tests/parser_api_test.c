#include <stdio.h>
#include <string.h>

#include <sequel.h>

static int check(int condition, const char *message) {
  if (!condition)
    fprintf(stderr, "parser test failed: %s\n", message);
  return condition;
}

int main(void) {
  const char sql[] = "SELECT id, 'value' FROM users WHERE id = $1; SELECT 2";
  sequel_source_view_t source = {sql, strlen(sql)};
  sequel_parse_options_t options;
  sequel_parse_options_init(&options);
  options.dialect = SEQUEL_POSTGRESQL;

  sequel_lexed_document_t *lexed = NULL;
  if (!check(sequel_lex(&source, &options, NULL, &lexed) == SEQUEL_OK, "lex succeeds"))
    return 1;
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  if (!check(token_count > 10, "lexed token count"))
    return 1;
  if (!check(tokens[0].kind == SEQUEL_TOKEN_KEYWORD && tokens[0].keyword_id != 0, "keyword token"))
    return 1;
  if (!check(tokens[3].kind == SEQUEL_TOKEN_STRING, "string token"))
    return 1;
  sequel_lexed_document_free(lexed);

  sequel_syntax_document_t *syntax = NULL;
  if (!check(sequel_parse_sql(&source, &options, NULL, &syntax) == SEQUEL_OK, "parse succeeds"))
    return 1;
  size_t node_count = 0;
  const sequel_syntax_node_t *nodes = sequel_syntax_nodes(syntax, &node_count);
  if (!check(node_count >= 2, "statement and clause nodes"))
    return 1;
  if (!check(nodes[0].kind == SEQUEL_SYNTAX_SELECT_STATEMENT, "select production"))
    return 1;
  sequel_query_document_t *query = NULL;
  if (!check(sequel_build_query_document(syntax, &options, &query) == SEQUEL_OK, "query document succeeds"))
    return 1;
  if (!check(sequel_query_statement_count(query) == 2, "two query statements"))
    return 1;
  if (!check(sequel_query_statements(query)[0].operation == SEQUEL_OPERATION_SELECT, "query operation"))
    return 1;
  sequel_query_document_free(query);
  sequel_syntax_document_free(syntax);
  return 0;
}
