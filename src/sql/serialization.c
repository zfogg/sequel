#include <stdlib.h>
#include <string.h>

#include <sequel/serialization.h>
#include <sequel/fingerprint.h>

#define SEQUEL_DOCUMENT_SCHEMA_MAJOR 1u
#define SEQUEL_DOCUMENT_SCHEMA_MINOR 2u

struct sequel_document_bundle {
  sequel_syntax_document_t *syntax;
  sequel_query_document_t *query;
};

static int append_bytes(sequel_buffer_t *buffer, const void *data, size_t length) {
  if (length > SIZE_MAX - buffer->length)
    return 0;
  size_t required = buffer->length + length;
  if (required > buffer->capacity) {
    size_t capacity = buffer->capacity ? buffer->capacity : 256;
    while (capacity < required) {
      if (capacity > SIZE_MAX / 2)
        return 0;
      capacity *= 2;
    }
    uint8_t *grown = realloc(buffer->data, capacity);
    if (!grown)
      return 0;
    buffer->data = grown;
    buffer->capacity = capacity;
  }
  memcpy(buffer->data + buffer->length, data, length);
  buffer->length = required;
  return 1;
}

static int append_u64(sequel_buffer_t *buffer, uint64_t value) { return append_bytes(buffer, &value, sizeof(value)); }

static int append_range(sequel_buffer_t *buffer, sequel_source_range_t range) {
  return append_u64(buffer, range.start) && append_u64(buffer, range.end);
}

static int read_u64(const uint8_t **cursor, size_t *remaining, uint64_t *value) {
  if (*remaining < sizeof(*value))
    return 0;
  memcpy(value, *cursor, sizeof(*value));
  *cursor += sizeof(*value);
  *remaining -= sizeof(*value);
  return 1;
}

static int serialized_range_valid(uint64_t start, uint64_t end, size_t source_length) {
  return start <= end && end <= source_length;
}

static int read_serialized_range(const uint8_t **cursor, size_t *remaining, size_t source_length) {
  uint64_t start = 0, end = 0;
  return read_u64(cursor, remaining, &start) && read_u64(cursor, remaining, &end) &&
         serialized_range_valid(start, end, source_length);
}

static int add_words(uint64_t *total, uint64_t count, uint64_t words_per_record) {
  if (count && words_per_record > UINT64_MAX / count)
    return 0;
  uint64_t words = count * words_per_record;
  if (words > UINT64_MAX - *total)
    return 0;
  *total += words;
  return 1;
}

static int serialized_index_valid(uint64_t index, uint64_t count) { return index == UINT64_MAX || index < count; }

static int read_serialized_u64(const uint8_t **cursor, size_t *remaining, uint64_t *value) {
  return read_u64(cursor, remaining, value);
}

static int read_serialized_query_range(const uint8_t **cursor, size_t *remaining, size_t source_length) {
  return read_serialized_range(cursor, remaining, source_length);
}

void sequel_serialization_options_init(sequel_serialization_options_t *options) {
  if (options)
    *options = (sequel_serialization_options_t){{SEQUEL_DOCUMENT_SCHEMA_MAJOR, SEQUEL_DOCUMENT_SCHEMA_MINOR}};
}

void sequel_buffer_free(sequel_buffer_t *buffer) {
  if (!buffer)
    return;
  free(buffer->data);
  *buffer = (sequel_buffer_t){0};
}

