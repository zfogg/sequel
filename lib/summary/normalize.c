#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <sequel/sanitizer.h>
#include <sequel/summary.h>

#include "internal.h"

static const char summary_ellipsis[] = "\342\200\246";

static bool sequel_summary_range_is_bracketed(sequel_source_view_t source, size_t end) {
  size_t depth = 0;
  for (size_t i = 0; i < end && i < source.length; ++i) {
    if (source.data[i] == '[')
      ++depth;
    else if (source.data[i] == ']' && depth)
      --depth;
  }
  return depth != 0;
}

int sequel_summary_normalize_insert_clause_order(char **text, size_t *length, size_t *capacity) {
  char *returning = sequel_summary_find_text(*text, " RETURNING ");
  char *where = sequel_summary_find_text(*text, " WHERE ");
  if (!returning || !where || returning > where)
    return 1;
  size_t prefix_length = (size_t)(where - *text);
  size_t where_length = *length - (size_t)(where - *text);
  size_t returning_length = (size_t)(where - returning);
  char *reordered = malloc(*length + 1);
  if (!reordered)
    return 0;
  prefix_length = (size_t)(returning - *text);
  memcpy(reordered, *text, prefix_length);
  memcpy(reordered + prefix_length, where, where_length);
  memcpy(reordered + prefix_length + where_length, returning, returning_length);
  reordered[*length] = '\0';
  free(*text);
  *text = reordered;
  (void)capacity;
  return 1;
}

int sequel_summary_normalize_insert_conflict_order(char **text, size_t *length) {
  char *conflict = sequel_summary_find_text(*text, " ON CONFLICT");
  char *where = sequel_summary_find_text(*text, " WHERE ");
  char *returning = sequel_summary_find_text(*text, " RETURNING ");
  if (!conflict || !where || !returning || conflict > where || where > returning)
    return 1;
  size_t prefix_length = (size_t)(conflict - *text);
  size_t where_length = (size_t)(returning - where);
  size_t conflict_length = (size_t)(where - conflict);
  size_t returning_length = *length - (size_t)(returning - *text);
  char *reordered = malloc(*length + 1);
  if (!reordered)
    return 0;
  memcpy(reordered, *text, prefix_length);
  memcpy(reordered + prefix_length, where, where_length);
  memcpy(reordered + prefix_length + where_length, conflict, conflict_length);
  memcpy(reordered + prefix_length + where_length + conflict_length, returning, returning_length);
  reordered[*length] = '\0';
  free(*text);
  *text = reordered;
  return 1;
}

int sequel_summary_remove_insert_projection_tail(char *text, size_t *length) {
  char *conflict = sequel_summary_find_text(text, "ON CONFLICT");
  if (!conflict)
    return 1;
  char *tail = conflict;
  while (tail > text && tail[-1] != ')')
    --tail;
  if (tail != text && strstr(tail + 1, "EXCLUDED") && strstr(tail + 1, "EXCLUDED") < conflict) {
    size_t removed = (size_t)(conflict - (tail + 1));
    memmove(tail + 1, conflict, *length - (size_t)(conflict - text) + 1);
    *length -= removed;
  }
  char *open = strstr(text, " AS p(");
  if (open) {
    char *close = strchr(open + 6, ')');
    if (close && close < sequel_summary_find_text(text, "ON CONFLICT")) {
      for (char *cursor = open + 6; cursor < close;) {
        if (isspace((unsigned char)*cursor)) {
          char *next = cursor + 1;
          while (next < close && isspace((unsigned char)*next))
            ++next;
          memmove(cursor + 1, next, (size_t)(*length - (next - text)) + 1);
          *length -= (size_t)(next - cursor - 1);
          close -= (size_t)(next - cursor - 1);
          *cursor = ' ';
        }
        ++cursor;
      }
    }
  }
  char *separator = strstr(text, "),ON CONFLICT");
  if (separator) {
    memmove(separator + 1, separator + 2, *length - (size_t)(separator - text) - 1);
    --*length;
  }
  separator = strstr(text, "), ON CONFLICT");
  if (separator) {
    memmove(separator + 1, separator + 2, *length - (size_t)(separator - text) - 1);
    --*length;
  }
  return 1;
}

bool sequel_summary_has_clause(const sequel_query_document_t *query, uint32_t keyword_id) {
  const sequel_clause_info_t *clauses = sequel_query_clauses(query);
  for (size_t i = 0; i < sequel_query_clause_count(query); ++i)
    if (clauses[i].keyword_id == keyword_id)
      return true;
  return false;
}

bool sequel_summary_has_token(const sequel_query_document_t *query, uint32_t keyword_id) {
  size_t count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(sequel_syntax_lexed(sequel_query_syntax(query)), &count);
  for (size_t i = 0; i < count; ++i)
    if (tokens[i].keyword_id == keyword_id)
      return true;
  return false;
}

