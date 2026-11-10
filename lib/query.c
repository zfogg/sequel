#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <sequel/query.h>

struct sequel_query_document {
  const sequel_syntax_document_t *syntax;
  sequel_statement_info_t *statements;
  size_t statement_count;
  sequel_relation_info_t *relations;
  size_t relation_count;
  sequel_parameter_info_t *parameters;
  size_t parameter_count;
  sequel_join_info_t *joins;
  size_t join_count;
  sequel_projection_info_t *projections;
  size_t projection_count;
  sequel_predicate_info_t *predicates;
  size_t predicate_count;
};

static int append(void **array, size_t *count, size_t size, const void *value) {
  void *grown = realloc(*array, (*count + 1) * size);
  if (!grown)
    return 0;
  *array = grown;
  memcpy((char *)grown + *count * size, value, size);
  ++*count;
  return 1;
}

static sequel_operation_t operation(sequel_syntax_kind_t kind) {
  switch (kind) {
  case SEQUEL_SYNTAX_SELECT_STATEMENT:
    return SEQUEL_OPERATION_SELECT;
  case SEQUEL_SYNTAX_INSERT_STATEMENT:
    return SEQUEL_OPERATION_INSERT;
  case SEQUEL_SYNTAX_UPDATE_STATEMENT:
    return SEQUEL_OPERATION_UPDATE;
  case SEQUEL_SYNTAX_DELETE_STATEMENT:
    return SEQUEL_OPERATION_DELETE;
  case SEQUEL_SYNTAX_MERGE_STATEMENT:
    return SEQUEL_OPERATION_MERGE;
  case SEQUEL_SYNTAX_CREATE_STATEMENT:
    return SEQUEL_OPERATION_CREATE;
  case SEQUEL_SYNTAX_ALTER_STATEMENT:
    return SEQUEL_OPERATION_ALTER;
  case SEQUEL_SYNTAX_DROP_STATEMENT:
    return SEQUEL_OPERATION_DROP;
  case SEQUEL_SYNTAX_TRANSACTION_STATEMENT:
    return SEQUEL_OPERATION_TRANSACTION;
  case SEQUEL_SYNTAX_EXPLAIN_STATEMENT:
    return SEQUEL_OPERATION_EXPLAIN;
  default:
    return SEQUEL_OPERATION_UNKNOWN;
  }
}

static size_t statement_for_range(const sequel_query_document_t *document, sequel_source_range_t range) {
  for (size_t i = 0; i < document->statement_count; ++i)
    if (sequel_source_range_contains(document->statements[i].range, range))
      return i;
  return SIZE_MAX;
}

static sequel_bind_style_t bind_style(const char *text, size_t length, size_t *number) {
  *number = 0;
  if (!length)
    return SEQUEL_BIND_UNKNOWN;
  if (text[0] == '?') {
    if (length == 1)
      return SEQUEL_BIND_POSITIONAL;
    for (size_t i = 1; i < length; ++i) {
      if (!isdigit((unsigned char)text[i]))
        return SEQUEL_BIND_UNKNOWN;
      *number = *number * 10 + (size_t)(text[i] - '0');
    }
    return *number ? SEQUEL_BIND_NUMBERED : SEQUEL_BIND_UNKNOWN;
  }
  if (text[0] == '$') {
    if (length == 1)
      return SEQUEL_BIND_UNKNOWN;
    bool numeric = true;
    for (size_t i = 1; i < length; ++i) {
      if (!isdigit((unsigned char)text[i]))
        numeric = false;
      else
        *number = *number * 10 + (size_t)(text[i] - '0');
    }
    return numeric && *number ? SEQUEL_BIND_NUMBERED : SEQUEL_BIND_NAMED;
  }
  if (text[0] == ':' || text[0] == '@')
    return length > 1 ? SEQUEL_BIND_NAMED : SEQUEL_BIND_UNKNOWN;
  if (text[0] == '%')
    return length >= 2 && (text[1] == 's' || text[1] == '(') ? SEQUEL_BIND_FORMAT : SEQUEL_BIND_UNKNOWN;
  return SEQUEL_BIND_UNKNOWN;
}

static int is_identifier_token(const sequel_token_t *token) {
  return token->kind == SEQUEL_TOKEN_IDENTIFIER || token->kind == SEQUEL_TOKEN_QUOTED_IDENTIFIER;
}

