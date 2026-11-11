#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <sequel/query.h>
#include "util/text.h"
#include "model.h"

static int is_trivia(const sequel_token_t *token);

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
  size_t max_nodes = options && options->max_nodes ? options->max_nodes : (size_t)-1;
  const sequel_lexed_document_t *lexed = sequel_syntax_lexed(syntax);
  size_t node_count = 0;
  const sequel_syntax_node_t *nodes = sequel_syntax_nodes(syntax, &node_count);
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  sequel_source_view_t source = sequel_lexed_source(lexed);
  sequel_status_t statement_status =
      sequel_query_build_statements(document, syntax, tokens, token_count, &source, max_nodes);
  if (statement_status != SEQUEL_OK) {
    sequel_query_document_free(document);
    return statement_status;
  }
  sequel_status_t cte_status = sequel_query_build_ctes(document, tokens, token_count, &source);
  if (cte_status != SEQUEL_OK) {
    sequel_query_document_free(document);
    return cte_status;
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
          sequel_query_word_equals_ci(source.data + tokens[token].range.start, 5, "LIMIT")) {
        document->outer_limit = true;
        break;
      }
    }
    document->outer_distinct_on = false;
    size_t cursor = outer;
    while (cursor < source.length && !isalpha((unsigned char)source.data[cursor]))
      ++cursor;
    if (cursor + 6 <= source.length && sequel_query_word_equals_ci(source.data + cursor, 6, "SELECT")) {
      cursor += 6;
      while (cursor < source.length && !isalpha((unsigned char)source.data[cursor]))
        ++cursor;
      if (cursor + 8 <= source.length && sequel_query_word_equals_ci(source.data + cursor, 8, "DISTINCT")) {
        cursor += 8;
        while (cursor < source.length && !isalpha((unsigned char)source.data[cursor]))
          ++cursor;
        document->outer_distinct_on =
            cursor + 2 <= source.length && sequel_query_word_equals_ci(source.data + cursor, 2, "ON");
      }
    }
    for (size_t token = 0; token < token_count; ++token) {
      if (tokens[token].range.start < outer || is_trivia(&tokens[token]))
        continue;
      sequel_operation_t outer_operation = sequel_query_operation_from_word(
          source.data + tokens[token].range.start, tokens[token].range.end - tokens[token].range.start);
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
          sequel_query_word_equals_ci(source.data + tokens[token].range.start, 5, "LIMIT")) {
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
      sequel_operation_t body_operation = sequel_query_operation_from_word(source.data + current->range.start,
                                                                           current->range.end - current->range.start);
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
      sequel_operation_t recovered = sequel_query_operation_from_word(
          source.data + tokens[token].range.start, tokens[token].range.end - tokens[token].range.start);
      if (recovered != SEQUEL_OPERATION_UNKNOWN) {
        document->statements[0].operation = recovered;
        break;
      }
    }
  }
  /* Parameter identity and type ranges are normalized by the binding layer. */
  sequel_status_t parameter_status = sequel_query_build_parameters(document, tokens, token_count, &source);
  if (parameter_status != SEQUEL_OK) {
    sequel_query_document_free(document);
    return parameter_status;
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
        projection.expression_node = sequel_query_expression_node_for_range(document->syntax, projection.range);
        if (!sequel_query_array_append((void **)&document->projections, &document->projection_count, sizeof(projection),
                                       &projection)) {
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
        predicate.expression_node = sequel_query_expression_node_for_range(document->syntax, predicate.range);
        if (!sequel_query_array_append((void **)&document->predicates, &document->predicate_count, sizeof(predicate),
                                       &predicate)) {
          sequel_query_document_free(document);
          return SEQUEL_ERR_NOMEM;
        }
      }
      break;
    }
  }
  for (size_t i = 0; i < token_count; ++i) {
    bool comma_relation = false;
    if (tokens[i].kind == SEQUEL_TOKEN_PUNCTUATION && source.data[tokens[i].range.start] == ',') {
      for (size_t previous = i; previous > 0;) {
        --previous;
        if (is_trivia(&tokens[previous]))
          continue;
        if (tokens[previous].kind == SEQUEL_TOKEN_KEYWORD && (tokens[previous].keyword_id == SEQUEL_KEYWORD_FROM ||
                                                              tokens[previous].keyword_id == SEQUEL_KEYWORD_JOIN)) {
          comma_relation = true;
          break;
        }
        if (tokens[previous].kind == SEQUEL_TOKEN_KEYWORD && is_clause_keyword(&tokens[previous]))
          break;
      }
    }
    if (!comma_relation && (tokens[i].kind != SEQUEL_TOKEN_KEYWORD ||
                            (tokens[i].keyword_id != SEQUEL_KEYWORD_FROM &&
                             tokens[i].keyword_id != SEQUEL_KEYWORD_JOIN && tokens[i].keyword_id != SEQUEL_KEYWORD_ON)))
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
          sequel_query_word_equals_ci(source.data + tokens[prior[0]].range.start,
                                      tokens[prior[0]].range.end - tokens[prior[0]].range.start, "DISTINCT"))
        expression_from = true;
    }
    if (expression_from)
      continue;
    size_t statement = sequel_query_statement_for_range(document, tokens[i].range);
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
                                         .range = {tokens[name].range.start, alias_range.end > tokens[close].range.end
                                                                                 ? alias_range.end
                                                                                 : tokens[close].range.end},
                                         .name_range = {tokens[name].range.start, tokens[close].range.end},
                                         .alias_range = alias_range,
                                         .statement_index = statement,
                                         .syntax_node = SIZE_MAX,
                                         .parent_relation = SIZE_MAX,
                                         .subquery_statement = SIZE_MAX,
                                         .flags = SEQUEL_RELATION_FLAG_NESTED_SCOPE};
      if (!sequel_query_array_append((void **)&document->relations, &document->relation_count, sizeof(relation),
                                     &relation)) {
        sequel_query_document_free(document);
        return SEQUEL_ERR_NOMEM;
      }
      continue;
    }
    bool lateral_name = sequel_query_word_equals_ci(source.data + tokens[name].range.start,
                                                    tokens[name].range.end - tokens[name].range.start, "LATERAL");
    if (!is_identifier_token(&tokens[name]) && !lateral_name)
      continue;
    size_t end = tokens[name].range.end;
    size_t cursor = name + 1;
    bool function_relation = false;
    bool lateral_subquery = sequel_query_word_equals_ci(source.data + tokens[name].range.start,
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
          /* Parenthesized LATERAL relations are subquery relations regardless
           * of the predicates present in their body.  Using a predicate as a
           * proxy misclassified aggregate-only lateral queries as tables and
           * left their closing tokens to the summary renderer. */
          lateral_subquery = true;
          if (lateral_subquery) {
            end = tokens[close].range.end;
            cursor = close + 1;
          }
        }
      }
    }
    if (!lateral_subquery) {
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
    if (tokens[i].keyword_id == SEQUEL_KEYWORD_JOIN &&
        !sequel_query_word_equals_ci(source.data + tokens[name].range.start,
                                     tokens[name].range.end - tokens[name].range.start, "LATERAL")) {
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
    statement = sequel_query_statement_for_range(document, range);
    size_t alias = cursor;
    while (alias < token_count &&
           (tokens[alias].kind == SEQUEL_TOKEN_WHITESPACE || tokens[alias].kind == SEQUEL_TOKEN_LINE_COMMENT ||
            tokens[alias].kind == SEQUEL_TOKEN_BLOCK_COMMENT))
      ++alias;
    if (alias < token_count &&
        sequel_query_word_equals_ci(source.data + tokens[alias].range.start,
                                    tokens[alias].range.end - tokens[alias].range.start, "WITH")) {
      ++alias;
      while (alias < token_count && is_trivia(&tokens[alias]))
        ++alias;
      if (alias < token_count &&
          sequel_query_word_equals_ci(source.data + tokens[alias].range.start,
                                      tokens[alias].range.end - tokens[alias].range.start, "ORDINALITY"))
        ++alias;
      while (alias < token_count && is_trivia(&tokens[alias]))
        ++alias;
    }
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
    sequel_relation_info_t relation = {
        .kind = SEQUEL_RELATION_TABLE,
        .range = {tokens[name].range.start, alias_range.end > end ? alias_range.end : end},
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
    if (function_relation)
      relation.name_range.end = tokens[name].range.end;
    if (!sequel_query_array_append((void **)&document->relations, &document->relation_count, sizeof(relation),
                                   &relation)) {
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
    size_t statement = sequel_query_statement_for_range(document, tokens[i].range);
    sequel_join_info_t join = {.kind = sequel_query_join_kind(tokens, token_count, i, &source),
                               .flags = sequel_query_join_flags(tokens, token_count, i, &source),
                               .range = {tokens[i].range.start, document->relations[right].range.end},
                               .condition_range = {0, 0},
                               .left_relation = left,
                               .right_relation = right,
                               .condition_node = SIZE_MAX,
                               .has_condition = false};
    size_t join_end = statement != SIZE_MAX ? document->statements[statement].range.end : token_count;
    for (size_t j = i + 1; j < token_count && tokens[j].range.start < join_end; ++j) {
      if (tokens[j].kind == SEQUEL_TOKEN_KEYWORD && tokens[j].keyword_id == SEQUEL_KEYWORD_JOIN)
        break;
      if (tokens[j].kind == SEQUEL_TOKEN_KEYWORD && tokens[j].keyword_id == SEQUEL_KEYWORD_USING)
        join.flags |= SEQUEL_JOIN_FLAG_USING;
      if (tokens[j].kind == SEQUEL_TOKEN_KEYWORD && tokens[j].keyword_id == SEQUEL_KEYWORD_ON) {
        join.has_condition = true;
        size_t condition_start = j + 1;
        while (condition_start < token_count && is_trivia(&tokens[condition_start]))
          ++condition_start;
        size_t condition_end = condition_start;
        size_t nesting = 0;
        while (condition_end < token_count && tokens[condition_end].range.start < join_end) {
          if (tokens[condition_end].kind == SEQUEL_TOKEN_PUNCTUATION &&
              tokens[condition_end].range.end == tokens[condition_end].range.start + 1) {
            char punctuation = source.data[tokens[condition_end].range.start];
            if (punctuation == '(')
              ++nesting;
            else if (punctuation == ')' && nesting)
              --nesting;
          }
          /* A JOIN starts a new relation and therefore owns any later ON.
           * Do not let the preceding join inherit that condition.  Nested
           * subqueries are protected by the nesting counter above. */
          if (nesting == 0 && condition_end > condition_start && tokens[condition_end].kind == SEQUEL_TOKEN_KEYWORD &&
              (tokens[condition_end].keyword_id == SEQUEL_KEYWORD_JOIN || is_clause_keyword(&tokens[condition_end])))
            break;
          ++condition_end;
        }
        while (condition_end > condition_start && is_trivia(&tokens[condition_end - 1]))
          --condition_end;
        if (condition_start < condition_end) {
          join.condition_range =
              (sequel_source_range_t){tokens[condition_start].range.start, tokens[condition_end - 1].range.end};
          join.condition_node = sequel_query_expression_node_for_range(document->syntax, join.condition_range);
        }
        break;
      }
    }
    if (!sequel_query_array_append((void **)&document->joins, &document->join_count, sizeof(join), &join)) {
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
    if (!sequel_query_array_append((void **)&document->clauses, &document->clause_count, sizeof(clause), &clause)) {
      sequel_query_document_free(document);
      return SEQUEL_ERR_NOMEM;
    }
  }
  *out = document;
  return SEQUEL_OK;
}
