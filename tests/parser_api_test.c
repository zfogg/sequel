#include <stdio.h>
#include <string.h>

#include <sequel.h>

static int check(int condition, const char *message) {
  if (!condition)
    fprintf(stderr, "parser test failed: %s\n", message);
  return condition;
}

static bool saw_from(const sequel_syntax_document_t *document, size_t node_index, void *userdata) {
  (void)document;
  const sequel_syntax_node_t *nodes = sequel_syntax_nodes(document, NULL);
  if (nodes[node_index].kind == SEQUEL_SYNTAX_FROM_CLAUSE)
    *(int *)userdata = 1;
  return true;
}

static bool count_relations(const sequel_query_document_t *document, size_t index, void *userdata) {
  (void)document;
  (void)index;
  ++*(size_t *)userdata;
  return true;
}

int main(void) {
  const char sql[] =
      "SELECT coalesce(id, 1), 'value' /* removed */ FROM users WHERE id = $1 AND (id IN (1, 2)); SELECT 2";
  sequel_source_view_t source = {sql, strlen(sql)};
  sequel_source_t *owned_source = NULL;
  if (!check(sequel_source_create(&source, NULL, &owned_source) == SEQUEL_OK, "owned source"))
    return 1;
  if (!check(sequel_source_dialect(owned_source) == SEQUEL_GENERIC, "default source dialect"))
    return 1;
  if (!check(sequel_source_view(owned_source).length == source.length, "owned source length"))
    return 1;
  sequel_source_free(owned_source);
  sequel_parse_options_t options;
  sequel_parse_options_init(&options);
  options.dialect = SEQUEL_POSTGRESQL;

  sequel_source_view_t positioned = {"α\r\nβ", 6};
  sequel_position_t position;
  if (!check(sequel_source_position(&positioned, 5, &position) == SEQUEL_OK && position.line == 2 &&
                 position.column == 2,
             "source position"))
    return 1;

  sequel_lexed_document_t *lexed = NULL;
  if (!check(sequel_lex(&source, &options, NULL, &lexed) == SEQUEL_OK, "lex succeeds"))
    return 1;
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  if (!check(token_count > 10 && sequel_lexed_token_count(lexed) == token_count, "lexed token count"))
    return 1;
  if (!check(tokens[0].kind == SEQUEL_TOKEN_KEYWORD && tokens[0].keyword_id != 0, "keyword token"))
    return 1;
  sequel_source_view_t numeric_source = {"SELECT .123, 0.123e9", strlen("SELECT .123, 0.123e9")};
  sequel_lexed_document_t *numeric_lexed = NULL;
  if (!check(sequel_lex(&numeric_source, &options, NULL, &numeric_lexed) == SEQUEL_OK, "numeric lexing"))
    return 1;
  size_t numeric_count = 0;
  const sequel_token_t *numeric_tokens = sequel_lexed_tokens(numeric_lexed, &numeric_count);
  if (!check(numeric_count >= 5 && numeric_tokens[1].kind == SEQUEL_TOKEN_NUMBER &&
                 numeric_tokens[3].kind == SEQUEL_TOKEN_NUMBER,
             "numeric token boundaries"))
    return 1;
  sequel_lexed_document_free(numeric_lexed);
  if (!check(sequel_lexed_source_object(lexed) != NULL &&
                 sequel_source_dialect(sequel_lexed_source_object(lexed)) == SEQUEL_POSTGRESQL,
             "lexed source ownership"))
    return 1;
  const char *token_text = NULL;
  size_t token_text_length = 0;
  sequel_source_view_t token_source_view = sequel_lexed_source(lexed);
  if (!check(sequel_token_text(&token_source_view, &tokens[0], &token_text, &token_text_length) ==
                 SEQUEL_OK && token_text_length > 0,
             "token text accessor"))
    return 1;
  int found_string = 0;
  for (size_t i = 0; i < token_count; ++i)
    found_string |= tokens[i].kind == SEQUEL_TOKEN_STRING;
  if (!check(found_string, "string token"))
    return 1;
  const char operator_sql[] = "SELECT [users] WHERE active >= TRUE";
  sequel_source_view_t operator_source = {operator_sql, strlen(operator_sql)};
  sequel_parse_options_t generic_options = options;
  generic_options.dialect = SEQUEL_GENERIC;
  sequel_lexed_document_t *operator_lexed = NULL;
  if (!check(sequel_lex(&operator_source, &generic_options, NULL, &operator_lexed) == SEQUEL_OK, "operator lexing"))
    return 1;
  size_t operator_count = 0;
  const sequel_token_t *operator_tokens = sequel_lexed_tokens(operator_lexed, &operator_count);
  if (!check(operator_tokens[1].kind == SEQUEL_TOKEN_QUOTED_IDENTIFIER, "bracket identifier"))
    return 1;
  if (!check(operator_tokens[4].kind == SEQUEL_TOKEN_OPERATOR, "multi-character operator"))
    return 1;
  if (!check(operator_tokens[5].kind == SEQUEL_TOKEN_BOOLEAN, "boolean literal"))
    return 1;
  sequel_lexed_document_free(operator_lexed);
  sequel_text_result_t sanitized = {0};
  if (!check(sequel_sanitize_tokens(lexed, SEQUEL_SANITIZE_REPLACE_LITERALS, &sanitized) == SEQUEL_OK &&
                 strstr(sanitized.data, "?") != NULL && strstr(sanitized.data, "'value'") == NULL,
             "token sanitizer"))
    return 1;
  sequel_text_result_free(&sanitized);
  sequel_lexed_document_free(lexed);

  const char lexical_sql[] = "SELECT 'unfinished";
  sequel_source_view_t lexical_source = {lexical_sql, strlen(lexical_sql)};
  sequel_lexed_document_t *lexical_document = NULL;
  if (!check(sequel_lex(&lexical_source, &options, NULL, &lexical_document) == SEQUEL_OK, "lexical recovery parse"))
    return 1;
  sequel_diagnostic_list_t lexical_diagnostics = {0};
  if (!check(sequel_collect_lexical_diagnostics(lexical_document, &lexical_diagnostics) == SEQUEL_OK &&
                 lexical_diagnostics.count == 1 && lexical_diagnostics.records[0].origin == SEQUEL_DIAGNOSTIC_LEXICAL &&
                 lexical_diagnostics.records[0].code == SEQUEL_DIAG_UNTERMINATED_STRING,
             "lexical diagnostic collection"))
    return 1;
  sequel_diagnostic_list_free(&lexical_diagnostics);
  sequel_lexed_document_free(lexical_document);
  const char limited_sql[] = ") ) ] ]";
  sequel_source_view_t limited_source = {limited_sql, strlen(limited_sql)};
  sequel_parse_options_t limited_options = options;
  limited_options.max_diagnostics = 1;
  sequel_syntax_document_t *limited_syntax = NULL;
  if (!check(sequel_parse_sql(&limited_source, &limited_options, NULL, &limited_syntax) == SEQUEL_OK,
             "diagnostic budget parse"))
    return 1;
  size_t limited_issue_count = 0;
  (void)sequel_syntax_issues(limited_syntax, &limited_issue_count);
  if (!check(limited_issue_count == 1 && sequel_syntax_issue_count(limited_syntax) == limited_issue_count,
             "diagnostic budget"))
    return 1;
  sequel_syntax_document_free(limited_syntax);
  sequel_parse_options_t node_limited_options = options;
  node_limited_options.max_nodes = 1;
  sequel_syntax_document_t *node_limited_syntax = NULL;
  if (!check(sequel_parse_sql(&source, &node_limited_options, NULL, &node_limited_syntax) == SEQUEL_ERR_LIMIT &&
                 node_limited_syntax == NULL,
             "node budget"))
    return 1;
  sequel_parse_options_t memory_limited_options = options;
  memory_limited_options.max_memory_bytes = source.length;
  if (!check(sequel_parse_sql(&source, &memory_limited_options, NULL, &node_limited_syntax) == SEQUEL_ERR_LIMIT &&
                 node_limited_syntax == NULL,
             "memory budget"))
    return 1;

  sequel_syntax_document_t *syntax = NULL;
  if (!check(sequel_parse_sql(&source, &options, NULL, &syntax) == SEQUEL_OK, "parse succeeds"))
    return 1;
  sequel_format_options_t format_options;
  sequel_format_options_init(&format_options);
  sequel_text_result_t formatted = {0};
  if (!check(sequel_format_syntax(syntax, &format_options, &formatted) == SEQUEL_OK &&
                 formatted.data != NULL && formatted.length > 0 && formatted.mode == SEQUEL_FORMAT_SYNTAX_AWARE &&
                 formatted.data[formatted.length] == 0,
             "syntax formatter result"))
    return 1;
  sequel_text_result_free(&formatted);
  size_t node_count = 0;
  const sequel_syntax_node_t *nodes = sequel_syntax_nodes(syntax, &node_count);
  if (!check(node_count >= 2, "statement and clause nodes"))
    return 1;
  int found_binary = 0;
  size_t binary_count = 0;
  for (size_t i = 0; i < node_count; ++i)
    if (nodes[i].kind == SEQUEL_SYNTAX_BINARY_EXPRESSION) {
      found_binary = 1;
      ++binary_count;
    }
  if (!check(found_binary && binary_count >= 3, "keyword and comparison precedence nodes"))
    return 1;
  int found_call = 0;
  int found_group = 0;
  for (size_t i = 0; i < node_count; ++i) {
    found_call |= nodes[i].kind == SEQUEL_SYNTAX_FUNCTION_CALL;
    found_group |= nodes[i].kind == SEQUEL_SYNTAX_GROUP;
  }
  const char call_sql[] = "SELECT count(id)";
  sequel_source_view_t call_source = {call_sql, strlen(call_sql)};
  sequel_syntax_document_t *call_syntax = NULL;
  if (!check(sequel_parse_sql(&call_source, &options, NULL, &call_syntax) == SEQUEL_OK, "function parse"))
    return 1;
  size_t call_nodes = 0;
  const sequel_syntax_node_t *call_tree = sequel_syntax_nodes(call_syntax, &call_nodes);
  for (size_t i = 0; i < call_nodes; ++i)
    found_call |= call_tree[i].kind == SEQUEL_SYNTAX_FUNCTION_CALL;
  sequel_syntax_document_free(call_syntax);
  if (!check(found_call && found_group, "function and grouped expression nodes"))
    return 1;
  const char family_sql[] = "EXPLAIN SELECT id FROM users WHERE id = 1 RETURNING id;";
  sequel_source_view_t family_source = {family_sql, strlen(family_sql)};
  sequel_syntax_document_t *family_syntax = NULL;
  if (!check(sequel_parse_sql(&family_source, &options, NULL, &family_syntax) == SEQUEL_OK, "statement family parse"))
    return 1;
  size_t family_nodes = 0;
  const sequel_syntax_node_t *family = sequel_syntax_nodes(family_syntax, &family_nodes);
  int found_returning = 0;
  if (family_nodes)
    for (size_t i = 0; i < family_nodes; ++i)
      found_returning |= family[i].kind == SEQUEL_SYNTAX_RETURNING_CLAUSE;
  if (!check(found_returning && family[0].kind == SEQUEL_SYNTAX_EXPLAIN_STATEMENT, "statement family nodes"))
    return 1;
  sequel_syntax_document_free(family_syntax);
  if (!check(nodes[0].kind == SEQUEL_SYNTAX_SELECT_STATEMENT, "select production"))
    return 1;
  sequel_query_document_t *query = NULL;
  if (!check(sequel_build_query_document(syntax, &options, &query) == SEQUEL_OK, "query document succeeds"))
    return 1;
  sequel_diagnostic_list_t query_diagnostics = {0};
  if (!check(sequel_collect_query_diagnostics(query, &query_diagnostics) == SEQUEL_OK, "query diagnostic collection"))
    return 1;
  int found_multiple_statements = 0;
  int found_no_relation = 0;
  for (size_t i = 0; i < query_diagnostics.count; ++i) {
    found_multiple_statements |= query_diagnostics.records[i].code == SEQUEL_DIAG_MULTIPLE_STATEMENTS;
    found_no_relation |= query_diagnostics.records[i].code == SEQUEL_DIAG_NO_REFERENCED_TABLE;
  }
  if (!check(found_multiple_statements && found_no_relation, "query structural diagnostics"))
    return 1;
  sequel_diagnostic_list_free(&query_diagnostics);
  if (!check(sequel_query_statement_count(query) == 2, "two query statements"))
    return 1;
  if (!check(sequel_query_statements(query)[0].operation == SEQUEL_OPERATION_SELECT, "query operation"))
    return 1;
  if (!check(sequel_query_parameter_count(query) == 1, "parameter count"))
    return 1;
  if (!check(sequel_query_parameters(query)[0].style == SEQUEL_BIND_NUMBERED &&
                 sequel_query_parameters(query)[0].explicit_number == 1,
             "numbered parameter"))
    return 1;
  sequel_source_view_t repeated_bind_source = {"SELECT :account, :account", strlen("SELECT :account, :account")};
  sequel_syntax_document_t *repeated_bind_syntax = NULL;
  sequel_query_document_t *repeated_bind_query = NULL;
  if (!check(sequel_parse_sql(&repeated_bind_source, &options, NULL, &repeated_bind_syntax) == SEQUEL_OK &&
                 sequel_build_query_document(repeated_bind_syntax, &options, &repeated_bind_query) == SEQUEL_OK &&
                 sequel_query_parameter_count(repeated_bind_query) == 2 &&
                 sequel_query_parameters(repeated_bind_query)[0].identity ==
                     sequel_query_parameters(repeated_bind_query)[1].identity,
             "repeated named bind identity"))
    return 1;
  char repeated_bind_fingerprint[SEQUEL_FINGERPRINT_LENGTH];
  char distinct_bind_fingerprint[SEQUEL_FINGERPRINT_LENGTH];
  sequel_source_view_t distinct_bind_source = {"SELECT :account, :user", strlen("SELECT :account, :user")};
  sequel_syntax_document_t *distinct_bind_syntax = NULL;
  sequel_query_document_t *distinct_bind_query = NULL;
  if (!check(sequel_parse_sql(&distinct_bind_source, &options, NULL, &distinct_bind_syntax) == SEQUEL_OK &&
                 sequel_build_query_document(distinct_bind_syntax, &options, &distinct_bind_query) == SEQUEL_OK &&
                 sequel_fingerprint_document(repeated_bind_syntax, repeated_bind_query, SEQUEL_FINGERPRINT_QUERY_SHAPE,
                                             repeated_bind_fingerprint) == SEQUEL_OK &&
                 sequel_fingerprint_document(distinct_bind_syntax, distinct_bind_query, SEQUEL_FINGERPRINT_QUERY_SHAPE,
                                             distinct_bind_fingerprint) == SEQUEL_OK &&
                 strcmp(repeated_bind_fingerprint, distinct_bind_fingerprint) != 0,
             "query fingerprint bind identity"))
    return 1;
  sequel_query_document_free(distinct_bind_query);
  sequel_syntax_document_free(distinct_bind_syntax);
  sequel_query_document_free(repeated_bind_query);
  sequel_syntax_document_free(repeated_bind_syntax);
  size_t visited_relations = 0;
  sequel_query_visitor_t query_visitor = {.relation = count_relations, .userdata = &visited_relations};
  if (!check(sequel_query_visit(query, &query_visitor) == SEQUEL_OK && visited_relations == 1, "query visitor"))
    return 1;
  if (!check(sequel_query_relation_count(query) == 1, "relation count"))
    return 1;
  if (!check(sequel_query_relations(query)[0].name_range.end > sequel_query_relations(query)[0].name_range.start,
             "relation name range"))
    return 1;
  if (!check(sequel_query_projection_count(query) >= 2, "projection count"))
    return 1;
  if (!check(!sequel_query_projections(query)[0].wildcard, "projection metadata"))
    return 1;
  if (!check(sequel_query_predicate_count(query) == 1 && sequel_query_predicates(query)[0].normalized_hash != 0,
             "predicate metadata"))
    return 1;
  sequel_query_analysis_options_t analysis_options;
  sequel_query_analysis_options_init(&analysis_options);
  sequel_query_analysis_t *document_analysis = NULL;
  if (!check(sequel_analyze_document(syntax, query, &analysis_options, NULL, &document_analysis) == SEQUEL_OK &&
                 sequel_query_analysis_query(document_analysis) != NULL,
             "document analysis"))
    return 1;
  if (!check(sequel_query_analysis_diagnostics(document_analysis) != NULL, "analysis diagnostics ownership"))
    return 1;
  size_t relation_count = 0, projection_count = 0, predicate_count = 0, parameter_count = 0;
  if (!check(sequel_query_analysis_relations(document_analysis, &relation_count) != NULL && relation_count == 1 &&
                 sequel_query_analysis_projections(document_analysis, &projection_count) != NULL && projection_count >= 2 &&
                 sequel_query_analysis_predicates(document_analysis, &predicate_count) != NULL && predicate_count == 1 &&
                 sequel_query_analysis_parameters(document_analysis, &parameter_count) != NULL && parameter_count == 1,
             "analysis semantic collections"))
    return 1;
  sequel_query_analysis_options_t selective_analysis_options = analysis_options;
  selective_analysis_options.include_bindings = false;
  selective_analysis_options.include_relations = false;
  sequel_query_analysis_t *selective_analysis = NULL;
  if (!check(sequel_analyze_document(syntax, query, &selective_analysis_options, NULL, &selective_analysis) == SEQUEL_OK &&
                 sequel_query_analysis_parameters(selective_analysis, &parameter_count) == NULL && parameter_count == 0 &&
                 sequel_query_analysis_relations(selective_analysis, &relation_count) == NULL && relation_count == 0,
             "analysis collection options"))
    return 1;
  sequel_query_analysis_free(selective_analysis);
  const sequel_text_result_t *analysis_summary = sequel_query_analysis_summary(document_analysis);
  if (!check(analysis_summary && analysis_summary->length > 0, "analysis summary output"))
    return 1;
  char analysis_fingerprint[SEQUEL_FINGERPRINT_LENGTH];
  if (!check(sequel_query_analysis_fingerprint(document_analysis, analysis_fingerprint) == SEQUEL_OK &&
                 strlen(analysis_fingerprint) == 16,
             "analysis fingerprint output"))
    return 1;
  const char portability_sql[] = "SELECT $tag$value$tag$";
  sequel_source_view_t portability_source = {portability_sql, strlen(portability_sql)};
  sequel_parse_options_t portability_options = options;
  portability_options.dialect = SEQUEL_POSTGRESQL;
  sequel_syntax_document_t *portability_syntax = NULL;
  sequel_query_document_t *portability_query = NULL;
  if (!check(sequel_parse_sql(&portability_source, &portability_options, NULL, &portability_syntax) == SEQUEL_OK &&
                 sequel_build_query_document(portability_syntax, &portability_options, &portability_query) == SEQUEL_OK,
             "portability document"))
    return 1;
  sequel_query_analysis_options_t portability_analysis_options = analysis_options;
  portability_analysis_options.include_portability = true;
  portability_analysis_options.target_dialect = SEQUEL_GENERIC;
  sequel_query_analysis_t *portability_analysis = NULL;
  if (!check(sequel_analyze_document(portability_syntax, portability_query, &portability_analysis_options, NULL,
                                     &portability_analysis) == SEQUEL_OK &&
                 sequel_query_analysis_diagnostics(portability_analysis)->count > 0,
             "portability diagnostics"))
    return 1;
  sequel_query_analysis_free(portability_analysis);
  const sequel_dialect_t portability_targets[] = {SEQUEL_GENERIC, SEQUEL_SQLITE};
  portability_analysis_options.portability_targets = portability_targets;
  portability_analysis_options.portability_target_count = 2;
  portability_analysis = NULL;
  if (!check(sequel_analyze_document(portability_syntax, portability_query, &portability_analysis_options, NULL,
                                     &portability_analysis) == SEQUEL_OK &&
                 sequel_query_analysis_diagnostics(portability_analysis)->count >= 2,
             "multiple portability targets"))
    return 1;
  sequel_query_analysis_free(portability_analysis);
  sequel_query_document_free(portability_query);
  sequel_syntax_document_free(portability_syntax);
  sequel_query_analysis_options_t output_options = analysis_options;
  output_options.include_sanitized_sql = true;
  output_options.include_formatted_sql = true;
  output_options.include_source_maps = true;
  sequel_query_analysis_t *output_analysis = NULL;
  bool has_deleted_segment = false;
  if (!check(sequel_analyze_document(syntax, query, &output_options, NULL, &output_analysis) == SEQUEL_OK &&
                 sequel_query_analysis_sanitized_sql(output_analysis) != NULL &&
                 sequel_query_analysis_formatted_sql(output_analysis) != NULL &&
                 sequel_query_analysis_sanitized_map(output_analysis) != NULL &&
                 sequel_query_analysis_formatted_map(output_analysis) != NULL &&
                 sequel_query_analysis_sanitized_map(output_analysis)->count > 1,
             "analysis rendered outputs"))
    return 1;
  const sequel_analysis_source_map_t *sanitized_map = sequel_query_analysis_sanitized_map(output_analysis);
  for (size_t i = 0; i < sanitized_map->count; ++i)
    has_deleted_segment |= sanitized_map->segments[i].kind == SEQUEL_ANALYSIS_MAP_DELETE;
  if (!check(has_deleted_segment, "analysis deleted-token provenance"))
    return 1;
  sequel_query_analysis_free(output_analysis);
  sequel_source_view_t broken_source = {"select 'unterminated", strlen("select 'unterminated")};
  sequel_syntax_document_t *broken_syntax = NULL;
  if (!check(sequel_parse_sql(&broken_source, &options, NULL, &broken_syntax) == SEQUEL_OK,
             "recoverable lexical parse"))
    return 1;
  sequel_query_document_t *broken_query = NULL;
  if (!check(sequel_build_query_document(broken_syntax, &options, &broken_query) == SEQUEL_OK,
             "recoverable lexical query"))
    return 1;
  sequel_query_analysis_t *broken_analysis = NULL;
  if (!check(sequel_analyze_document(broken_syntax, broken_query, &analysis_options, NULL, &broken_analysis) == SEQUEL_OK &&
                 sequel_query_analysis_diagnostics(broken_analysis)->count > 0,
             "analysis includes lexical diagnostics"))
    return 1;
  sequel_query_analysis_free(broken_analysis);
  sequel_query_document_free(broken_query);
  sequel_syntax_document_free(broken_syntax);
  size_t analysis_parameter_count = 0;
  const sequel_parameter_info_t *analysis_parameters =
      sequel_query_analysis_parameters(document_analysis, &analysis_parameter_count);
  if (!check(analysis_parameter_count == 1 && analysis_parameters[0].ordinal == 1 &&
                 analysis_parameters[0].style == SEQUEL_BIND_NUMBERED,
             "analysis bind metadata"))
    return 1;
  char token_fingerprint[SEQUEL_FINGERPRINT_LENGTH];
  char syntax_fingerprint[SEQUEL_FINGERPRINT_LENGTH];
  char query_fingerprint[SEQUEL_FINGERPRINT_LENGTH];
  if (!check(sequel_fingerprint_document(syntax, query, SEQUEL_FINGERPRINT_TOKENS, token_fingerprint) == SEQUEL_OK &&
                 sequel_fingerprint_document(syntax, query, SEQUEL_FINGERPRINT_SYNTAX, syntax_fingerprint) ==
                     SEQUEL_OK &&
                 sequel_fingerprint_document(syntax, query, SEQUEL_FINGERPRINT_QUERY_SHAPE, query_fingerprint) ==
                     SEQUEL_OK &&
                 strlen(token_fingerprint) == 16 && strlen(syntax_fingerprint) == 16 && strlen(query_fingerprint) == 16,
             "document fingerprints"))
    return 1;
  sequel_summary_options_t summary_options;
  sequel_summary_options_init(&summary_options);
  sequel_text_result_t summary = {0};
  if (!check(sequel_summarize_query(query, &summary_options, &summary) == SEQUEL_OK && summary.length > 0 &&
                 strstr(summary.data, "SELECT") != NULL,
             "query summary"))
    return 1;
  sequel_text_result_free(&summary);
  sequel_query_analysis_free(document_analysis);
  sequel_buffer_t serialized = {0};
  if (!check(sequel_document_serialize(syntax, query, NULL, &serialized) == SEQUEL_OK && serialized.length > 32,
             "document serialization"))
    return 1;
  sequel_buffer_view_t serialized_view = {serialized.data, serialized.length};
  sequel_document_bundle_t *bundle = NULL;
  if (!check(sequel_document_deserialize(&source, &serialized_view, NULL, &bundle) == SEQUEL_OK &&
                 sequel_document_bundle_syntax(bundle) != NULL && sequel_document_bundle_query(bundle) != NULL,
             "document deserialization"))
    return 1;
  sequel_document_bundle_free(bundle);
  uint64_t corrupt_range = UINT64_MAX;
  memcpy(serialized.data + 16 * sizeof(uint64_t) + 2 * sizeof(uint64_t), &corrupt_range, sizeof(corrupt_range));
  if (!check(sequel_document_deserialize(&source, &serialized_view, NULL, &bundle) == SEQUEL_ERR_INVALID_ARGUMENT,
             "serialized range validation"))
    return 1;
  uint64_t valid_range = 0;
  memcpy(serialized.data + 16 * sizeof(uint64_t) + 2 * sizeof(uint64_t), &valid_range, sizeof(valid_range));
  serialized_view.length -= 1;
  if (!check(sequel_document_deserialize(&source, &serialized_view, NULL, &bundle) == SEQUEL_ERR_INVALID_ARGUMENT,
             "truncated document rejection"))
    return 1;
  sequel_buffer_free(&serialized);
  int found_from = 0;
  sequel_syntax_visitor_t visitor = {.enter_node = saw_from, .userdata = &found_from};
  if (!check(sequel_syntax_visit(syntax, 0, &visitor) == SEQUEL_OK && found_from, "syntax visitor"))
    return 1;
  const char malformed[] = "SELECT (id";
  sequel_source_view_t malformed_source = {malformed, strlen(malformed)};
  sequel_syntax_document_t *malformed_syntax = NULL;
  if (!check(sequel_parse_sql(&malformed_source, &options, NULL, &malformed_syntax) == SEQUEL_OK, "recovered parse"))
    return 1;
  size_t issue_count = 0;
  const sequel_parse_issue_t *issues = sequel_syntax_issues(malformed_syntax, &issue_count);
  if (!check(issue_count == 1 && issues[0].code == SEQUEL_PARSE_ISSUE_UNCLOSED_GROUP &&
                 issues[0].recovery == SEQUEL_RECOVERY_INSERTED_TOKEN,
             "unclosed group recovery"))
    return 1;
  sequel_diagnostic_t recovered_diagnostic;
  if (!check(sequel_syntax_diagnostic(malformed_syntax, 0, &recovered_diagnostic) == SEQUEL_OK &&
                 recovered_diagnostic.code == SEQUEL_DIAG_UNCLOSED_GROUP && recovered_diagnostic.incomplete,
             "recovery diagnostic bridge"))
    return 1;
  sequel_diagnostic_list_t collected = {0};
  if (!check(sequel_collect_syntax_diagnostics(malformed_syntax, &collected) == SEQUEL_OK && collected.count == 1 &&
                 collected.records[0].origin == SEQUEL_DIAGNOSTIC_SYNTAX &&
                 collected.records[0].code == SEQUEL_DIAG_UNCLOSED_GROUP && collected.records[0].incomplete,
             "layered syntax diagnostic collection"))
    return 1;
  sequel_diagnostic_list_free(&collected);
  sequel_syntax_document_free(malformed_syntax);
  sequel_text_edit_t edit = {.start = 7, .old_end = 9, .replacement = "name", .replacement_length = 4};
  sequel_syntax_document_t *updated = NULL;
  if (!check(sequel_reparse(syntax, &edit, &options, NULL, &updated) == SEQUEL_OK, "incremental reparse"))
    return 1;
  sequel_reparse_impact_t impact;
  if (!check(sequel_reparse_impact(strlen(sql), &edit, &impact) == SEQUEL_OK && impact.preserved_prefix == 7 &&
                 impact.preserved_suffix == strlen(sql) - 9 && impact.new_range.end == 11,
             "incremental impact"))
    return 1;
  if (!check(sequel_lexed_source(sequel_syntax_lexed(updated)).length == strlen(sql) + 2, "edited source length"))
    return 1;
  sequel_syntax_document_free(updated);
  sequel_query_document_free(query);
  sequel_syntax_document_free(syntax);
  return 0;
}