static int is_clause_keyword(const sequel_token_t *token) {
  if (token->kind != SEQUEL_TOKEN_KEYWORD)
    return 0;
  return token->keyword_id == SEQUEL_KEYWORD_WHERE || token->keyword_id == SEQUEL_KEYWORD_GROUP ||
         token->keyword_id == SEQUEL_KEYWORD_ORDER || token->keyword_id == SEQUEL_KEYWORD_LIMIT ||
         token->keyword_id == SEQUEL_KEYWORD_OFFSET || token->keyword_id == SEQUEL_KEYWORD_JOIN ||
         token->keyword_id == SEQUEL_KEYWORD_ON || token->keyword_id == SEQUEL_KEYWORD_HAVING ||
         token->keyword_id == SEQUEL_KEYWORD_RETURNING || token->keyword_id == SEQUEL_KEYWORD_VALUES ||
         token->keyword_id == SEQUEL_KEYWORD_SET || token->keyword_id == SEQUEL_KEYWORD_INTO ||
         token->keyword_id == SEQUEL_KEYWORD_USING || token->keyword_id == SEQUEL_KEYWORD_WINDOW ||
         token->keyword_id == SEQUEL_KEYWORD_QUALIFY || token->keyword_id == SEQUEL_KEYWORD_PREWHERE;
}

static int is_trivia(const sequel_token_t *token) {
  return token->kind == SEQUEL_TOKEN_WHITESPACE || token->kind == SEQUEL_TOKEN_LINE_COMMENT ||
         token->kind == SEQUEL_TOKEN_BLOCK_COMMENT;
}

static uint64_t range_hash(const sequel_source_view_t *source, sequel_source_range_t range) {
  uint64_t hash = 1469598103934665603ull;
  for (size_t i = range.start; i < range.end; ++i)
    hash = (hash ^ (unsigned char)source->data[i]) * 1099511628211ull;
  return hash;
}