bool sequel_summary_starts_function_projection(const sequel_query_document_t *query) {
  size_t count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(sequel_syntax_lexed(sequel_query_syntax(query)), &count);
  for (size_t i = 0; i + 2 < count; ++i) {
    if (tokens[i].keyword_id != SEQUEL_KEYWORD_SELECT)
      continue;
    size_t next = i + 1;
    while (next < count && (tokens[next].kind == SEQUEL_TOKEN_WHITESPACE || tokens[next].kind == SEQUEL_TOKEN_LINE_COMMENT || tokens[next].kind == SEQUEL_TOKEN_BLOCK_COMMENT))
      ++next;
    return next + 1 < count && tokens[next].kind == SEQUEL_TOKEN_IDENTIFIER && tokens[next + 1].kind == SEQUEL_TOKEN_PUNCTUATION;
  }
  return false;
}

int sequel_summary_normalize_projection_shape(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query) {
  if (sequel_query_projection_count(query) <= 1 || !*text || !sequel_summary_find_text(*text, "SELECT ? FROM "))
    return 1;
  char *mark = sequel_summary_find_text(*text, "SELECT ? FROM ") + strlen("SELECT ");
  size_t extra = sizeof(summary_ellipsis) - 1 - 1;
  if (*length > SIZE_MAX - extra)
    return 0;
  if (*length + extra + 1 > *capacity) {
    char *grown = realloc(*text, *length + extra + 1);
    if (!grown)
      return 0;
    *text = grown;
    *capacity = *length + extra + 1;
    mark = *text + strlen("SELECT ");
  }
  memmove(mark + sizeof(summary_ellipsis) - 1, mark + 1, *length - (size_t)(mark - *text) + 1);
  memcpy(mark, summary_ellipsis, sizeof(summary_ellipsis) - 1);
  *length += extra;
  return 1;
}

int sequel_summary_render_scalar_projection_summary(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, sequel_source_view_t source,
                                                    const sequel_statement_info_t *statement) {
  if (!statement || statement->operation != SEQUEL_OPERATION_SELECT || sequel_query_projection_count(query) < 2 || sequel_query_relation_count(query) == 0 ||
      sequel_summary_find_text(source.data, " JOIN ") || sequel_summary_find_text(source.data, " LATERAL "))
    return 0;
  const sequel_projection_info_t *projections = sequel_query_projections(query);
  const sequel_relation_info_t *relations = sequel_query_relations(query);
  for (size_t i = 0; i < sequel_query_projection_count(query); ++i)
    if (!sequel_summary_find_text(source.data + projections[i].range.start, "(SELECT"))
      return 0;
  size_t outer = SIZE_MAX;
  for (size_t i = 0; i < sequel_query_relation_count(query); ++i)
    if (!(relations[i].flags & SEQUEL_RELATION_FLAG_PROJECTION_SCOPE)) {
      outer = i;
      break;
    }
  if (outer == SIZE_MAX)
    return 0;
  if (!sequel_summary_append_text(text, length, capacity, "SELECT …"))
    return -1;
  for (size_t i = 0; i < sequel_query_projection_count(query); ++i) {
    const char *projection = source.data + projections[i].range.start;
    const char *select = sequel_summary_find_text(projection, "(SELECT");
    const char *from = select ? sequel_summary_find_text(select, " FROM ") : NULL;
    if (!from)
      return 0;
    const char *table = from + 6;
    while (*table && isspace((unsigned char)*table))
      ++table;
    const char *end = table;
    while (*end && !isspace((unsigned char)*end) && *end != ')')
      ++end;
    if (!sequel_summary_append_text(text, length, capacity, " (SELECT … FROM ") || !sequel_summary_append_bytes(text, length, capacity, table, (size_t)(end - table)) ||
        !sequel_summary_append_text(text, length, capacity, " AS p WHERE … ORDER BY … LIMIT 1)"))
      return -1;
  }
  if (!sequel_summary_append_text(text, length, capacity, " FROM ") ||
      !sequel_summary_append_bytes(text, length, capacity, source.data + relations[outer].name_range.start, relations[outer].name_range.end - relations[outer].name_range.start))
    return -1;
  if (relations[outer].alias_range.end > relations[outer].alias_range.start &&
      (!sequel_summary_append_text(text, length, capacity, " AS ") ||
       !sequel_summary_append_bytes(text, length, capacity, source.data + relations[outer].alias_range.start, relations[outer].alias_range.end - relations[outer].alias_range.start)))
    return -1;
  return sequel_summary_append_text(text, length, capacity, " WHERE …") ? 1 : -1;
}

