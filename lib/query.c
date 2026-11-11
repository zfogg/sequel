#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <sequel/query.h>
#include "util/text.h"

struct sequel_query_document {
  const sequel_syntax_document_t *syntax;
  sequel_statement_info_t *statements;
  size_t statement_count;
  sequel_relation_info_t *relations;
  size_t relation_count;
  sequel_cte_info_t *ctes;
  size_t cte_count;
  bool outer_distinct_on;
  bool outer_limit;
  sequel_source_range_t outer_range;
  sequel_parameter_info_t *parameters;
  size_t parameter_count;
  sequel_join_info_t *joins;
  size_t join_count;
  sequel_projection_info_t *projections;
  size_t projection_count;
  sequel_predicate_info_t *predicates;
  size_t predicate_count;
  sequel_clause_info_t *clauses;
  size_t clause_count;
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

static bool word_equals_ci(const char *word, size_t length, const char *expected) {
  size_t expected_length = strlen(expected);
  if (length != expected_length)
    return false;
  for (size_t i = 0; i < length; ++i)
    if (toupper((unsigned char)word[i]) != (unsigned char)expected[i])
      return false;
  return true;
}

static sequel_operation_t operation_from_word(const char *word, size_t length) {
  if (word_equals_ci(word, length, "SELECT"))
    return SEQUEL_OPERATION_SELECT;
  if (word_equals_ci(word, length, "INSERT"))
    return SEQUEL_OPERATION_INSERT;
  if (word_equals_ci(word, length, "UPDATE"))
    return SEQUEL_OPERATION_UPDATE;
  if (word_equals_ci(word, length, "DELETE"))
    return SEQUEL_OPERATION_DELETE;
  if (word_equals_ci(word, length, "MERGE"))
    return SEQUEL_OPERATION_MERGE;
  if (word_equals_ci(word, length, "OPTIMIZE"))
    return SEQUEL_OPERATION_OPTIMIZE;
  if (word_equals_ci(word, length, "TRUNCATE"))
    return SEQUEL_OPERATION_TRUNCATE;
  return SEQUEL_OPERATION_UNKNOWN;
}

static size_t statement_for_range(const sequel_query_document_t *document, sequel_source_range_t range) {
  for (size_t i = 0; i < document->statement_count; ++i)
    if (sequel_source_range_contains(document->statements[i].range, range))
      return i;
  return SIZE_MAX;
}

static bool is_expression_kind(sequel_syntax_kind_t kind) {
  return kind >= SEQUEL_SYNTAX_IDENTIFIER && kind <= SEQUEL_SYNTAX_GROUP;
}

static int is_trivia(const sequel_token_t *token);

static size_t expression_node_for_range(const sequel_syntax_document_t *syntax, sequel_source_range_t range) {
  size_t count = 0;
  const sequel_syntax_node_t *nodes = sequel_syntax_nodes(syntax, &count);
  size_t best = SIZE_MAX;
  size_t best_width = SIZE_MAX;
  for (size_t i = 0; i < count; ++i) {
    if (!is_expression_kind(nodes[i].kind) ||
        (!sequel_source_range_contains(range, nodes[i].range) && !sequel_source_range_contains(nodes[i].range, range)))
      continue;
    size_t width = nodes[i].range.end - nodes[i].range.start;
    if (width < best_width) {
      best = i;
      best_width = width;
    }
  }
  return best;
}

static sequel_join_kind_t join_kind(const sequel_token_t *tokens, size_t token_count, size_t join_index,
                                    const sequel_source_view_t *source) {
  size_t previous[3] = {SIZE_MAX, SIZE_MAX, SIZE_MAX};
  size_t found = 0;
  for (size_t cursor = join_index; cursor-- > 0 && found < 3;) {
    if (!is_trivia(&tokens[cursor]))
      previous[found++] = cursor;
  }
  if (join_index + 1 < token_count) {
    size_t next = join_index + 1;
    while (next < token_count && is_trivia(&tokens[next]))
      ++next;
    if (next < token_count && word_equals_ci(source->data + tokens[next].range.start,
                                             tokens[next].range.end - tokens[next].range.start, "LATERAL"))
      return SEQUEL_JOIN_LATERAL;
  }
  if (found && word_equals_ci(source->data + tokens[previous[0]].range.start,
                              tokens[previous[0]].range.end - tokens[previous[0]].range.start, "CROSS"))
    return SEQUEL_JOIN_CROSS;
  if (found && word_equals_ci(source->data + tokens[previous[0]].range.start,
                              tokens[previous[0]].range.end - tokens[previous[0]].range.start, "NATURAL"))
    return SEQUEL_JOIN_NATURAL;
  size_t modifier =
      found > 1 && word_equals_ci(source->data + tokens[previous[0]].range.start,
                                  tokens[previous[0]].range.end - tokens[previous[0]].range.start, "OUTER")
          ? 1
          : 0;
  if (found > modifier &&
      word_equals_ci(source->data + tokens[previous[modifier]].range.start,
                     tokens[previous[modifier]].range.end - tokens[previous[modifier]].range.start, "LEFT"))
    return SEQUEL_JOIN_LEFT;
  if (found > modifier &&
      word_equals_ci(source->data + tokens[previous[modifier]].range.start,
                     tokens[previous[modifier]].range.end - tokens[previous[modifier]].range.start, "RIGHT"))
    return SEQUEL_JOIN_RIGHT;
  if (found > modifier &&
      word_equals_ci(source->data + tokens[previous[modifier]].range.start,
                     tokens[previous[modifier]].range.end - tokens[previous[modifier]].range.start, "FULL"))
    return SEQUEL_JOIN_FULL;
  if (found && word_equals_ci(source->data + tokens[previous[0]].range.start,
                              tokens[previous[0]].range.end - tokens[previous[0]].range.start, "INNER"))
    return SEQUEL_JOIN_INNER;
  return SEQUEL_JOIN_INNER;
}

static uint32_t join_flags(const sequel_token_t *tokens, size_t token_count, size_t join_index,
                           const sequel_source_view_t *source) {
  uint32_t flags = 0;
  size_t begin = join_index > 6 ? join_index - 6 : 0;
  for (size_t i = begin; i < join_index; ++i) {
    if (tokens[i].kind != SEQUEL_TOKEN_KEYWORD)
      continue;
    const char *word = source->data + tokens[i].range.start;
    size_t length = tokens[i].range.end - tokens[i].range.start;
    if (word_equals_ci(word, length, "OUTER"))
      flags |= SEQUEL_JOIN_FLAG_OUTER;
    if (word_equals_ci(word, length, "NATURAL"))
      flags |= SEQUEL_JOIN_FLAG_NATURAL;
    if (word_equals_ci(word, length, "ASOF"))
      flags |= SEQUEL_JOIN_FLAG_ASOF;
    if (word_equals_ci(word, length, "GLOBAL"))
      flags |= SEQUEL_JOIN_FLAG_GLOBAL;
    if (word_equals_ci(word, length, "ANY"))
      flags |= SEQUEL_JOIN_FLAG_ANY;
    if (word_equals_ci(word, length, "SEMI"))
      flags |= SEQUEL_JOIN_FLAG_SEMI;
    if (word_equals_ci(word, length, "ANTI"))
      flags |= SEQUEL_JOIN_FLAG_ANTI;
  }
  size_t next = join_index + 1;
  while (next < token_count && is_trivia(&tokens[next]))
    ++next;
  if (next < token_count && tokens[next].kind == SEQUEL_TOKEN_KEYWORD &&
      word_equals_ci(source->data + tokens[next].range.start, tokens[next].range.end - tokens[next].range.start,
                     "LATERAL"))
    flags |= SEQUEL_JOIN_FLAG_LATERAL;
  return flags;
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
         token->keyword_id == SEQUEL_KEYWORD_QUALIFY || token->keyword_id == SEQUEL_KEYWORD_PREWHERE ||
         token->keyword_id == SEQUEL_KEYWORD_UNION;
}

static int is_reserved_relation_alias(const sequel_source_view_t *source, const sequel_token_t *token) {
  size_t length = token->range.end - token->range.start;
  if (length != 5 || token->range.end > source->length)
    return 0;
  return toupper((unsigned char)source->data[token->range.start]) == 'F' &&
         toupper((unsigned char)source->data[token->range.start + 1]) == 'I' &&
         toupper((unsigned char)source->data[token->range.start + 2]) == 'N' &&
         toupper((unsigned char)source->data[token->range.start + 3]) == 'A' &&
         toupper((unsigned char)source->data[token->range.start + 4]) == 'L';
}

static int is_join_modifier(const sequel_source_view_t *source, const sequel_token_t *token) {
  if (!is_identifier_token(token) || token->range.end > source->length)
    return 0;
  size_t length = token->range.end - token->range.start;
  const char *modifiers[] = {"NATURAL", "LEFT",  "RIGHT",   "FULL", "OUTER", "CROSS", "GLOBAL",
                             "ANY",     "INNER", "LATERAL", "ASOF", "SEMI",  "ANTI",  "POSITIONAL"};
  for (size_t i = 0; i < sizeof(modifiers) / sizeof(modifiers[0]); ++i)
    if (length == strlen(modifiers[i]) && !_strnicmp(source->data + token->range.start, modifiers[i], length))
      return 1;
  return 0;
}

static int token_in_subquery(const sequel_token_t *tokens, size_t index, const sequel_source_view_t *source) {
  size_t depth = 0;
  size_t subquery_depth = SIZE_MAX;
  for (size_t i = 0; i < index; ++i) {
    if (tokens[i].kind != SEQUEL_TOKEN_PUNCTUATION)
      continue;
    if (tokens[i].range.end - tokens[i].range.start != 1 || tokens[i].range.start >= source->length)
      continue;
    char punctuation = source->data[tokens[i].range.start];
    if (punctuation == '(') {
      ++depth;
      size_t next = i + 1;
      while (next < index &&
             (tokens[next].kind == SEQUEL_TOKEN_WHITESPACE || tokens[next].kind == SEQUEL_TOKEN_LINE_COMMENT ||
              tokens[next].kind == SEQUEL_TOKEN_BLOCK_COMMENT))
        ++next;
      if (next < index) {
        size_t length = tokens[next].range.end - tokens[next].range.start;
        bool select_keyword =
            tokens[next].kind == SEQUEL_TOKEN_KEYWORD && tokens[next].keyword_id == SEQUEL_KEYWORD_SELECT;
        bool select_spelling = length == 6 && !_strnicmp(source->data + tokens[next].range.start, "SELECT", length);
        if (select_keyword || select_spelling)
          subquery_depth = depth;
      }
    } else if (punctuation == ')' && depth) {
      if (subquery_depth == depth)
        subquery_depth = SIZE_MAX;
      --depth;
    }
  }
  return subquery_depth != SIZE_MAX && depth >= subquery_depth;
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
  const sequel_lexed_document_t *lexed = sequel_syntax_lexed(syntax);
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  sequel_source_view_t source = sequel_lexed_source(lexed);
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
    if (statement.operation == SEQUEL_OPERATION_UNKNOWN) {
      size_t first = nodes[i].first_token;
      while (first < token_count && (is_trivia(&tokens[first]) || (tokens[first].kind == SEQUEL_TOKEN_PUNCTUATION &&
                                                                   source.data[tokens[first].range.start] == '(')))
        ++first;
      if (first >= token_count)
        continue;
      const sequel_token_t *first_token = &tokens[first];
      document->statements[document->statement_count - 1].operation = operation_from_word(
          source.data + first_token->range.start, first_token->range.end - first_token->range.start);
      if (document->statements[document->statement_count - 1].operation == SEQUEL_OPERATION_UNKNOWN) {
        for (size_t candidate = first + 1; candidate < nodes[i].first_token + nodes[i].token_count; ++candidate) {
          if (is_trivia(&tokens[candidate]))
            continue;
          sequel_operation_t recovered = operation_from_word(
              source.data + tokens[candidate].range.start, tokens[candidate].range.end - tokens[candidate].range.start);
          if (recovered != SEQUEL_OPERATION_UNKNOWN) {
            document->statements[document->statement_count - 1].operation = recovered;
            break;
          }
        }
      }
    }
  }
  if (!document->statement_count && token_count) {
    for (size_t token = 0; token < token_count; ++token) {
      if (is_trivia(&tokens[token]))
        continue;
      sequel_operation_t recovered = operation_from_word(source.data + tokens[token].range.start,
                                                         tokens[token].range.end - tokens[token].range.start);
      if (recovered == SEQUEL_OPERATION_UNKNOWN)
        continue;
      sequel_statement_info_t statement = {
          .range = {0, source.length}, .operation = recovered, .syntax_node = SIZE_MAX, .query_body = SIZE_MAX};
      if (!append((void **)&document->statements, &document->statement_count, sizeof(statement), &statement)) {
        sequel_query_document_free(document);
        return SEQUEL_ERR_NOMEM;
      }
      break;
    }
  }
  for (size_t i = 0; i < token_count; ++i) {
    if (tokens[i].kind != SEQUEL_TOKEN_KEYWORD || tokens[i].keyword_id != SEQUEL_KEYWORD_WITH)
      continue;
    size_t cursor = i + 1;
    while (cursor < token_count && (is_trivia(&tokens[cursor])))
      ++cursor;
    if (cursor < token_count && tokens[cursor].range.end - tokens[cursor].range.start == 9 &&
        !memcmp(source.data + tokens[cursor].range.start, "RECURSIVE", 9))
      ++cursor;
    for (;;) {
      while (cursor < token_count && is_trivia(&tokens[cursor]))
        ++cursor;
      if (cursor >= token_count || !is_identifier_token(&tokens[cursor]))
        break;
      size_t name = cursor++;
      while (cursor < token_count && is_trivia(&tokens[cursor]))
        ++cursor;
      if (cursor < token_count && tokens[cursor].kind == SEQUEL_TOKEN_PUNCTUATION &&
          source.data[tokens[cursor].range.start] == '(') {
        unsigned depth = 0;
        do {
          if (tokens[cursor].kind == SEQUEL_TOKEN_PUNCTUATION) {
            char punctuation = source.data[tokens[cursor].range.start];
            if (punctuation == '(')
              ++depth;
            else if (punctuation == ')' && depth)
              --depth;
          }
          ++cursor;
        } while (cursor < token_count && depth);
        while (cursor < token_count && is_trivia(&tokens[cursor]))
          ++cursor;
      }
      if (cursor >= token_count || tokens[cursor].range.end - tokens[cursor].range.start != 2 ||
          memcmp(source.data + tokens[cursor].range.start, "AS", 2) != 0)
        break;
      ++cursor;
      while (cursor < token_count && is_trivia(&tokens[cursor]))
        ++cursor;
      if (cursor < token_count && tokens[cursor].kind == SEQUEL_TOKEN_IDENTIFIER) {
        size_t modifier = tokens[cursor].range.end - tokens[cursor].range.start;
        if ((modifier == 12 && !memcmp(source.data + tokens[cursor].range.start, "MATERIALIZED", 12)) ||
            (modifier == 3 && !memcmp(source.data + tokens[cursor].range.start, "NOT", 3))) {
          ++cursor;
          while (cursor < token_count && is_trivia(&tokens[cursor]))
            ++cursor;
          if (modifier == 3 && cursor < token_count && tokens[cursor].kind == SEQUEL_TOKEN_IDENTIFIER &&
              tokens[cursor].range.end - tokens[cursor].range.start == 12 &&
              !memcmp(source.data + tokens[cursor].range.start, "MATERIALIZED", 12))
            ++cursor;
          while (cursor < token_count && is_trivia(&tokens[cursor]))
            ++cursor;
        }
      }
      if (cursor >= token_count || tokens[cursor].kind != SEQUEL_TOKEN_PUNCTUATION ||
          source.data[tokens[cursor].range.start] != '(')
        break;
      size_t body_open = cursor++;
      unsigned depth = 1;
      while (cursor < token_count && depth) {
        if (tokens[cursor].kind == SEQUEL_TOKEN_PUNCTUATION) {
          char punctuation = source.data[tokens[cursor].range.start];
          if (punctuation == '(')
            ++depth;
          else if (punctuation == ')')
            --depth;
        }
        ++cursor;
      }
      size_t body_close = cursor ? cursor - 1 : body_open;
      sequel_cte_info_t cte = {.range = {tokens[name].range.start, tokens[body_close].range.end},
                               .name_range = tokens[name].range,
                               .body_range = {tokens[body_open].range.end, tokens[body_close].range.start},
                               .statement_index = statement_for_range(document, tokens[name].range),
                               .syntax_node = SIZE_MAX};
      if (!append((void **)&document->ctes, &document->cte_count, sizeof(cte), &cte)) {
        sequel_query_document_free(document);
        return SEQUEL_ERR_NOMEM;
      }
      while (cursor < token_count && is_trivia(&tokens[cursor]))
        ++cursor;
      if (cursor >= token_count || tokens[cursor].kind != SEQUEL_TOKEN_PUNCTUATION ||
          source.data[tokens[cursor].range.start] != ',')
        break;
      ++cursor;
    }
    break;
  }
  if (document->cte_count && document->statement_count) {
    size_t kept = 0;
    for (size_t i = 0; i < document->statement_count; ++i) {
      bool nested = false;
      for (size_t cte = 0; cte < document->cte_count; ++cte) {
        if (document->statements[i].range.start >= document->ctes[cte].body_range.start &&
            document->statements[i].range.end <= document->ctes[cte].body_range.end) {
          nested = true;
          break;
        }
      }
      if (!nested)
        document->statements[kept++] = document->statements[i];
    }
    document->statement_count = kept;
    size_t outer = document->ctes[document->cte_count - 1].range.end;
    if (document->statement_count && document->statements[0].range.start >= outer)
      outer = document->statements[0].range.start;
    document->outer_range = (sequel_source_range_t){outer, source.length};
    unsigned outer_depth = 0;
    for (size_t token = 0; token < token_count; ++token) {
      if (tokens[token].range.end <= outer || is_trivia(&tokens[token]))
        continue;
      char punctuation = source.data[tokens[token].range.start];
      if (punctuation == '(' || punctuation == ')') {
        if (punctuation == '(')
          ++outer_depth;
        else if (outer_depth)
          --outer_depth;
        continue;
      }
      if (!outer_depth && tokens[token].range.end - tokens[token].range.start == 5 &&
          word_equals_ci(source.data + tokens[token].range.start, 5, "LIMIT")) {
        document->outer_limit = true;
        break;
      }
    }
    document->outer_distinct_on = false;
    size_t cursor = outer;
    while (cursor < source.length && !isalpha((unsigned char)source.data[cursor]))
      ++cursor;
    if (cursor + 6 <= source.length && word_equals_ci(source.data + cursor, 6, "SELECT")) {
      cursor += 6;
      while (cursor < source.length && !isalpha((unsigned char)source.data[cursor]))
        ++cursor;
      if (cursor + 8 <= source.length && word_equals_ci(source.data + cursor, 8, "DISTINCT")) {
        cursor += 8;
        while (cursor < source.length && !isalpha((unsigned char)source.data[cursor]))
          ++cursor;
        document->outer_distinct_on = cursor + 2 <= source.length && word_equals_ci(source.data + cursor, 2, "ON");
      }
    }
    for (size_t token = 0; token < token_count; ++token) {
      if (tokens[token].range.start < outer || is_trivia(&tokens[token]))
        continue;
      sequel_operation_t outer_operation = operation_from_word(source.data + tokens[token].range.start,
                                                               tokens[token].range.end - tokens[token].range.start);
      if (outer_operation != SEQUEL_OPERATION_UNKNOWN) {
        document->statements[0].operation = outer_operation;
        break;
      }
    }
  }
  if (!document->cte_count && document->statement_count) {
    size_t outer = document->statements[0].range.start;
    document->outer_range = (sequel_source_range_t){outer, source.length};
    unsigned outer_depth = 0;
    for (size_t token = 0; token < token_count; ++token) {
      if (tokens[token].range.end <= outer || is_trivia(&tokens[token]))
        continue;
      char punctuation = source.data[tokens[token].range.start];
      if (punctuation == '(' || punctuation == ')') {
        if (punctuation == '(')
          ++outer_depth;
        else if (outer_depth)
          --outer_depth;
        continue;
      }
      if (!outer_depth && tokens[token].range.end - tokens[token].range.start == 5 &&
          word_equals_ci(source.data + tokens[token].range.start, 5, "LIMIT")) {
        document->outer_limit = true;
        break;
      }
    }
  }
  /* ClickHouse also permits scalar WITH bindings without a parenthesized CTE:
   * WITH expr AS name SELECT ... . The syntax node is intentionally retained
   * as WITH, but the normalized statement operation is the executable body. */
  if (document->statement_count && document->statements[0].operation == SEQUEL_OPERATION_UNKNOWN) {
    size_t depth = 0;
    bool after_with = false;
    for (size_t token = 0; token < token_count; ++token) {
      const sequel_token_t *current = &tokens[token];
      if (is_trivia(current))
        continue;
      if (!after_with) {
        if (current->kind == SEQUEL_TOKEN_KEYWORD && current->keyword_id == SEQUEL_KEYWORD_WITH)
          after_with = true;
        continue;
      }
      if (current->kind == SEQUEL_TOKEN_PUNCTUATION) {
        char punctuation = source.data[current->range.start];
        if (punctuation == '(')
          ++depth;
        else if (punctuation == ')' && depth)
          --depth;
        continue;
      }
      if (depth || current->kind != SEQUEL_TOKEN_KEYWORD)
        continue;
      sequel_operation_t body_operation =
          operation_from_word(source.data + current->range.start, current->range.end - current->range.start);
      if (body_operation != SEQUEL_OPERATION_UNKNOWN) {
        document->statements[0].operation = body_operation;
        break;
      }
    }
  }
  if (document->statement_count && document->statements[0].operation == SEQUEL_OPERATION_UNKNOWN) {
    for (size_t token = 0; token < token_count; ++token) {
      if (is_trivia(&tokens[token]))
        continue;
      sequel_operation_t recovered = operation_from_word(source.data + tokens[token].range.start,
                                                         tokens[token].range.end - tokens[token].range.start);
      if (recovered != SEQUEL_OPERATION_UNKNOWN) {
        document->statements[0].operation = recovered;
        break;
      }
    }
  }
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
    sequel_source_range_t type_range = {tokens[i].range.end, tokens[i].range.end};
    if (style == SEQUEL_BIND_TYPED) {
      size_t type_start = tokens[i].range.start;
      while (type_start + 1 < tokens[i].range.end && source.data[type_start] != ':' &&
             source.data[type_start + 1] != ':')
        ++type_start;
      if (type_start + 1 < tokens[i].range.end) {
        type_start += 2;
        type_range = (sequel_source_range_t){type_start, tokens[i].range.end};
      }
    }
    sequel_parameter_info_t parameter = {.range = tokens[i].range,
                                         .style = style,
                                         .statement_index = statement_for_range(document, tokens[i].range),
                                         .ordinal = ordinal,
                                         .explicit_number = explicit_number,
                                         .name_range = name_range,
                                         .type_range = type_range,
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
    size_t range_first = 0;
    while (range_first < token_count && tokens[range_first].range.end <= statement->range.start)
      ++range_first;
    size_t range_end = range_first;
    while (range_end < token_count && tokens[range_end].range.start < statement->range.end)
      ++range_end;
    if (range_first < range_end) {
      first = range_first;
      end = range_end;
    }
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
      if (!boundary && tokens[i].range.end == tokens[i].range.start + 1 && tokens[i].range.start < source.length) {
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
        projection.expression_node = expression_node_for_range(document->syntax, projection.range);
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
        predicate.expression_node = expression_node_for_range(document->syntax, predicate.range);
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
    bool expression_from = false;
    if (tokens[i].keyword_id == SEQUEL_KEYWORD_FROM) {
      size_t prior[3] = {SIZE_MAX, SIZE_MAX, SIZE_MAX};
      size_t found = 0;
      for (size_t cursor = i; cursor-- > 0 && found < 3;) {
        if (!is_trivia(&tokens[cursor]))
          prior[found++] = cursor;
      }
      if (found == 3 && tokens[prior[1]].kind == SEQUEL_TOKEN_KEYWORD &&
          tokens[prior[1]].keyword_id == SEQUEL_KEYWORD_NOT && tokens[prior[2]].kind == SEQUEL_TOKEN_KEYWORD &&
          tokens[prior[2]].keyword_id == SEQUEL_KEYWORD_IS &&
          word_equals_ci(source.data + tokens[prior[0]].range.start,
                         tokens[prior[0]].range.end - tokens[prior[0]].range.start, "DISTINCT"))
        expression_from = true;
    }
    if (expression_from)
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
    if (name >= token_count)
      continue;
    if (tokens[name].kind == SEQUEL_TOKEN_PUNCTUATION && source.data[tokens[name].range.start] == '(') {
      size_t close = name;
      unsigned depth = 0;
      for (; close < token_count; ++close) {
        if (tokens[close].kind != SEQUEL_TOKEN_PUNCTUATION)
          continue;
        char punctuation = source.data[tokens[close].range.start];
        if (punctuation == '(')
          ++depth;
        else if (punctuation == ')' && depth && --depth == 0)
          break;
      }
      if (close >= token_count)
        continue;
      size_t alias = close + 1;
      while (alias < token_count && is_trivia(&tokens[alias]))
        ++alias;
      if (alias < token_count && tokens[alias].kind == SEQUEL_TOKEN_KEYWORD &&
          tokens[alias].keyword_id == SEQUEL_KEYWORD_AS) {
        ++alias;
        while (alias < token_count && is_trivia(&tokens[alias]))
          ++alias;
      }
      sequel_source_range_t alias_range = {tokens[close].range.end, tokens[close].range.end};
      if (alias < token_count && is_identifier_token(&tokens[alias]))
        alias_range = tokens[alias].range;
      sequel_relation_info_t relation = {.kind = SEQUEL_RELATION_SUBQUERY,
                                         .range = {tokens[i].range.start, alias_range.end > tokens[close].range.end
                                                                              ? alias_range.end
                                                                              : tokens[close].range.end},
                                         .name_range = {tokens[name].range.start, tokens[close].range.end},
                                         .alias_range = alias_range,
                                         .statement_index = statement,
                                         .syntax_node = SIZE_MAX,
                                         .parent_relation = SIZE_MAX,
                                         .subquery_statement = SIZE_MAX,
                                         .flags = SEQUEL_RELATION_FLAG_NESTED_SCOPE};
      if (!append((void **)&document->relations, &document->relation_count, sizeof(relation), &relation)) {
        sequel_query_document_free(document);
        return SEQUEL_ERR_NOMEM;
      }
      continue;
    }
    if (!is_identifier_token(&tokens[name]))
      continue;
    size_t end = tokens[name].range.end;
    size_t cursor = name + 1;
    bool function_relation = false;
    bool lateral_subquery = word_equals_ci(source.data + tokens[name].range.start,
                                           tokens[name].range.end - tokens[name].range.start, "LATERAL");
    if (lateral_subquery) {
      while (cursor < token_count && is_trivia(&tokens[cursor]))
        ++cursor;
      if (cursor < token_count && tokens[cursor].kind == SEQUEL_TOKEN_PUNCTUATION &&
          source.data[tokens[cursor].range.start] == '(') {
        size_t close = cursor;
        unsigned depth = 0;
        for (; close < token_count; ++close) {
          if (tokens[close].kind != SEQUEL_TOKEN_PUNCTUATION)
            continue;
          char punctuation = source.data[tokens[close].range.start];
          if (punctuation == '(')
            ++depth;
          else if (punctuation == ')' && depth && --depth == 0)
            break;
        }
        if (close < token_count) {
          size_t body_start = tokens[cursor].range.end;
          size_t body_end = tokens[close].range.start;
          lateral_subquery = sequel_text_find(source.data + body_start, body_end - body_start, " OR ") != NULL;
          if (lateral_subquery) {
            end = tokens[close].range.end;
            cursor = close + 1;
          }
        }
      }
    }
    if (tokens[i].keyword_id == SEQUEL_KEYWORD_JOIN &&
        !word_equals_ci(source.data + tokens[name].range.start, tokens[name].range.end - tokens[name].range.start,
                        "LATERAL")) {
      size_t opening = cursor;
      while (opening < token_count && is_trivia(&tokens[opening]))
        ++opening;
      if (opening < token_count && tokens[opening].kind == SEQUEL_TOKEN_PUNCTUATION &&
          source.data[tokens[opening].range.start] == '(') {
        size_t close = opening;
        unsigned depth = 0;
        for (; close < token_count; ++close) {
          if (tokens[close].kind != SEQUEL_TOKEN_PUNCTUATION)
            continue;
          char punctuation = source.data[tokens[close].range.start];
          if (punctuation == '(')
            ++depth;
          else if (punctuation == ')' && depth && --depth == 0)
            break;
        }
        if (close < token_count) {
          function_relation = true;
          end = tokens[close].range.end;
          cursor = close + 1;
        }
      }
    }
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
    if (alias < token_count && tokens[alias].kind == SEQUEL_TOKEN_KEYWORD &&
        tokens[alias].keyword_id == SEQUEL_KEYWORD_AS) {
      ++alias;
      while (alias < token_count &&
             (tokens[alias].kind == SEQUEL_TOKEN_WHITESPACE || tokens[alias].kind == SEQUEL_TOKEN_LINE_COMMENT ||
              tokens[alias].kind == SEQUEL_TOKEN_BLOCK_COMMENT))
        ++alias;
    }
    sequel_source_range_t alias_range = {end, end};
    if (alias < token_count && is_identifier_token(&tokens[alias]) && !is_clause_keyword(&tokens[alias]) &&
        !is_join_modifier(&source, &tokens[alias]) && !is_reserved_relation_alias(&source, &tokens[alias]))
      alias_range = tokens[alias].range;
    sequel_relation_info_t relation = {.kind = SEQUEL_RELATION_TABLE,
                                       .range = {tokens[i].range.start, alias_range.end > end ? alias_range.end : end},
                                       .name_range = {tokens[name].range.start, end},
                                       .alias_range = alias_range,
                                       .statement_index = statement,
                                       .syntax_node = SIZE_MAX,
                                       .parent_relation = SIZE_MAX,
                                       .subquery_statement = SIZE_MAX};
    if (token_in_subquery(tokens, i, &source)) {
      relation.flags |= SEQUEL_RELATION_FLAG_NESTED_SCOPE;
      relation.kind = SEQUEL_RELATION_SUBQUERY;
    }
    if (lateral_subquery) {
      relation.flags |= SEQUEL_RELATION_FLAG_NESTED_SCOPE;
      relation.kind = SEQUEL_RELATION_SUBQUERY;
      relation.name_range.end = tokens[name].range.end;
    }
    if (function_relation)
      relation.kind = SEQUEL_RELATION_FUNCTION;
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
    size_t statement = statement_for_range(document, tokens[i].range);
    sequel_join_info_t join = {.kind = join_kind(tokens, token_count, i, &source),
                               .flags = join_flags(tokens, token_count, i, &source),
                               .range = {tokens[i].range.start, document->relations[right].range.end},
                               .condition_range = {0, 0},
                               .left_relation = left,
                               .right_relation = right,
                               .condition_node = SIZE_MAX,
                               .has_condition = false};
    size_t join_end = statement != SIZE_MAX ? document->statements[statement].range.end : token_count;
    for (size_t j = i + 1; j < token_count && tokens[j].range.start < join_end; ++j) {
      if (tokens[j].kind == SEQUEL_TOKEN_KEYWORD && tokens[j].keyword_id == SEQUEL_KEYWORD_USING)
        join.flags |= SEQUEL_JOIN_FLAG_USING;
      if (tokens[j].kind == SEQUEL_TOKEN_KEYWORD && tokens[j].keyword_id == SEQUEL_KEYWORD_ON) {
        join.has_condition = true;
        size_t condition_start = j + 1;
        while (condition_start < token_count && is_trivia(&tokens[condition_start]))
          ++condition_start;
        size_t condition_end = condition_start;
        while (condition_end < token_count && tokens[condition_end].range.start < join_end) {
          if (condition_end > condition_start && tokens[condition_end].kind == SEQUEL_TOKEN_KEYWORD &&
              is_clause_keyword(&tokens[condition_end]))
            break;
          ++condition_end;
        }
        while (condition_end > condition_start && is_trivia(&tokens[condition_end - 1]))
          --condition_end;
        if (condition_start < condition_end) {
          join.condition_range =
              (sequel_source_range_t){tokens[condition_start].range.start, tokens[condition_end - 1].range.end};
          join.condition_node = expression_node_for_range(document->syntax, join.condition_range);
        }
        break;
      }
    }
    if (!append((void **)&document->joins, &document->join_count, sizeof(join), &join)) {
      sequel_query_document_free(document);
      return SEQUEL_ERR_NOMEM;
    }
  }
  /* Relations discovered inside scalar SELECT-list expressions belong to
   * the expression scope, not the statement's FROM clause. Record that
   * ownership explicitly so consumers never have to infer it from text. */
  for (size_t r = 0; r < document->relation_count; ++r) {
    for (size_t p = 0; p < document->projection_count; ++p) {
      if (sequel_source_range_contains(document->projections[p].range, document->relations[r].range)) {
        document->relations[r].flags |= SEQUEL_RELATION_FLAG_PROJECTION_SCOPE;
        break;
      }
    }
  }
  for (size_t i = 0; i < token_count; ++i) {
    if (tokens[i].kind != SEQUEL_TOKEN_KEYWORD || !is_clause_keyword(&tokens[i]))
      continue;
    size_t statement_index = SIZE_MAX;
    for (size_t s = 0; s < document->statement_count; ++s)
      if (sequel_source_range_contains(document->statements[s].range, tokens[i].range)) {
        statement_index = s;
        break;
      }
    if (statement_index == SIZE_MAX)
      continue;
    size_t depth = 0;
    size_t statement_start = document->statements[statement_index].range.start;
    for (size_t previous = 0; previous < i; ++previous) {
      if (tokens[previous].range.start < statement_start || tokens[previous].range.start >= tokens[i].range.start ||
          tokens[previous].range.end != tokens[previous].range.start + 1)
        continue;
      char punctuation = source.data[tokens[previous].range.start];
      if (punctuation == '(')
        ++depth;
      else if (punctuation == ')' && depth)
        --depth;
    }
    if (depth != 0)
      continue;
    size_t end = document->statements[statement_index].range.end;
    for (size_t j = i + 1; j < token_count; ++j) {
      if (tokens[j].range.start >= end)
        break;
      if (tokens[j].kind == SEQUEL_TOKEN_KEYWORD && is_clause_keyword(&tokens[j])) {
        size_t nested = 0;
        for (size_t previous = i + 1; previous < j; ++previous) {
          if (tokens[previous].range.end != tokens[previous].range.start + 1)
            continue;
          char punctuation = source.data[tokens[previous].range.start];
          if (punctuation == '(')
            ++nested;
          else if (punctuation == ')' && nested)
            --nested;
        }
        if (nested != 0)
          continue;
        end = tokens[j].range.start;
        break;
      }
    }
    sequel_clause_info_t clause = {{tokens[i].range.start, end}, statement_index, tokens[i].keyword_id};
    if (!append((void **)&document->clauses, &document->clause_count, sizeof(clause), &clause)) {
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
  free(document->ctes);
  free(document->parameters);
  free(document->joins);
  free(document->projections);
  free(document->predicates);
  free(document->clauses);
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

size_t sequel_query_cte_count(const sequel_query_document_t *document) { return document ? document->cte_count : 0; }

const sequel_cte_info_t *sequel_query_ctes(const sequel_query_document_t *document) {
  return document ? document->ctes : NULL;
}

bool sequel_query_outer_distinct_on(const sequel_query_document_t *document) {
  return document ? document->outer_distinct_on : false;
}

bool sequel_query_outer_limit(const sequel_query_document_t *document) {
  return document ? document->outer_limit : false;
}

sequel_source_range_t sequel_query_outer_range(const sequel_query_document_t *document) {
  return document ? document->outer_range : (sequel_source_range_t){0, 0};
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

size_t sequel_query_clause_count(const sequel_query_document_t *document) {
  return document ? document->clause_count : 0;
}

const sequel_clause_info_t *sequel_query_clauses(const sequel_query_document_t *document) {
  return document ? document->clauses : NULL;
}

sequel_source_view_t sequel_query_source(const sequel_query_document_t *document) {
  return document ? sequel_lexed_source(sequel_syntax_lexed(document->syntax)) : (sequel_source_view_t){0};
}

sequel_dialect_t sequel_query_dialect(const sequel_query_document_t *document) {
  return document ? sequel_lexed_dialect(sequel_syntax_lexed(document->syntax)) : SEQUEL_GENERIC;
}

const sequel_syntax_document_t *sequel_query_syntax(const sequel_query_document_t *document) {
  return document ? document->syntax : NULL;
}

sequel_status_t sequel_query_visit(const sequel_query_document_t *document, const sequel_query_visitor_t *visitor) {
  if (!document || !visitor)
    return SEQUEL_ERR_NULL;
  for (size_t i = 0; i < document->statement_count; ++i)
    if (visitor->statement && !visitor->statement(document, i, visitor->userdata))
      return SEQUEL_ERR_CANCELLED;
  for (size_t i = 0; i < document->clause_count; ++i)
    if (visitor->clause && !visitor->clause(document, i, visitor->userdata))
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