sequel_status_t sequel_build_query_document(const sequel_syntax_document_t *syntax,
                                            const sequel_parse_options_t *options, sequel_query_document_t **out) {
  if (!syntax || !out)
    return SEQUEL_ERR_NULL;
  *out = NULL;
  sequel_query_document_t *document = calloc(1, sizeof(*document));
  if (!document)
    return SEQUEL_ERR_NOMEM;
  document->syntax = syntax;
  size_t node_count = 0;
  const sequel_syntax_node_t *nodes = sequel_syntax_nodes(syntax, &node_count);
  size_t max_nodes = options && options->max_nodes ? options->max_nodes : (size_t)-1;
  for (size_t i = 0; i < node_count && document->statement_count < max_nodes; ++i) {
    sequel_syntax_kind_t kind = nodes[i].kind;
    if (kind < SEQUEL_SYNTAX_SELECT_STATEMENT || kind > SEQUEL_SYNTAX_UNKNOWN_STATEMENT)
      continue;
    sequel_statement_info_t statement = {
        .range = nodes[i].range, .operation = operation(kind), .syntax_node = i, .query_body = SIZE_MAX};
    if (!append((void **)&document->statements, &document->statement_count, sizeof(statement), &statement)) {
      sequel_query_document_free(document);
      return SEQUEL_ERR_NOMEM;
    }
  }
  const sequel_lexed_document_t *lexed = sequel_syntax_lexed(syntax);
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  sequel_source_view_t source = sequel_lexed_source(lexed);
  size_t ordinal = 0;
  for (size_t i = 0; i < token_count; ++i) {
    if (tokens[i].kind != SEQUEL_TOKEN_PARAMETER)
      continue;
    const char *text = source.data + tokens[i].range.start;
    size_t length = tokens[i].range.end - tokens[i].range.start;
    size_t explicit_number = 0;
    sequel_bind_style_t style = bind_style(text, length, &explicit_number);
    if (style == SEQUEL_BIND_UNKNOWN)
      continue;
    ++ordinal;
    size_t name_start = tokens[i].range.start + (style == SEQUEL_BIND_POSITIONAL ? 0 : 1);
    size_t name_end = tokens[i].range.end;
    if (style == SEQUEL_BIND_FORMAT && text[1] == '(') {
      name_start = tokens[i].range.start + 2;
      name_end = tokens[i].range.end >= 2 && source.data[tokens[i].range.end - 2] == ')' ? tokens[i].range.end - 2
                                                                                         : tokens[i].range.end;
    }
    sequel_source_range_t name_range = {name_start, name_end};
    uint64_t identity = explicit_number ? explicit_number : ordinal;
    if (!explicit_number && (style == SEQUEL_BIND_NAMED || style == SEQUEL_BIND_TYPED || style == SEQUEL_BIND_FORMAT)) {
      for (size_t previous = 0; previous < document->parameter_count; ++previous) {
        const sequel_parameter_info_t *candidate = &document->parameters[previous];
        size_t candidate_length = candidate->name_range.end - candidate->name_range.start;
        size_t name_length = name_range.end - name_range.start;
        if (candidate->style == style && candidate_length == name_length &&
            !memcmp(source.data + candidate->name_range.start, source.data + name_range.start, name_length)) {
          identity = candidate->identity;
          break;
        }
      }
    }
    sequel_parameter_info_t parameter = {.range = tokens[i].range,
                                         .style = style,
                                         .statement_index = statement_for_range(document, tokens[i].range),
                                         .ordinal = ordinal,
                                         .explicit_number = explicit_number,
                                         .name_range = name_range,
                                         .type_range = {tokens[i].range.end, tokens[i].range.end},
                                         .identity = identity};
    if (!append((void **)&document->parameters, &document->parameter_count, sizeof(parameter), &parameter)) {
      sequel_query_document_free(document);
      return SEQUEL_ERR_NOMEM;
    }
  }
  for (size_t s = 0; s < document->statement_count; ++s) {
    const sequel_statement_info_t *statement = &document->statements[s];
    size_t first = statement->syntax_node < node_count ? nodes[statement->syntax_node].first_token : 0;
    size_t end = first + (statement->syntax_node < node_count ? nodes[statement->syntax_node].token_count : 0);
    size_t select = first;
    while (select < end &&
           !(tokens[select].kind == SEQUEL_TOKEN_KEYWORD && tokens[select].keyword_id == SEQUEL_KEYWORD_SELECT))
      ++select;
    if (select == end)
      continue;
    size_t item_start = select + 1;
    int depth = 0;
    for (size_t i = item_start; i <= end; ++i) {
      bool boundary = i == end;
      if (!boundary && tokens[i].kind == SEQUEL_TOKEN_PUNCTUATION) {
        char c = source.data[tokens[i].range.start];
        if (c == '(')
          ++depth;
        else if (c == ')' && depth)
          --depth;
        boundary = depth == 0 && c == ',';
      }
      if (!boundary && i < end && tokens[i].kind == SEQUEL_TOKEN_KEYWORD &&
          tokens[i].keyword_id == SEQUEL_KEYWORD_FROM && depth == 0)
        boundary = true;
      if (!boundary)
        continue;
      size_t first_token = item_start;
      while (first_token < i && is_trivia(&tokens[first_token]))
        ++first_token;
      size_t last_token = i;
      while (last_token > first_token && is_trivia(&tokens[last_token - 1]))
        --last_token;
      if (first_token < last_token) {
        sequel_projection_info_t projection = {
            .range = {tokens[first_token].range.start, tokens[last_token - 1].range.end},
            .statement_index = s,
            .expression_node = SIZE_MAX,
            .wildcard = last_token == first_token + 1 && tokens[first_token].kind == SEQUEL_TOKEN_OPERATOR &&
                        source.data[tokens[first_token].range.start] == '*',
            .alias_range = {tokens[last_token - 1].range.end, tokens[last_token - 1].range.end}};
        if (!append((void **)&document->projections, &document->projection_count, sizeof(projection), &projection)) {
          sequel_query_document_free(document);
          return SEQUEL_ERR_NOMEM;
        }
      }
      item_start = i + (i < end ? 1 : 0);
      if (i < end && tokens[i].kind == SEQUEL_TOKEN_KEYWORD && tokens[i].keyword_id == SEQUEL_KEYWORD_FROM)
        break;
    }
    for (size_t i = first; i < end; ++i) {
      if (tokens[i].kind != SEQUEL_TOKEN_KEYWORD ||
          (tokens[i].keyword_id != SEQUEL_KEYWORD_WHERE && tokens[i].keyword_id != SEQUEL_KEYWORD_PREWHERE))
        continue;
      size_t predicate_start = i + 1;
      while (predicate_start < end && is_trivia(&tokens[predicate_start]))
        ++predicate_start;
      size_t predicate_end = end;
      for (size_t j = predicate_start; j < end; ++j)
        if (tokens[j].kind == SEQUEL_TOKEN_KEYWORD && is_clause_keyword(&tokens[j])) {
          predicate_end = j;
          break;
        }
      while (predicate_end > predicate_start && is_trivia(&tokens[predicate_end - 1]))
        --predicate_end;
      if (predicate_start < predicate_end) {
        sequel_predicate_info_t predicate = {
            .range = {tokens[predicate_start].range.start, tokens[predicate_end - 1].range.end},
            .statement_index = s,
            .expression_node = SIZE_MAX,
            .normalized_hash = range_hash(&source, (sequel_source_range_t){tokens[predicate_start].range.start,
                                                                           tokens[predicate_end - 1].range.end}),
            .depth = 0};
        if (!append((void **)&document->predicates, &document->predicate_count, sizeof(predicate), &predicate)) {
          sequel_query_document_free(document);
          return SEQUEL_ERR_NOMEM;
        }
      }
      break;
    }
  }
  for (size_t i = 0; i < token_count; ++i) {
    if (tokens[i].kind != SEQUEL_TOKEN_KEYWORD ||
        (tokens[i].keyword_id != SEQUEL_KEYWORD_FROM && tokens[i].keyword_id != SEQUEL_KEYWORD_JOIN &&
         tokens[i].keyword_id != SEQUEL_KEYWORD_ON))
      continue;
    size_t statement = statement_for_range(document, tokens[i].range);
    if (tokens[i].keyword_id == SEQUEL_KEYWORD_ON &&
        (statement == SIZE_MAX || document->statements[statement].operation != SEQUEL_OPERATION_CREATE))
      continue;
    size_t name = i + 1;
    while (name < token_count &&
           (tokens[name].kind == SEQUEL_TOKEN_WHITESPACE || tokens[name].kind == SEQUEL_TOKEN_LINE_COMMENT ||
            tokens[name].kind == SEQUEL_TOKEN_BLOCK_COMMENT))
      ++name;
    if (name >= token_count || !is_identifier_token(&tokens[name]))
      continue;
    size_t end = tokens[name].range.end;
    size_t cursor = name + 1;
    while (cursor + 1 < token_count && tokens[cursor].kind == SEQUEL_TOKEN_PUNCTUATION &&
           source.data[tokens[cursor].range.start] == '.' && is_identifier_token(&tokens[cursor + 1])) {
      end = tokens[cursor + 1].range.end;
      cursor += 2;
    }
    sequel_source_range_t range = {tokens[i].range.start, end};
    statement = statement_for_range(document, range);
    size_t alias = cursor;
    while (alias < token_count &&
           (tokens[alias].kind == SEQUEL_TOKEN_WHITESPACE || tokens[alias].kind == SEQUEL_TOKEN_LINE_COMMENT ||
            tokens[alias].kind == SEQUEL_TOKEN_BLOCK_COMMENT))
      ++alias;
    sequel_source_range_t alias_range = {end, end};
    if (alias < token_count && is_identifier_token(&tokens[alias]) && !is_clause_keyword(&tokens[alias]))
      alias_range = tokens[alias].range;
    sequel_relation_info_t relation = {.kind = SEQUEL_RELATION_TABLE,
                                       .range = {tokens[i].range.start, alias_range.end > end ? alias_range.end : end},
                                       .name_range = {tokens[name].range.start, end},
                                       .alias_range = alias_range,
                                       .statement_index = statement,
                                       .syntax_node = SIZE_MAX,
                                       .parent_relation = SIZE_MAX,
                                       .subquery_statement = SIZE_MAX};
    if (!append((void **)&document->relations, &document->relation_count, sizeof(relation), &relation)) {
      sequel_query_document_free(document);
      return SEQUEL_ERR_NOMEM;
    }
  }
  for (size_t i = 0; i < token_count; ++i) {
    if (tokens[i].kind != SEQUEL_TOKEN_KEYWORD || tokens[i].keyword_id != SEQUEL_KEYWORD_JOIN)
      continue;
    size_t left = SIZE_MAX;
    size_t right = SIZE_MAX;
    for (size_t r = 0; r < document->relation_count; ++r) {
      if (document->relations[r].range.start < tokens[i].range.start)
        left = r;
      else if (right == SIZE_MAX)
        right = r;
    }
    if (left == SIZE_MAX || right == SIZE_MAX)
      continue;
    sequel_join_info_t join = {.kind = SEQUEL_JOIN_INNER,
                               .range = {tokens[i].range.start, document->relations[right].range.end},
                               .left_relation = left,
                               .right_relation = right,
                               .condition_node = SIZE_MAX,
                               .has_condition = false};
    for (size_t j = i + 1; j < token_count && tokens[j].range.start < document->relations[right].range.start; ++j) {
      if (tokens[j].kind == SEQUEL_TOKEN_KEYWORD && tokens[j].keyword_id == SEQUEL_KEYWORD_ON) {
        join.has_condition = true;
        break;
      }
    }
    if (!append((void **)&document->joins, &document->join_count, sizeof(join), &join)) {
      sequel_query_document_free(document);
      return SEQUEL_ERR_NOMEM;
    }
  }
  *out = document;
  return SEQUEL_OK;
}

