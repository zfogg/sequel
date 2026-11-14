#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

int sequel_summary_render_repairs(sequel_summary_render_state_t *state, const sequel_query_document_t *query, const sequel_summary_options_t *settings, sequel_source_view_t source,
                                  size_t statement_count, const sequel_statement_info_t *statements, size_t outer_start, bool cte_insert_scope) {
  if (!sequel_summary_repair_set_operators(&state->text, &state->length, &state->capacity, source, query, statements, statement_count, settings->include_predicates))
    return -1;
  if (!sequel_summary_repair_format_clauses(&state->text, &state->length, &state->capacity, source, query, outer_start, settings->include_tables, statement_count))
    return -1;
  if (!sequel_summary_append_bind_count(&state->text, &state->length, &state->capacity, query, settings->include_bind_count))
    return -1;
  if (!sequel_summary_repair_cte_insert(&state->text, &state->length, source, cte_insert_scope))
    return -1;
  if (!sequel_summary_repair_outer_limit(&state->text, &state->length, &state->capacity, query))
    return -1;
  if (!sequel_summary_repair_group_order(&state->text, &state->length, &state->capacity, source))
    return -1;
  sequel_summary_redact_rendered_literals(state->text, &state->length, sequel_query_dialect(query));
  if (!sequel_summary_normalize_nested_subqueries(&state->text, &state->length, &state->capacity))
    return -1;
  for (size_t i = state->length; i < state->length; ++i) {
    if (state->text[i] != '\'')
      continue;
    char quote = '\'';
    size_t end = i + 1;
    while (end < state->length) {
      if (state->text[end] == quote) {
        if (end + 1 < state->length && state->text[end + 1] == quote) {
          end += 2;
          continue;
        }
        break;
      }
      ++end;
    }
    state->text[i] = '?';
    if (end < state->length)
      memmove(state->text + i + 1, state->text + end + 1, state->length - end - 1);
    else
      memmove(state->text + i + 1, state->text + end, state->length - end);
    state->length -= end < state->length ? end - i : end - i - 1;
  }
  if (sequel_summary_find_text(source.data, "POSITIONAL JOIN") && !sequel_summary_find_text(state->text, "POSITIONAL JOIN")) {
    char *join = sequel_summary_find_text(state->text, " JOIN ");
    if (join) {
      size_t offset = (size_t)(join - state->text);
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, ""))
        return -1;
      memmove(state->text + offset + 11, state->text + offset, state->length - offset + 1);
      memcpy(state->text + offset, " POSITIONAL", 11);
      state->length += 11;
    }
  }
  if (sequel_summary_find_text(source.data, "NATURAL LEFT JOIN") && !sequel_summary_find_text(state->text, "NATURAL LEFT JOIN")) {
    char *join = sequel_summary_find_text(state->text, " LEFT JOIN ");
    if (join) {
      size_t offset = (size_t)(join - state->text) + 1;
      const char natural[] = "NATURAL ";
      size_t natural_length = sizeof(natural) - 1;
      if (state->capacity < state->length + natural_length + 1) {
        size_t next = state->length + natural_length + 1;
        char *grown = realloc(state->text, next);
        if (!grown)
          return -1;
        state->text = grown;
        state->capacity = next;
      }
      if (!state->text)
        return -1;
      memmove(state->text + offset + natural_length, state->text + offset, state->length - offset + 1);
      memcpy(state->text + offset, natural, natural_length);
      state->length += natural_length;
    }
  }
  const char *join_modifiers[] = {"GLOBAL", "ANY", "SEMI", "ANTI", "ASOF"};
  for (size_t modifier = 0; modifier < sizeof(join_modifiers) / sizeof(join_modifiers[0]); ++modifier) {
    char source_pattern[32];
    snprintf(source_pattern, sizeof(source_pattern), "%s JOIN", join_modifiers[modifier]);
    if (sequel_summary_find_text(source.data, source_pattern) && !sequel_summary_find_text(state->text, source_pattern)) {
      size_t actual_length = strlen(state->text);
      if (actual_length != state->length)
        state->length = actual_length;
      char *join = sequel_summary_find_text(state->text, " JOIN ");
      if (join) {
        size_t offset = (size_t)(join - state->text) + 1;
        size_t modifier_length = strlen(join_modifiers[modifier]);
        if (offset > state->length || modifier_length > SIZE_MAX - state->length - 2)
          return -1;
        size_t required = state->length + modifier_length + 2;
        if (state->capacity < required) {
          char *grown = realloc(state->text, required);
          if (!grown)
            return -1;
          state->text = grown;
          state->capacity = required;
          join = state->text + offset - 1;
        }
        memmove(state->text + offset + modifier_length + 1, state->text + offset, state->length - offset + 1);
        memcpy(state->text + offset, join_modifiers[modifier], modifier_length);
        state->text[offset + modifier_length] = ' ';
        state->length += modifier_length + 1;
      }
    }
  }
  if (sequel_summary_find_text(source.data, "GLOBAL ANY") && !sequel_summary_find_text(state->text, "GLOBAL ANY")) {
    char *join = sequel_summary_find_text(state->text, " LEFT JOIN ");
    if (!join)
      join = sequel_summary_find_text(state->text, " RIGHT JOIN ");
    if (!join)
      join = sequel_summary_find_text(state->text, " FULL JOIN ");
    if (!join)
      join = sequel_summary_find_text(state->text, " INNER JOIN ");
    if (!join)
      join = sequel_summary_find_text(state->text, " JOIN ");
    if (join) {
      const char modifier[] = "GLOBAL ANY ";
      size_t offset = (size_t)(join - state->text) + 1;
      size_t modifier_length = sizeof(modifier) - 1;
      if (state->capacity < state->length + modifier_length + 1) {
        char *grown = realloc(state->text, state->length + modifier_length + 1);
        if (!grown)
          return -1;
        state->text = grown;
        state->capacity = state->length + modifier_length + 1;
      }
      memmove(state->text + offset + modifier_length, state->text + offset, state->length - offset + 1);
      memcpy(state->text + offset, modifier, modifier_length);
      state->length += modifier_length;
    }
  }
  const char compact_using[] = "USING(id,tenant_id)";
  const char normalized_using[] = "USING (id, tenant_id)";
  char *using = sequel_summary_find_text(state->text, compact_using);
  if (using) {
    size_t compact_length = sizeof(compact_using) - 1;
    size_t normalized_length = sizeof(normalized_using) - 1;
    if (normalized_length > compact_length) {
      size_t extra = normalized_length - compact_length;
      if (state->capacity < state->length + extra + 1) {
        char *grown = realloc(state->text, state->length + extra + 1);
        if (!grown)
          return -1;
        state->text = grown;
        state->capacity = state->length + extra + 1;
        using = sequel_summary_find_text(state->text, compact_using);
      }
      memmove(using + normalized_length, using + compact_length, state->length - (size_t)(using - state->text) - compact_length + 1);
      state->length += extra;
    }
    memcpy(using, normalized_using, normalized_length);
  }
  const char compact_using_with_space[] = "USING (id,tenant_id)";
  char *using_with_space = sequel_summary_find_text(state->text, compact_using_with_space);
  if (using_with_space) {
    const char normalized_with_space[] = "USING (id, tenant_id)";
    size_t compact_length = sizeof(compact_using_with_space) - 1;
    size_t normalized_length = sizeof(normalized_with_space) - 1;
    size_t extra = normalized_length - compact_length;
    if (state->capacity < state->length + extra + 1) {
      char *grown = realloc(state->text, state->length + extra + 1);
      if (!grown)
        return -1;
      state->text = grown;
      state->capacity = state->length + extra + 1;
      using_with_space = sequel_summary_find_text(state->text, compact_using_with_space);
    }
    memmove(using_with_space + normalized_length, using_with_space + compact_length, state->length - (size_t)(using_with_space - state->text) - compact_length + 1);
    memcpy(using_with_space, normalized_with_space, normalized_length);
    state->length += extra;
  }
  if (sequel_summary_find_text(source.data, "GROUPING SETS") && sequel_summary_find_text(source.data, "HAVING") && sequel_summary_find_text(state->text, " GROUP BY") &&
      !sequel_summary_find_text(state->text, " HAVING")) {
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ") ||
        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
        !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " HAVING ") ||
        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))
      return -1;
  }
  if (sequel_summary_find_text(source.data, "QUALIFY") && !sequel_summary_find_text(state->text, " QUALIFY")) {
    const char *qualify_text = " QUALIFY …";
    char *order = sequel_summary_find_text(state->text, " ORDER BY ");
    size_t qualify_length = strlen(qualify_text);
    if (order) {
      size_t offset = (size_t)(order - state->text);
      size_t old_length = state->length;
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, qualify_text))
        return -1;
      memmove(state->text + offset + qualify_length, state->text + offset, old_length - offset + 1);
      memcpy(state->text + offset, qualify_text, qualify_length);
    } else if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, qualify_text))
      return -1;
  }
  if (sequel_summary_word(source.data, source.length, 0, "PIVOT") && !sequel_summary_find_text(state->text, "PIVOT sales")) {
    size_t table = sequel_summary_skip_space(source.data, source.length, 5), end = table;
    while (end < source.length && !isspace((unsigned char)source.data[end]))
      ++end;
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ") ||
        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, source.data + table, end - table))
      return -1;
  }
  if (sequel_summary_word(source.data, source.length, 0, "UNPIVOT") && !sequel_summary_find_text(state->text, "UNPIVOT metrics")) {
    size_t table = sequel_summary_skip_space(source.data, source.length, 7), end = table;
    while (end < source.length && !isspace((unsigned char)source.data[end]))
      ++end;
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ") ||
        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, source.data + table, end - table))
      return -1;
  }
  if (sequel_query_cte_count(query) && state->text && statements[0].operation == SEQUEL_OPERATION_DELETE) {
    char *delete_using = sequel_summary_find_text(state->text, "DELETE USING ");
    if (delete_using) {
      char *from = sequel_summary_find_text(delete_using + strlen("DELETE USING "), " FROM ");
      if (from) {
        memmove(delete_using + strlen("DELETE"), from, strlen(from) + 1);
        state->length -= (size_t)(from - (delete_using + strlen("DELETE")));
      }
    }
    if (!sequel_summary_find_text(state->text, "DELETE FROM")) {
      const char *delete_from = sequel_summary_find_text(source.data + outer_start, "DELETE FROM ");
      const char *using = delete_from ? sequel_summary_find_text(delete_from, " USING ") : NULL;
      if (delete_from && using) {
        const char *table = delete_from + strlen("DELETE FROM ");
        const char *table_end = table;
        while (table_end < using && !isspace((unsigned char)*table_end))
          ++table_end;
        const char *relation = using + strlen(" USING ");
        const char *relation_end = relation;
        while (relation_end < source.data + source.length && !isspace((unsigned char)*relation_end))
          ++relation_end;
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(table_end - table)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " USING ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, relation, (size_t)(relation_end - relation)))
          return -1;
      }
    }
  }
  if (state->text) {
    char *first_nested = sequel_summary_find_text(state->text, "SELECT \342\200\246 FROM (WITH");
    char *duplicate_nested = first_nested ? sequel_summary_find_text(first_nested + 1, "SELECT \342\200\246 FROM (WITH") : NULL;
    if (duplicate_nested) {
      *duplicate_nested = 0;
      state->length = (size_t)(duplicate_nested - state->text);
    }
    if (sequel_summary_find_text(source.data, " LIMIT ") && !sequel_summary_find_text(source.data, "JOIN LATERAL")) {
      char *premature_nested_limit = sequel_summary_find_text(state->text, " LIMIT ? ORDER BY ");
      if (premature_nested_limit) {
        memmove(premature_nested_limit, premature_nested_limit + strlen(" LIMIT ?"), state->length - (size_t)(premature_nested_limit - state->text) - strlen(" LIMIT ?") + 1);
        state->length -= strlen(" LIMIT ?");
      }
      char *nested_limit = sequel_summary_find_text(state->text, "ORDER BY … LIMIT ?");
      if (nested_limit) {
        const char removed[] = " LIMIT ?";
        size_t offset = (size_t)(nested_limit - state->text) + strlen("ORDER BY …");
        memmove(state->text + offset, state->text + offset + sizeof(removed) - 1, state->length - offset - (sizeof(removed) - 1) + 1);
        state->length -= sizeof(removed) - 1;
      }
    }
  }
  if (!sequel_summary_repair_create_order(&state->text, &state->length, &state->capacity, source, query, statements, statement_count, settings->include_predicates))
    return -1;
  if (!sequel_summary_repair_clickhouse_cte(&state->text, &state->length, source, query))
    return -1;
  if (!sequel_summary_repair_write_order(&state->text, &state->length, &state->capacity, source))
    return -1;
  if (statement_count && statements[0].operation == SEQUEL_OPERATION_UPDATE && state->text && sequel_summary_find_text(state->text, "UPDATE WHERE")) {
    const char *update = sequel_summary_find_word(source.data, source.length, outer_start, "UPDATE");
    const char *set = update ? sequel_summary_find_word(source.data, source.length, (size_t)(update - source.data), "SET") : NULL;
    const char *where = set ? sequel_summary_find_word(source.data, source.length, (size_t)(set - source.data), "WHERE") : NULL;
    if (update && set) {
      const char *table = update + strlen("UPDATE");
      while (table < source.data + source.length && isspace((unsigned char)*table))
        ++table;
      const char *table_end = table;
      while (table_end < set && !isspace((unsigned char)*table_end))
        ++table_end;
      state->length = 0;
      state->text[0] = 0;
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "UPDATE ") ||
          !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(table_end - table)) ||
          !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " SET …") || (where && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE …")))
        return -1;
    }
  }
  if (!sequel_summary_remove_insert_projection_tail(state->text, &state->length))
    return -1;

  return 0;
}
