#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <sequel.h>
#include <sequel/fingerprint.h>

sequel_status_t sequel_fingerprint(const char *sql, sequel_dialect_t dialect, char out[96]) {
  if (!out)
    return SEQUEL_ERR_NULL;
  out[0] = 0;
  if (sql) {
    const char *cursor = sql;
    while (*cursor && isspace((unsigned char)*cursor))
      ++cursor;
    if (!*cursor || (cursor[0] == '-' && cursor[1] == '-') || (cursor[0] == '/' && cursor[1] == '*'))
      return SEQUEL_OK;
  }
  sequel_analysis_options_t options = {0};
  options.dialect = dialect;
  options.omit_query_text = true;
  options.omit_source_maps = true;
  options.omit_bind_descriptors = true;
  sequel_analysis_t analysis;
  sequel_status_t status = sequel_analyze(sql, &options, &analysis);
  if (status == SEQUEL_OK)
    memcpy(out, analysis.fingerprint, sizeof(analysis.fingerprint));
  sequel_analysis_free(&analysis);
  return status;
}

static void hash_bytes(uint64_t *hash, const void *data, size_t length) {
  const unsigned char *bytes = data;
  for (size_t i = 0; i < length; ++i) {
    *hash ^= bytes[i];
    *hash *= UINT64_C(1099511628211);
  }
}

static void hash_u64(uint64_t *hash, uint64_t value) { hash_bytes(hash, &value, sizeof(value)); }

sequel_status_t sequel_fingerprint_document(const sequel_syntax_document_t *syntax,
                                            const sequel_query_document_t *query, sequel_fingerprint_mode_t mode,
                                            char out[SEQUEL_FINGERPRINT_LENGTH]) {
  if (!syntax || !out || mode < SEQUEL_FINGERPRINT_TOKENS || mode > SEQUEL_FINGERPRINT_QUERY_SHAPE)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (mode == SEQUEL_FINGERPRINT_QUERY_SHAPE && !query)
    return SEQUEL_ERR_NULL;
  uint64_t hash = UINT64_C(1469598103934665603);
  const sequel_lexed_document_t *lexed = sequel_syntax_lexed(syntax);
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  if (mode == SEQUEL_FINGERPRINT_TOKENS) {
    for (size_t i = 0; i < token_count; ++i) {
      hash_u64(&hash, tokens[i].kind);
      hash_u64(&hash, tokens[i].flags);
      hash_u64(&hash, tokens[i].range.start);
      hash_u64(&hash, tokens[i].range.end);
      hash_u64(&hash, tokens[i].keyword_id);
      hash_u64(&hash, tokens[i].dialect_id);
    }
  } else if (mode == SEQUEL_FINGERPRINT_SYNTAX) {
    size_t node_count = 0;
    const sequel_syntax_node_t *nodes = sequel_syntax_nodes(syntax, &node_count);
    for (size_t i = 0; i < node_count; ++i) {
      hash_u64(&hash, nodes[i].kind);
      hash_u64(&hash, nodes[i].flags);
      hash_u64(&hash, nodes[i].range.start);
      hash_u64(&hash, nodes[i].range.end);
      hash_u64(&hash, nodes[i].first_child);
      hash_u64(&hash, nodes[i].child_count);
      hash_u64(&hash, nodes[i].first_token);
      hash_u64(&hash, nodes[i].token_count);
    }
  } else {
    size_t statement_count = sequel_query_statement_count(query);
    const sequel_statement_info_t *statements = sequel_query_statements(query);
    for (size_t i = 0; i < statement_count; ++i) {
      hash_u64(&hash, statements[i].operation);
      hash_u64(&hash, statements[i].flags);
    }
    size_t relation_count = sequel_query_relation_count(query);
    const sequel_relation_info_t *relations = sequel_query_relations(query);
    for (size_t i = 0; i < relation_count; ++i) {
      hash_u64(&hash, relations[i].kind);
      hash_u64(&hash, relations[i].statement_index);
      hash_u64(&hash, relations[i].parent_relation);
    }
    size_t predicate_count = sequel_query_predicate_count(query);
    const sequel_predicate_info_t *predicates = sequel_query_predicates(query);
    for (size_t i = 0; i < predicate_count; ++i) {
      hash_u64(&hash, predicates[i].statement_index);
      hash_u64(&hash, predicates[i].normalized_hash);
      hash_u64(&hash, predicates[i].depth);
    }
    size_t parameter_count = sequel_query_parameter_count(query);
    const sequel_parameter_info_t *parameters = sequel_query_parameters(query);
    for (size_t i = 0; i < parameter_count; ++i) {
      hash_u64(&hash, parameters[i].style);
      hash_u64(&hash, parameters[i].statement_index);
      hash_u64(&hash, parameters[i].identity);
    }
  }
  snprintf(out, SEQUEL_FINGERPRINT_LENGTH, "%016llx", (unsigned long long)hash);
  return SEQUEL_OK;
}