void sequel_query_document_free(sequel_query_document_t *document) {
  if (!document)
    return;
  free(document->statements);
  free(document->relations);
  free(document->parameters);
  free(document->joins);
  free(document->projections);
  free(document->predicates);
  free(document);
}

size_t sequel_query_statement_count(const sequel_query_document_t *document) {
  return document ? document->statement_count : 0;
}

const sequel_statement_info_t *sequel_query_statements(const sequel_query_document_t *document) {
  return document ? document->statements : NULL;
}

size_t sequel_query_relation_count(const sequel_query_document_t *document) {
  return document ? document->relation_count : 0;
}

const sequel_relation_info_t *sequel_query_relations(const sequel_query_document_t *document) {
  return document ? document->relations : NULL;
}

size_t sequel_query_parameter_count(const sequel_query_document_t *document) {
  return document ? document->parameter_count : 0;
}

const sequel_parameter_info_t *sequel_query_parameters(const sequel_query_document_t *document) {
  return document ? document->parameters : NULL;
}

size_t sequel_query_join_count(const sequel_query_document_t *document) { return document ? document->join_count : 0; }

const sequel_join_info_t *sequel_query_joins(const sequel_query_document_t *document) {
  return document ? document->joins : NULL;
}

size_t sequel_query_projection_count(const sequel_query_document_t *document) {
  return document ? document->projection_count : 0;
}