sequel_status_t sequel_document_serialize(const sequel_syntax_document_t *syntax, const sequel_query_document_t *query,
                                          const sequel_serialization_options_t *options, sequel_buffer_t *out) {
  if (!syntax || !out)
    return SEQUEL_ERR_NULL;
  sequel_serialization_options_t defaults;
  sequel_serialization_options_init(&defaults);
  const sequel_serialization_options_t *settings = options ? options : &defaults;
  if (settings->schema.major != SEQUEL_DOCUMENT_SCHEMA_MAJOR)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  sequel_buffer_free(out);
  const sequel_lexed_document_t *lexed = sequel_syntax_lexed(syntax);
  sequel_source_view_t source = sequel_lexed_source(lexed);
  size_t token_count = 0, node_count = 0, child_count = 0, issue_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  const sequel_syntax_node_t *nodes = sequel_syntax_nodes(syntax, &node_count);
  (void)sequel_syntax_issues(syntax, &issue_count);
  for (size_t i = 0; i < node_count; ++i)
    child_count += nodes[i].child_count;
  uint64_t header[] = {0x31544451534c4553ull,
                       settings->schema.major,
                       settings->schema.minor,
                       sequel_lexed_dialect(lexed),
                       source.length,
                       token_count,
                       node_count,
                       child_count,
                       issue_count,
                       query ? sequel_query_statement_count(query) : 0,
                       query ? sequel_query_relation_count(query) : 0,
                       query ? sequel_query_parameter_count(query) : 0,
                       query ? sequel_query_join_count(query) : 0,
                       query ? sequel_query_projection_count(query) : 0,
                       query ? sequel_query_predicate_count(query) : 0,
                       9};
  if (!append_bytes(out, header, sizeof(header)))
    return SEQUEL_ERR_NOMEM;
  for (size_t i = 0; i < token_count; ++i) {
    uint64_t record[] = {tokens[i].kind,
                         tokens[i].flags,
                         tokens[i].range.start,
                         tokens[i].range.end,
                         tokens[i].leading_trivia.start,
                         tokens[i].leading_trivia.end,
                         tokens[i].trailing_trivia.start,
                         tokens[i].trailing_trivia.end,
                         tokens[i].keyword_id,
                         tokens[i].dialect_id};
    if (!append_bytes(out, record, sizeof(record)))
      return SEQUEL_ERR_NOMEM;
  }
  for (size_t i = 0; i < node_count; ++i) {
    uint64_t record[] = {nodes[i].kind,
                         nodes[i].flags,
                         nodes[i].range.start,
                         nodes[i].range.end,
                         nodes[i].header_range.start,
                         nodes[i].header_range.end,
                         nodes[i].first_child,
                         nodes[i].child_count,
                         nodes[i].first_token,
                         nodes[i].token_count,
                         nodes[i].production_id};
    if (!append_bytes(out, record, sizeof(record)))
      return SEQUEL_ERR_NOMEM;
  }
  size_t issue_count_for_serialization = 0;
  const sequel_parse_issue_t *issues = sequel_syntax_issues(syntax, &issue_count_for_serialization);
  for (size_t i = 0; i < issue_count_for_serialization; ++i)
    if (!append_u64(out, issues[i].code) || !append_u64(out, issues[i].recovery) ||
        !append_range(out, issues[i].range) || !append_u64(out, issues[i].statement_index))
      return SEQUEL_ERR_NOMEM;
  for (size_t i = 0; i < node_count; ++i) {
    size_t count = 0;
    const size_t *node_children = sequel_syntax_children(syntax, i, &count);
    for (size_t j = 0; j < count; ++j)
      if (!append_u64(out, node_children[j]))
        return SEQUEL_ERR_NOMEM;
  }
  if (query) {
    size_t count = sequel_query_statement_count(query);
    const sequel_statement_info_t *statements = sequel_query_statements(query);
    for (size_t i = 0; i < count; ++i)
      if (!append_range(out, statements[i].range) || !append_u64(out, statements[i].operation) ||
          !append_u64(out, statements[i].syntax_node) || !append_u64(out, statements[i].query_body) ||
          !append_u64(out, statements[i].flags))
        return SEQUEL_ERR_NOMEM;
    count = sequel_query_relation_count(query);
    const sequel_relation_info_t *relations = sequel_query_relations(query);
    for (size_t i = 0; i < count; ++i)
      if (!append_u64(out, relations[i].kind) || !append_range(out, relations[i].range) ||
          !append_range(out, relations[i].name_range) || !append_range(out, relations[i].alias_range) ||
          !append_u64(out, relations[i].statement_index) || !append_u64(out, relations[i].syntax_node) ||
          !append_u64(out, relations[i].parent_relation) || !append_u64(out, relations[i].subquery_statement) ||
          !append_u64(out, relations[i].flags))
        return SEQUEL_ERR_NOMEM;
    count = sequel_query_parameter_count(query);
    const sequel_parameter_info_t *parameters = sequel_query_parameters(query);
    for (size_t i = 0; i < count; ++i)
      if (!append_range(out, parameters[i].range) || !append_u64(out, parameters[i].style) ||
          !append_u64(out, parameters[i].statement_index) || !append_u64(out, parameters[i].ordinal) ||
          !append_u64(out, parameters[i].explicit_number) || !append_range(out, parameters[i].name_range) ||
          !append_range(out, parameters[i].type_range) || !append_u64(out, parameters[i].identity))
        return SEQUEL_ERR_NOMEM;
    count = sequel_query_join_count(query);
    const sequel_join_info_t *joins = sequel_query_joins(query);
    for (size_t i = 0; i < count; ++i)
      if (!append_u64(out, joins[i].kind) || !append_range(out, joins[i].range) ||
          !append_u64(out, joins[i].left_relation) || !append_u64(out, joins[i].right_relation) ||
          !append_u64(out, joins[i].condition_node) || !append_u64(out, joins[i].has_condition))
        return SEQUEL_ERR_NOMEM;
    count = sequel_query_projection_count(query);
    const sequel_projection_info_t *projections = sequel_query_projections(query);
    for (size_t i = 0; i < count; ++i)
      if (!append_range(out, projections[i].range) || !append_u64(out, projections[i].statement_index) ||
          !append_u64(out, projections[i].expression_node) || !append_u64(out, projections[i].wildcard) ||
          !append_range(out, projections[i].alias_range))
        return SEQUEL_ERR_NOMEM;
    count = sequel_query_predicate_count(query);
    const sequel_predicate_info_t *predicates = sequel_query_predicates(query);
    for (size_t i = 0; i < count; ++i)
      if (!append_range(out, predicates[i].range) || !append_u64(out, predicates[i].statement_index) ||
          !append_u64(out, predicates[i].expression_node) || !append_u64(out, predicates[i].normalized_hash) ||
          !append_u64(out, predicates[i].depth))
        return SEQUEL_ERR_NOMEM;
  }
  char fingerprint[SEQUEL_FINGERPRINT_LENGTH];
  if (sequel_fingerprint_document(syntax, query, SEQUEL_FINGERPRINT_QUERY_SHAPE, fingerprint) != SEQUEL_OK)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  for (size_t i = 0; i < 9; ++i) {
    uint64_t word = 0;
    size_t offset = i * sizeof(word);
    size_t length = sizeof(word);
    if (offset + length > SEQUEL_FINGERPRINT_LENGTH)
      length = SEQUEL_FINGERPRINT_LENGTH - offset;
    memcpy(&word, fingerprint + offset, length);
    if (!append_u64(out, word))
      return SEQUEL_ERR_NOMEM;
  }
  return SEQUEL_OK;
}