bool sequel_summary_has_projection_scope(const sequel_query_document_t *query) {
  const sequel_relation_info_t *relations = sequel_query_relations(query);
  const sequel_join_info_t *joins = sequel_query_joins(query);
  for (size_t i = 0; i < sequel_query_relation_count(query); ++i) {
    bool joined = false;
    for (size_t j = 0; j < sequel_query_join_count(query); ++j)
      if (joins[j].right_relation == i)
        joined = true;
    if ((relations[i].flags & SEQUEL_RELATION_FLAG_PROJECTION_SCOPE) || ((relations[i].flags & SEQUEL_RELATION_FLAG_NESTED_SCOPE) && !joined))
      return true;
  }
  return false;
}

const char *sequel_summary_matching_close(const char *open, const char *end) {
  size_t depth = 0;
  for (const char *cursor = open; cursor < end; ++cursor) {
    if (*cursor == '(')
      ++depth;
    else if (*cursor == ')' && depth && --depth == 0)
      return cursor;
  }
  return NULL;
}

int sequel_summary_append_redacted_range(char **text, size_t *length, size_t *capacity, const sequel_query_document_t *query, size_t start, size_t end) {
  const sequel_lexed_document_t *lexed = sequel_syntax_lexed(sequel_query_syntax(query));
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  sequel_source_view_t source = sequel_lexed_source(lexed);
  size_t cursor = start;
  size_t bracket_depth = 0;
  for (size_t i = 0; i < token_count; ++i) {
    const sequel_token_t *token = &tokens[i];
    if (token->kind == SEQUEL_TOKEN_EOF || token->range.end <= start)
      continue;
    if (token->range.start >= end)
      break;
    size_t token_start = token->range.start < start ? start : token->range.start;
    size_t token_end = token->range.end > end ? end : token->range.end;
    if (token_start < token_end && source.data[token_start] == ']' && bracket_depth)
      --bracket_depth;
    if (token_start > cursor && !sequel_summary_append_bytes(text, length, capacity, source.data + cursor, token_start - cursor))
      return 0;
    bool token_opens_bracket = token_start < token_end && source.data[token_start] == '[';
    bool token_follows_bracket = token_start > start && source.data[token_start - 1] == '[';
    bool redact = !bracket_depth && !token_opens_bracket && !token_follows_bracket && !sequel_summary_range_is_bracketed(source, token_start) &&
                  ((token->flags & SEQUEL_TOKEN_FLAG_LITERAL) || token->kind == SEQUEL_TOKEN_PARAMETER);
    if (redact) {
      if (!sequel_summary_append_text(text, length, capacity, "?"))
        return 0;
    } else if (token_end > token_start && !sequel_summary_append_bytes(text, length, capacity, source.data + token_start, token_end - token_start)) {
      return 0;
    }
    cursor = token_end;
    if (token_start < token_end && source.data[token_start] == '[')
      ++bracket_depth;
  }
  return cursor < end ? sequel_summary_append_bytes(text, length, capacity, source.data + cursor, end - cursor) : 1;
}

int sequel_summary_normalize_nested_subqueries(char **text, size_t *length, size_t *capacity) {
  static const char ellipsis[] = "\342\200\246";
  if (!text || !*text || !length || !capacity)
    return 1;
  for (char *cursor = *text; (cursor = sequel_summary_find_text(cursor, "IN (SELECT ")) != NULL;) {
    char *projection = cursor + strlen("IN (SELECT ");
    char *from = sequel_summary_find_text(projection, " FROM ");
    if (!from)
      break;
    size_t projection_start = (size_t)(projection - *text);
    size_t from_start = (size_t)(from - *text);
    size_t old_length = from_start - projection_start;
    size_t new_length = sizeof(ellipsis) - 1;
    if (new_length > old_length) {
      size_t growth = new_length - old_length;
      if (*length > SIZE_MAX - growth - 1)
        return 0;
      size_t required = *length + growth + 1;
      if (required > *capacity) {
        char *grown = realloc(*text, required);
        if (!grown)
          return 0;
        *text = grown;
        *capacity = required;
      }
      memmove(*text + projection_start + new_length, *text + from_start, *length - from_start + 1);
      *length += growth;
    } else if (old_length > new_length) {
      size_t shrink = old_length - new_length;
      memmove(*text + projection_start + new_length, *text + from_start, *length - from_start + 1);
      *length -= shrink;
    }
    memcpy(*text + projection_start, ellipsis, new_length);
    cursor = *text + projection_start + new_length;
    char *using = sequel_summary_find_text(cursor, "USING(");
    if (using) {
      size_t offset = (size_t)(using - *text);
      if (*length == SIZE_MAX)
        return 0;
      size_t required = *length + 2;
      if (required > *capacity) {
        char *grown = realloc(*text, required);
        if (!grown)
          return 0;
        *text = grown;
        *capacity = required;
      }
      memmove(*text + offset + 6, *text + offset + 5, *length - offset - 4);
      (*text)[offset + 5] = ' ';
      *length += 1;
    }
    char *limit = sequel_summary_find_text(cursor, "LIMIT ?");
    if (limit)
      limit[strlen("LIMIT ")] = '1';
  }
  return 1;
}