const sequel_projection_info_t *sequel_query_projections(const sequel_query_document_t *document) {
  return document ? document->projections : NULL;
}

size_t sequel_query_predicate_count(const sequel_query_document_t *document) {
  return document ? document->predicate_count : 0;
}

const sequel_predicate_info_t *sequel_query_predicates(const sequel_query_document_t *document) {
  return document ? document->predicates : NULL;
}

sequel_source_view_t sequel_query_source(const sequel_query_document_t *document) {
  return document ? sequel_lexed_source(sequel_syntax_lexed(document->syntax)) : (sequel_source_view_t){0};
}

sequel_status_t sequel_query_visit(const sequel_query_document_t *document, const sequel_query_visitor_t *visitor) {
  if (!document || !visitor)
    return SEQUEL_ERR_NULL;
  for (size_t i = 0; i < document->statement_count; ++i)
    if (visitor->statement && !visitor->statement(document, i, visitor->userdata))
      return SEQUEL_ERR_CANCELLED;
  for (size_t i = 0; i < document->relation_count; ++i)
    if (visitor->relation && !visitor->relation(document, i, visitor->userdata))
      return SEQUEL_ERR_CANCELLED;
  for (size_t i = 0; i < document->parameter_count; ++i)
    if (visitor->parameter && !visitor->parameter(document, i, visitor->userdata))
      return SEQUEL_ERR_CANCELLED;
  for (size_t i = 0; i < document->join_count; ++i)
    if (visitor->join && !visitor->join(document, i, visitor->userdata))
      return SEQUEL_ERR_CANCELLED;
  for (size_t i = 0; i < document->projection_count; ++i)
    if (visitor->projection && !visitor->projection(document, i, visitor->userdata))
      return SEQUEL_ERR_CANCELLED;
  for (size_t i = 0; i < document->predicate_count; ++i)
    if (visitor->predicate && !visitor->predicate(document, i, visitor->userdata))
      return SEQUEL_ERR_CANCELLED;
  return SEQUEL_OK;
}