sequel_status_t sequel_document_deserialize(const sequel_source_view_t *source, const sequel_buffer_view_t *serialized,
                                            const sequel_serialization_options_t *options,
                                            sequel_document_bundle_t **out) {
  if (!source || !serialized || !out)
    return SEQUEL_ERR_NULL;
  *out = NULL;
  const uint8_t *cursor = serialized->data;
  size_t remaining = serialized->length;
  uint64_t header[16];
  for (size_t i = 0; i < 16; ++i)
    if (!read_u64(&cursor, &remaining, &header[i]))
      return SEQUEL_ERR_INVALID_ARGUMENT;
  if (header[0] != 0x31544451534c4553ull || header[1] != SEQUEL_DOCUMENT_SCHEMA_MAJOR ||
      header[2] > SEQUEL_DOCUMENT_SCHEMA_MINOR || header[3] > SEQUEL_CLICKHOUSE ||
      (options && (options->schema.major != header[1] || options->schema.minor < header[2])))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (header[4] != source->length)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  uint64_t words = 0;
  if (!add_words(&words, header[5], 10) || !add_words(&words, header[6], 11) || !add_words(&words, header[7], 1) ||
      !add_words(&words, header[8], 5) || !add_words(&words, header[9], 6) || !add_words(&words, header[10], 11) ||
      !add_words(&words, header[11], 10) || !add_words(&words, header[12], 6) || !add_words(&words, header[13], 6) ||
      !add_words(&words, header[14], 6) || !add_words(&words, header[15], 1))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (words > SIZE_MAX / sizeof(uint64_t) || remaining < words * sizeof(uint64_t) || remaining % sizeof(uint64_t) != 0)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  const uint8_t *validation_cursor = cursor;
  size_t validation_remaining = remaining;
  uint64_t serialized_fingerprint[9] = {0};
  for (uint64_t i = 0; i < header[5]; ++i) {
    uint64_t record[10];
    for (size_t j = 0; j < 10; ++j)
      if (!read_u64(&validation_cursor, &validation_remaining, &record[j]))
        return SEQUEL_ERR_INVALID_ARGUMENT;
    if (record[0] > SEQUEL_TOKEN_ERROR || !serialized_range_valid(record[2], record[3], source->length) ||
        !serialized_range_valid(record[4], record[5], source->length) ||
        !serialized_range_valid(record[6], record[7], source->length))
      return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  for (uint64_t i = 0; i < header[6]; ++i) {
    uint64_t record[11];
    for (size_t j = 0; j < 11; ++j)
      if (!read_u64(&validation_cursor, &validation_remaining, &record[j]))
        return SEQUEL_ERR_INVALID_ARGUMENT;
    if (record[0] > SEQUEL_SYNTAX_ERROR || !serialized_range_valid(record[2], record[3], source->length) ||
        !serialized_range_valid(record[4], record[5], source->length) || record[6] > header[6] ||
        record[6] + record[7] > header[7] || record[8] > header[5] || record[8] + record[9] > header[5])
      return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  for (uint64_t i = 0; i < header[8]; ++i) {
    uint64_t code, recovery, statement;
    if (!read_u64(&validation_cursor, &validation_remaining, &code) ||
        !read_u64(&validation_cursor, &validation_remaining, &recovery) ||
        !read_serialized_range(&validation_cursor, &validation_remaining, source->length) ||
        !read_u64(&validation_cursor, &validation_remaining, &statement) ||
        code > SEQUEL_PARSE_ISSUE_UNEXPECTED_TOKEN || recovery > SEQUEL_RECOVERY_OPAQUE_REGION)
      return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  for (uint64_t i = 0; i < header[7]; ++i) {
    uint64_t child_index;
    if (!read_u64(&validation_cursor, &validation_remaining, &child_index) || child_index >= header[6])
      return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  for (uint64_t i = 0; i < header[9]; ++i) {
    uint64_t range_start, range_end, operation, syntax_node, query_body, flags;
    if (!read_serialized_u64(&validation_cursor, &validation_remaining, &range_start) ||
        !read_serialized_u64(&validation_cursor, &validation_remaining, &range_end) ||
        !serialized_range_valid(range_start, range_end, source->length) ||
        !read_serialized_u64(&validation_cursor, &validation_remaining, &operation) ||
        !read_serialized_u64(&validation_cursor, &validation_remaining, &syntax_node) ||
        !read_serialized_u64(&validation_cursor, &validation_remaining, &query_body) ||
        !read_serialized_u64(&validation_cursor, &validation_remaining, &flags) ||
        operation > SEQUEL_OPERATION_EXPLAIN || !serialized_index_valid(syntax_node, header[6]) ||
        !serialized_index_valid(query_body, header[6]))
      return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  for (uint64_t i = 0; i < header[10]; ++i) {
    uint64_t values[12];
    for (size_t j = 0; j < 12; ++j)
      if (!read_serialized_u64(&validation_cursor, &validation_remaining, &values[j]))
        return SEQUEL_ERR_INVALID_ARGUMENT;
    if (values[0] > SEQUEL_RELATION_CTE || !serialized_range_valid(values[1], values[2], source->length) ||
        !serialized_range_valid(values[3], values[4], source->length) ||
        !serialized_range_valid(values[5], values[6], source->length) ||
        !serialized_index_valid(values[7], header[9]) || !serialized_index_valid(values[8], header[6]) ||
        !serialized_index_valid(values[9], header[10]) || !serialized_index_valid(values[10], header[9]))
      return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  for (uint64_t i = 0; i < header[11]; ++i) {
    uint64_t values[11];
    for (size_t j = 0; j < 11; ++j)
      if (!read_serialized_u64(&validation_cursor, &validation_remaining, &values[j]))
        return SEQUEL_ERR_INVALID_ARGUMENT;
    if (!serialized_range_valid(values[0], values[1], source->length) || values[2] > SEQUEL_BIND_TYPED ||
        !serialized_index_valid(values[3], header[9]) ||
        !serialized_range_valid(values[6], values[7], source->length) ||
        !serialized_range_valid(values[8], values[9], source->length))
      return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  for (uint64_t i = 0; i < header[12]; ++i) {
    uint64_t values[6];
    for (size_t j = 0; j < 6; ++j)
      if (!read_serialized_u64(&validation_cursor, &validation_remaining, &values[j]))
        return SEQUEL_ERR_INVALID_ARGUMENT;
    if (values[0] > SEQUEL_JOIN_CROSS || !serialized_range_valid(values[1], values[2], source->length) ||
        !serialized_index_valid(values[3], header[10]) || !serialized_index_valid(values[4], header[10]))
      return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  for (uint64_t i = 0; i < header[13]; ++i) {
    uint64_t values[7];
    for (size_t j = 0; j < 7; ++j)
      if (!read_serialized_u64(&validation_cursor, &validation_remaining, &values[j]))
        return SEQUEL_ERR_INVALID_ARGUMENT;
    if (!serialized_range_valid(values[0], values[1], source->length) ||
        !serialized_index_valid(values[2], header[9]) || !serialized_index_valid(values[3], header[6]) ||
        !serialized_range_valid(values[5], values[6], source->length))
      return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  for (uint64_t i = 0; i < header[14]; ++i) {
    uint64_t values[6];
    for (size_t j = 0; j < 6; ++j)
      if (!read_serialized_u64(&validation_cursor, &validation_remaining, &values[j]))
        return SEQUEL_ERR_INVALID_ARGUMENT;
    if (!serialized_range_valid(values[0], values[1], source->length) ||
        !serialized_index_valid(values[2], header[9]) || !serialized_index_valid(values[3], header[6]))
      return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  if (header[15] != 9)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  for (uint64_t i = 0; i < header[15]; ++i) {
    if (!read_u64(&validation_cursor, &validation_remaining, &serialized_fingerprint[i]))
      return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  if (validation_remaining != 0)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  sequel_parse_options_t parse_options;
  sequel_parse_options_init(&parse_options);
  parse_options.dialect = (sequel_dialect_t)header[3];
  sequel_document_bundle_t *bundle = calloc(1, sizeof(*bundle));
  if (!bundle)
    return SEQUEL_ERR_NOMEM;
  sequel_status_t status = sequel_parse_sql(source, &parse_options, NULL, &bundle->syntax);
  if (status == SEQUEL_OK)
    status = sequel_build_query_document(bundle->syntax, &parse_options, &bundle->query);
  if (status != SEQUEL_OK) {
    sequel_document_bundle_free(bundle);
    return status;
  }
  if (sequel_lexed_token_count(sequel_syntax_lexed(bundle->syntax)) != header[5] ||
      sequel_syntax_node_count(bundle->syntax) != header[6] || sequel_syntax_issue_count(bundle->syntax) != header[8] ||
      sequel_query_statement_count(bundle->query) != header[9] ||
      sequel_query_relation_count(bundle->query) != header[10] ||
      sequel_query_parameter_count(bundle->query) != header[11] ||
      sequel_query_join_count(bundle->query) != header[12] ||
      sequel_query_projection_count(bundle->query) != header[13] ||
      sequel_query_predicate_count(bundle->query) != header[14]) {
    sequel_document_bundle_free(bundle);
    return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  char rebuilt_fingerprint[SEQUEL_FINGERPRINT_LENGTH];
  if (sequel_fingerprint_document(bundle->syntax, bundle->query, SEQUEL_FINGERPRINT_QUERY_SHAPE, rebuilt_fingerprint) !=
          SEQUEL_OK ||
      strcmp((const char *)serialized_fingerprint, rebuilt_fingerprint) != 0) {
    sequel_document_bundle_free(bundle);
    return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  *out = bundle;
  return SEQUEL_OK;
}

void sequel_document_bundle_free(sequel_document_bundle_t *bundle) {
  if (!bundle)
    return;
  sequel_query_document_free(bundle->query);
  sequel_syntax_document_free(bundle->syntax);
  free(bundle);
}

const sequel_syntax_document_t *sequel_document_bundle_syntax(const sequel_document_bundle_t *bundle) {
  return bundle ? bundle->syntax : NULL;
}

const sequel_query_document_t *sequel_document_bundle_query(const sequel_document_bundle_t *bundle) {
  return bundle ? bundle->query : NULL;
}
