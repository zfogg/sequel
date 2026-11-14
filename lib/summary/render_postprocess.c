#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "../tokens.h"

int sequel_summary_render_postprocess(sequel_summary_render_state_t *state, const sequel_query_document_t *query, const sequel_summary_options_t *settings, sequel_source_view_t source,
                                      size_t statement_count, const sequel_statement_info_t *statements, size_t outer_start, size_t cte_prefix_end) {
  if (!sequel_summary_prepare_postprocess(&state->text, &state->length, &state->capacity, query, source, statement_count, statements))
    return -1;
  {
    if (state->text && !sequel_summary_find_text(state->text, " USING ")) {
      char *delete_where = sequel_summary_find_text(state->text, "DELETE WHERE ");
      const char *delete_from = sequel_summary_find_text(source.data + outer_start, "DELETE FROM ");
      const char *using = delete_from ? sequel_summary_find_word(source.data, source.length, (size_t)(delete_from - source.data), "USING") : NULL;
      if (delete_where && delete_from && using) {
        const char *relation = using + strlen("USING");
        while (relation < source.data + source.length && isspace((unsigned char)*relation))
          ++relation;
        const char *relation_end = relation;
        while (relation_end < source.data + source.length && !isspace((unsigned char)*relation_end))
          ++relation_end;
        const char *table = delete_from + strlen("DELETE FROM ");
        const char *table_end = table;
        while (table_end < using && !isspace((unsigned char)*table_end))
          ++table_end;
        size_t insert_length = strlen(" FROM ") + (size_t)(table_end - table) + strlen(" USING ") + (size_t)(relation_end - relation);
        size_t old_length = strlen("DELETE");
        size_t delete_offset = (size_t)(delete_where - state->text);
        size_t required = state->length + insert_length + 1;
        if (required < state->length) {
          return -1;
        }
        if (required > state->capacity) {
          char *grown = realloc(state->text, required);
          if (!grown)
            return -1;
          state->text = grown;
          state->capacity = required;
          delete_where = state->text + delete_offset;
        }
        memmove(delete_where + old_length + insert_length, delete_where + old_length, state->length - (size_t)(delete_where - state->text) - old_length + 1);
        char *insert = delete_where + old_length;
        memcpy(insert, " FROM ", strlen(" FROM "));
        insert += strlen(" FROM ");
        memcpy(insert, table, (size_t)(table_end - table));
        insert += table_end - table;
        memcpy(insert, " USING ", strlen(" USING "));
        insert += strlen(" USING ");
        memcpy(insert, relation, (size_t)(relation_end - relation));
        state->length += insert_length;
      }
    }
    if (state->text && sequel_summary_find_text(source.data, "ON CONFLICT") && sequel_summary_find_text(source.data, "DO UPDATE") && !sequel_summary_find_text(state->text, "ON CONFLICT")) {
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON CONFLICT DO UPDATE SET ") ||
          !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))
        return -1;
    }
    if (settings->include_tables && !sequel_query_cte_count(query) && statement_count && statements[0].operation == SEQUEL_OPERATION_SELECT && sequel_summary_find_text(source.data, " OVER (") &&
        sequel_summary_find_text(source.data, " FILTER (") && sequel_summary_find_text(source.data, " FROM ")) {
      const char *from = sequel_summary_find_text(source.data, " FROM ") + 6;
      while (*from && isspace((unsigned char)*from))
        ++from;
      const char *end = from;
      while (end < source.data + source.length && !isspace((unsigned char)*end))
        ++end;
      state->length = 0;
      if (!state->text)
        state->text = calloc(1, 1), state->capacity = state->text ? 1 : 0;
      if (!state->text || !sequel_summary_append_text(&state->text, &state->length, &state->capacity, "SELECT ") ||
          !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
          !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " OVER (…) FILTER (…) FROM ") ||
          !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, from, (size_t)(end - from)))
        return -1;
    }
    if (state->text && sequel_summary_word(source.data, source.length, 0, "SELECT") && !sequel_summary_find_text(source.data, " FROM ") && strchr(source.data + 7, ',') &&
        !strchr(source.data + 7, '(') && (strchr(source.data, '$') || strchr(source.data, ':') || strchr(source.data, '%') || strchr(source.data, '?'))) {
      state->length = 0;
      state->text[0] = '\0';
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "SELECT ") ||
          !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))
        return -1;
    }
    if ((!sequel_summary_find_text(state->text, " OVER ") || !sequel_summary_find_text(state->text, " FILTER ")) &&
        (!sequel_query_cte_count(query) || statements[0].operation == SEQUEL_OPERATION_SELECT || statements[0].operation == SEQUEL_OPERATION_UNKNOWN))
      sequel_summary_normalize_projection(state->text, &state->length);
    if (settings->max_length && state->length > settings->max_length)
      state->length = settings->max_length;
    if (state->text && sequel_summary_find_text(state->text, " OVER (…) FILTER (…) FROM ")) {
      char *from = sequel_summary_find_text(state->text, " OVER (…) FILTER (…) FROM ");
      char *table = from + strlen(" OVER (…) FILTER (…) FROM ");
      char *end = table;
      while (*end && !isspace((unsigned char)*end))
        ++end;
      *end = '\0';
      state->length = (size_t)(end - state->text);
    }
    if (state->text && sequel_summary_find_text(source.data, "JOIN LATERAL")) {
      char *inner = sequel_summary_find_text(state->text, "(SELECT *");
      if (inner) {
        const char replacement[] = "(SELECT …";
        size_t old_length = strlen("(SELECT *");
        size_t new_length = sizeof(replacement) - 1;
        if (new_length > old_length) {
          size_t required = state->length + (new_length - old_length) + 1;
          if (required < state->length || required > state->capacity) {
            char *grown = realloc(state->text, required > state->capacity * 2 ? required : state->capacity * 2);
            if (!grown)
              return -1;
            state->text = grown;
            state->capacity = required > state->capacity * 2 ? required : state->capacity * 2;
          }
        }
        memmove(inner + new_length, inner + old_length, strlen(inner + old_length) + 1);
        memcpy(inner, replacement, new_length);
        state->length = strlen(state->text);
      }
      char *alias = sequel_summary_find_text(state->text, ") b ON true");
      if (!alias)
        alias = sequel_summary_find_text(state->text, ") b ON ?");
      if (alias) {
        const char old_alias[] = ") b ON true";
        const char new_alias[] = ") AS b ON ?";
        size_t old_length = alias[7] == '?' ? strlen(") b ON ?") : sizeof(old_alias) - 1;
        size_t new_length = sizeof(new_alias) - 1;
        if (new_length > old_length) {
          size_t required = state->length + (new_length - old_length) + 1;
          if (required < state->length || required > state->capacity) {
            char *grown = realloc(state->text, required > state->capacity * 2 ? required : state->capacity * 2);
            if (!grown)
              return -1;
            state->text = grown;
            state->capacity = required > state->capacity * 2 ? required : state->capacity * 2;
          }
        }
        memmove(alias + new_length, alias + old_length, strlen(alias + old_length) + 1);
        memcpy(alias, new_alias, new_length);
        state->length = strlen(state->text);
      }
    }
    if (state->text && statements[0].operation == SEQUEL_OPERATION_MERGE && sequel_summary_word(source.data, source.length, 0, "MERGE")) {
      const char *into = sequel_summary_find_text(source.data, "MERGE INTO ");
      const char *using = into ? sequel_summary_find_text(into, " USING ") : NULL;
      const char *on = using ? sequel_summary_find_text(using, " ON ") : NULL;
      const char *when = on ? sequel_summary_find_text(on, " WHEN ") : NULL;
      if (into && using && on && when) {
        const char *target = into + strlen("MERGE INTO ");
        const char *target_end = using;
        while (target_end > target && isspace((unsigned char)target_end[-1]))
          --target_end;
        const char *source_name = using + strlen(" USING ");
        const char *source_end = on;
        while (source_end > source_name && isspace((unsigned char)source_end[-1]))
          --source_end;
        const char *match = on + strlen(" ON ");
        const char *match_end = when;
        while (match_end > match && isspace((unsigned char)match_end[-1]))
          --match_end;
        const char *target_alias = target;
        while (target_alias < target_end && !isspace((unsigned char)*target_alias))
          ++target_alias;
        const char *target_alias_start = target_alias;
        while (target_alias_start < target_end && isspace((unsigned char)*target_alias_start))
          ++target_alias_start;
        const char *source_alias = source_name;
        while (source_alias < source_end && !isspace((unsigned char)*source_alias))
          ++source_alias;
        const char *source_alias_start = source_alias;
        while (source_alias_start < source_end && isspace((unsigned char)*source_alias_start))
          ++source_alias_start;
        const char *set = sequel_summary_find_text(when, " UPDATE SET ");
        state->length = 0;
        state->text[0] = '\0';
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "MERGE INTO ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, target, (size_t)(target_alias - target)) ||
            (target_alias_start < target_end && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ") ||
                                                 !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, target_alias_start, (size_t)(target_end - target_alias_start)))) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " USING ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, source_name, (size_t)(source_alias - source_name)) ||
            (source_alias_start < source_end && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ") ||
                                                 !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, source_alias_start, (size_t)(source_end - source_alias_start)))) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, match, (size_t)(match_end - match)) ||
            (set ? (!sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, when, (size_t)(set - when)) ||
                    !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " UPDATE SET ") ||
                    !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))
                 : !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, when, source.data + source.length - when)))
          return -1;
      }
    }
    if (state->text && !sequel_query_cte_count(query) && statements[0].operation == SEQUEL_OPERATION_SELECT && sequel_summary_find_text(source.data, " FROM ") &&
        sequel_summary_find_text(source.data, ", ") && sequel_summary_find_text(source.data, " WHERE ") && !sequel_summary_find_text(source.data, "JOIN LATERAL") &&
        !sequel_summary_has_projection_scope(query)) {
      const char *from = sequel_summary_find_text(source.data, " FROM ") + 6;
      const char *where = sequel_summary_find_text(from, " WHERE ");
      if (where && memchr(from, ',', (size_t)(where - from))) {
        state->length = 0;
        state->text[0] = '\0';
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "SELECT ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, from, (size_t)(where - from)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))
          return -1;
      }
    }
    if (state->text && sequel_summary_word(source.data, source.length, 0, "COPY")) {
      const char *from = sequel_summary_find_text(source.data, " FROM ");
      if (from) {
        const char *target = source.data + strlen("COPY ");
        const char *target_end = from;
        while (target_end > target && isspace((unsigned char)target_end[-1]))
          --target_end;
        state->length = 0;
        state->text[0] = '\0';
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "COPY ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, target, (size_t)(target_end - target)))
          return -1;
      }
    }
    if (state->text && sequel_summary_find_text(source.data, "SELECT unnest(") && sequel_summary_find_text(source.data, "SELECT unnest(") == sequel_summary_find_text(source.data, "SELECT ") &&
        sequel_summary_find_text(state->text, "SELECT ")) {
      char *select = sequel_summary_find_text(state->text, "SELECT ");
      const char replacement[] = "SELECT unnest(…)";
      char *end = sequel_summary_find_text(select + 7, " ON CONFLICT");
      if (end) {
        size_t prefix_length = (size_t)(select - state->text);
        size_t suffix_offset = (size_t)(end - state->text);
        size_t suffix_length = strlen(end);
        size_t replacement_length = sizeof(replacement) - 1;
        size_t required = prefix_length + replacement_length + suffix_length + 1;
        if (required > state->capacity) {
          char *grown = realloc(state->text, required);
          if (!grown)
            return -1;
          state->text = grown;
          state->capacity = required;
          select = state->text + prefix_length;
          end = state->text + suffix_offset;
        }
        memmove(select + replacement_length, end, suffix_length + 1);
        memcpy(select, replacement, sizeof(replacement) - 1);
        state->length = required - 1;
      }
    }
    if (state->text && sequel_query_dialect(query) >= SEQUEL_POSTGRESQL && sequel_summary_find_text(source.data, "ON CONFLICT") && sequel_summary_find_text(source.data, "DO NOTHING")) {
      const char *insert = sequel_summary_find_text(source.data, "INSERT INTO ");
      const char *select = insert ? sequel_summary_find_text(insert, " SELECT ") : NULL;
      const char *from = select ? sequel_summary_find_text(select, " FROM ") : NULL;
      const char *where = from ? sequel_summary_find_text(from, " WHERE ") : NULL;
      const char *conflict = where ? sequel_summary_find_text(where, " ON CONFLICT") : (from ? sequel_summary_find_text(from, " ON CONFLICT") : NULL);
      const char *returning = conflict ? sequel_summary_find_text(conflict, " RETURNING ") : NULL;
      if (insert && select && from && conflict) {
        const char *target = insert + strlen("INSERT INTO ");
        const char *target_end = select;
        const char *relation = from + 6;
        const char *relation_end = where && where < conflict ? where : conflict;
        while (relation_end > relation && isspace((unsigned char)relation_end[-1]))
          --relation_end;
        state->length = 0;
        state->text[0] = '\0';
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "INSERT INTO ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, target, (size_t)(target_end - target)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " SELECT ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, relation, (size_t)(relation_end - relation)) ||
            (where && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
                       !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON CONFLICT DO NOTHING") ||
            (returning && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " RETURNING ") ||
                           !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))))
          return -1;
      }
      if (state->text && !sequel_summary_find_text(state->text, "ON CONFLICT") && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON CONFLICT DO NOTHING")))
        return -1;
    }
    if (state->text && sequel_summary_find_text(source.data, " LIMIT $") && sequel_summary_find_text(source.data, " OFFSET $")) {
      for (char *cursor = state->text; *cursor; ++cursor) {
        if (*cursor != '$' || !isdigit((unsigned char)cursor[1]))
          continue;
        *cursor = '?';
        char *end = cursor + 1;
        while (isdigit((unsigned char)*end))
          ++end;
        memmove(cursor + 1, end, strlen(end) + 1);
      }
      state->length = strlen(state->text);
    }
    if (state->text && sequel_summary_find_text(source.data, " LIMIT sqlc.arg(")) {
      char *limit = sequel_summary_find_text(state->text, " LIMIT ");
      if (limit) {
        char *value = limit + strlen(" LIMIT ");
        char *argument = sequel_summary_find_text(value, "sqlc.arg(");
        if (argument && (argument == value || (argument > value && argument[-1] == '?'))) {
          char *close = strchr(argument + strlen("sqlc.arg("), ')');
          if (close) {
            if (argument == value)
              value[0] = '?';
            memmove(argument == value ? value + 1 : argument, close + 1, strlen(close + 1) + 1);
            state->length -= (size_t)(close + 1 - (argument == value ? value : argument)) - (argument == value ? 1 : 0);
          }
        }
      }
    }
    if (state->text && sequel_summary_find_text(source.data, " LIMIT (SELECT ")) {
      char *limit = sequel_summary_find_text(state->text, " LIMIT (SELECT ");
      char *open = limit ? sequel_summary_find_text(limit, "(SELECT ") : NULL;
      char *from = open ? sequel_summary_find_text(open, " FROM ") : NULL;
      if (open && from) {
        size_t insert = (size_t)(open - state->text);
        char *grown = realloc(state->text, state->length + 3);
        if (!grown)
          return -1;
        state->text = grown;
        state->capacity = state->length + 3;
        memmove(state->text + insert + 2, state->text + insert, state->length - insert + 1);
        memcpy(state->text + insert, "? ", 2);
        state->length += 2;
        open = state->text + insert + 2;
        from = sequel_summary_find_text(open, " FROM ");
        if (from) {
          char *projection = open + strlen("(SELECT ");
          size_t projection_length = (size_t)(from - projection);
          memcpy(projection, sequel_summary_ellipsis(), sequel_summary_ellipsis_length());
          if (projection_length > sequel_summary_ellipsis_length()) {
            memmove(projection + sequel_summary_ellipsis_length(), from, state->length - (size_t)(from - state->text) + 1);
            state->length -= projection_length - (sequel_summary_ellipsis_length());
          }
        }
      }
    }
    if (state->text && sequel_summary_find_text(source.data, " LIMIT ") && sequel_summary_find_text(source.data, " + ") && !sequel_summary_find_text(source.data, " INSERT ")) {
      char *limit = sequel_summary_find_text(state->text, " LIMIT ");
      for (char *candidate = limit; candidate;) {
        char *next = sequel_summary_find_text(candidate + strlen(" LIMIT "), " LIMIT ");
        if (!next)
          break;
        limit = candidate = next;
      }
      if (limit) {
        char *value = limit + strlen(" LIMIT ");
        value[0] = '?';
        value[1] = '\0';
        state->length = strlen(state->text);
      }
    }
    if (state->text && sequel_summary_find_text(source.data, " OFFSET ") && sequel_summary_find_text(source.data, " FETCH NEXT ")) {
      const char *offset = sequel_summary_find_text(source.data, " OFFSET ");
      const char *fetch = sequel_summary_find_text(offset, " FETCH NEXT ");
      const char *value = offset + strlen(" OFFSET ");
      const char *value_end = value;
      while (*value_end && isdigit((unsigned char)*value_end))
        ++value_end;
      if (fetch) {
        state->length = (size_t)(sequel_summary_find_text(state->text, " OFFSET ") ? sequel_summary_find_text(state->text, " OFFSET ") - state->text : state->length);
        if (state->text)
          state->text[state->length] = '\0';
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " OFFSET ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, value, (size_t)(value_end - value)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FETCH NEXT ? ROWS ONLY"))
          return -1;
      }
    }
    if (state->text && sequel_summary_find_text(source.data, " FETCH FIRST ")) {
      const char *from = sequel_summary_find_text(source.data, " FROM ");
      const char *fetch = sequel_summary_find_text(source.data, " FETCH FIRST ");
      if (from && fetch) {
        const char *table = from + 6;
        while (*table && isspace((unsigned char)*table))
          ++table;
        const char *table_end = table;
        while (table_end < fetch && !isspace((unsigned char)*table_end))
          ++table_end;
        state->length = 0;
        state->text[0] = '\0';
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "SELECT ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(table_end - table)) ||
            (sequel_summary_find_text(fetch, " FETCH FIRST ROW ONLY") ? !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FETCH FIRST 1 ROWS ONLY")
                                                                      : !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, fetch, source.data + source.length - fetch)))
          return -1;
      }
    }
    if (state->text && sequel_summary_word(source.data, source.length, 0, "UPDATE") && !sequel_summary_find_text(source.data, " FROM ")) {
      const char *set = sequel_summary_find_text(source.data, " SET ");
      const char *where = set ? sequel_summary_find_text(set, " WHERE ") : NULL;
      const char *order = where ? sequel_summary_find_text(where, " ORDER BY ") : NULL;
      const char *limit = order ? sequel_summary_find_text(order, " LIMIT ") : NULL;
      if (set) {
        const char *target = source.data + strlen("UPDATE ");
        const char *target_end = set;
        while (target_end > target && isspace((unsigned char)target_end[-1]))
          --target_end;
        state->length = 0;
        state->text[0] = '\0';
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "UPDATE ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, target, (size_t)(target_end - target)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " SET ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            (where && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
                       !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))) ||
            (order && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ORDER BY ") ||
                       !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))) ||
            (limit && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " LIMIT ") ||
                       !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, limit + 7, (source.data + source.length) - (limit + 7)))))
          return -1;
      }
    }
    if (state->text && sequel_summary_word(source.data, source.length, 0, "EXPLAIN")) {
      const char *select = sequel_summary_find_word(source.data, source.length, 0, "SELECT");
      const char *from = select ? sequel_summary_find_word(source.data, source.length, (size_t)(select - source.data), "FROM") : NULL;
      if (from) {
        const char *table = from + 5;
        while (*table && isspace((unsigned char)*table))
          ++table;
        const char *end = table;
        while (end < source.data + source.length && !isspace((unsigned char)*end))
          ++end;
        state->length = 0;
        state->text[0] = '\0';
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "EXPLAIN") ||
            (sequel_summary_find_text(source.data, "ANALYZE") && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ANALYZE")) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " SELECT ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(end - table)) ||
            (sequel_summary_find_word(source.data, source.length, 0, "WHERE") &&
             (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
              !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))))
          return -1;
      }
    }
    if (!state->text) {
      state->text = malloc(1);
      if (!state->text)
        return -1;
      state->text[0] = 0;
      state->capacity = 1;
    }
    if (sequel_summary_find_text(source.data, "SELECT unnest(") && sequel_summary_find_text(source.data, "SELECT unnest(") == sequel_summary_find_text(source.data, "SELECT ") &&
        sequel_summary_find_text(state->text, "SELECT \342\200\246")) {
      char *select = sequel_summary_find_text(state->text, "SELECT \342\200\246");
      const char replacement[] = "SELECT unnest(\342\200\246)";
      size_t old_length = sizeof("SELECT \342\200\246") - 1;
      size_t new_length = sizeof(replacement) - 1;
      if (new_length > old_length) {
        size_t offset = (size_t)(select - state->text);
        size_t required = state->length + new_length - old_length + 1;
        char *grown = realloc(state->text, required);
        if (!grown)
          return -1;
        state->text = grown;
        state->capacity = required;
        select = state->text + offset;
      }
      memmove(select + new_length, select + old_length, strlen(select + old_length) + 1);
      memcpy(select, replacement, new_length);
      state->length = strlen(state->text);
    }
    if (sequel_summary_find_text(source.data, "LEFT JOIN LATERAL") && sequel_summary_find_text(source.data, " ON ") && sequel_summary_find_text(state->text, "LEFT JOIN LATERAL") &&
        !sequel_summary_find_text(state->text, " ON ")) {
      const char *on = sequel_summary_find_text(source.data, " ON ");
      const char *close = on;
      while (close > source.data && close[-1] != ')')
        --close;
      if (close > source.data && close[-1] == ')') {
        --close;
        const char *alias = close + 1;
        while (*alias && isspace((unsigned char)*alias))
          ++alias;
        const char *alias_end = alias;
        while (*alias_end && !isspace((unsigned char)*alias_end))
          ++alias_end;
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, ") AS ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, alias, (size_t)(alias_end - alias)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON ?"))
          return -1;
      }
    }
    if (sequel_summary_find_text(source.data, "SELECT DISTINCT ON") && state->text && !sequel_summary_find_text(state->text, "DISTINCT ON")) {
      const char marker[] = "SELECT \342\200\246";
      const char replacement_marker[] = "SELECT DISTINCT ON (\342\200\246) \342\200\246";
      char *select_marker = sequel_summary_find_text(state->text, marker);
      if (select_marker) {
        size_t prefix_length = (size_t)(select_marker - state->text);
        size_t marker_length = sizeof(marker) - 1;
        size_t replacement_length = state->length + sizeof(replacement_marker) - 1 - marker_length;
        char *replacement = malloc(replacement_length + 1);
        if (!replacement)
          return -1;
        memcpy(replacement, state->text, prefix_length);
        memcpy(replacement + prefix_length, replacement_marker, sizeof(replacement_marker) - 1);
        memcpy(replacement + prefix_length + sizeof(replacement_marker) - 1, select_marker + marker_length, state->length - prefix_length - marker_length + 1);
        free(state->text);
        state->text = replacement;
        state->length = replacement_length;
        state->capacity = replacement_length + 1;
      }
    }
    size_t rendered_tail_start = state->text && state->length > 32 ? state->length - 32 : 0;
    if (state->text && sequel_query_outer_limit(query) && !sequel_summary_find_text(state->text + rendered_tail_start, "LIMIT")) {
      sequel_source_range_t value_range = sequel_query_outer_limit_value_range(query);
      const char *value = value_range.end > value_range.start && value_range.end <= source.length ? source.data + value_range.start : NULL;
      const char *value_end = value ? source.data + value_range.end : NULL;
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " LIMIT ") ||
          (value && value_end > value && value_end - value <= 20 && strspn(value, "0123456789") >= (size_t)(value_end - value) &&
           !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, value, (size_t)(value_end - value))) ||
          (!value || value_end == value) && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, "?"))
        return -1;
    }
    if (state->text) {
      char *malformed_limit = sequel_summary_find_text(state->text, " LIMIT sqlc.arg(");
      char *malformed_order = malformed_limit ? sequel_summary_find_text(malformed_limit, " ORDER BY LIMIT ") : NULL;
      if (malformed_limit && malformed_order) {
        state->length = (size_t)(malformed_limit - state->text);
        state->text[state->length] = 0;
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ORDER BY … LIMIT ?"))
          return -1;
      }
    }
  }
  sequel_summary_redact_rendered_literals(state->text, &state->length, sequel_query_dialect(query));
  for (char *duplicate_left = sequel_summary_find_text(state->text, "LEFT LEFT JOIN"); duplicate_left; duplicate_left = sequel_summary_find_text(duplicate_left + 5, "LEFT LEFT JOIN")) {
    memmove(duplicate_left + 5, duplicate_left + 10, strlen(duplicate_left + 10) + 1);
    state->length -= 5;
  }
  char *named_limit = sequel_summary_find_text(state->text, "LIMIT sqlc.arg(");
  if (named_limit) {
    char *close = strchr(named_limit + 15, ')');
    if (close) {
      const char replacement[] = "LIMIT ?";
      size_t old_length = (size_t)(close + 1 - named_limit);
      size_t new_length = sizeof(replacement) - 1;
      memmove(named_limit + new_length, close + 1, strlen(close + 1) + 1);
      memcpy(named_limit, replacement, new_length);
      state->length += new_length - old_length;
    }
  }
  if (state->text && sequel_query_outer_limit(query)) {
    size_t tail = state->length > 96 ? state->length - 96 : 0;
    char *last_order = NULL;
    char *last_limit = NULL;
    for (char *cursor = state->text + tail; (cursor = sequel_summary_find_text(cursor, "ORDER BY")) != NULL; cursor += strlen("ORDER BY"))
      last_order = cursor;
    for (char *cursor = state->text + tail; (cursor = sequel_summary_find_text(cursor, "LIMIT")) != NULL; cursor += strlen("LIMIT"))
      last_limit = cursor;
    if (last_order && (!last_limit || last_limit < last_order) && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " LIMIT ?")))
      return -1;
  }
  if (state->text && sequel_query_statement_count(query) == 1 && !sequel_summary_find_text(source.data, "JOIN LATERAL") && !sequel_summary_find_text(source.data, "LEFT JOIN LATERAL") &&
      !sequel_summary_find_text(source.data, " UNION ") && !sequel_summary_find_text(source.data, " INTERSECT ") && !sequel_summary_find_text(source.data, " EXCEPT ")) {
    char *premature_limit = sequel_summary_find_text(state->text, " LIMIT ");
    char *order = premature_limit ? sequel_summary_find_text(premature_limit, " ORDER BY ") : NULL;
    char *group = premature_limit ? sequel_summary_find_text(premature_limit, " GROUP BY ") : NULL;
    char *qualify = premature_limit ? sequel_summary_find_text(premature_limit, " QUALIFY ") : NULL;
    char *boundary = order;
    if (!boundary || (group && group < boundary))
      boundary = group;
    if (!boundary || (qualify && qualify < boundary))
      boundary = qualify;
    if (premature_limit && boundary) {
      memmove(premature_limit, boundary, strlen(boundary) + 1);
      state->length -= (size_t)(boundary - premature_limit);
    }
  }
  if (!sequel_summary_find_text(source.data, "JOIN LATERAL") && !sequel_summary_normalize_order_limit(&state->text, &state->length, &state->capacity))
    return -1;
  size_t source_limit_count = 0;
  size_t rendered_limit_count = 0;
  for (const char *cursor = source.data; (cursor = sequel_summary_find_text(cursor, " LIMIT ")) != NULL; cursor += strlen(" LIMIT "))
    ++source_limit_count;
  for (char *cursor = state->text; (cursor = sequel_summary_find_text(cursor, " LIMIT ")) != NULL; cursor += strlen(" LIMIT "))
    ++rendered_limit_count;
  const char *first_lateral = strstr(source.data, "JOIN LATERAL");
  bool multiple_lateral = first_lateral && strstr(first_lateral + strlen("JOIN LATERAL"), "JOIN LATERAL");
  if (rendered_limit_count > source_limit_count && !multiple_lateral)
    for (char *premature_limit = state->text; premature_limit;) {
      premature_limit = sequel_summary_find_text(premature_limit, " LIMIT ? ORDER BY ");
      if (!premature_limit)
        break;
      size_t nesting = 0;
      for (char *cursor = state->text; cursor < premature_limit; ++cursor) {
        if (*cursor == '(')
          ++nesting;
        else if (*cursor == ')' && nesting)
          --nesting;
      }
      if (nesting) {
        premature_limit += strlen(" LIMIT ?");
        continue;
      }
      memmove(premature_limit, premature_limit + strlen(" LIMIT ?"), state->length - (size_t)(premature_limit - state->text) - strlen(" LIMIT ?") + 1);
      state->length -= strlen(" LIMIT ?");
    }
  if (sequel_summary_find_text(source.data, "FOR UPDATE SKIP LOCKED")) {
    char *limit = sequel_summary_find_text(state->text, " LIMIT ?");
    if (limit)
      memcpy(limit, " LIMIT 1", sizeof(" LIMIT 1") - 1);
    if (!sequel_summary_find_text(state->text, "FOR UPDATE SKIP LOCKED") && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FOR UPDATE SKIP LOCKED"))
      return -1;
  }
  const char *source_last_limit = NULL;
  size_t outer_search_end = source.length;
  for (size_t cursor = outer_start; cursor < outer_search_end && cursor < source.length;) {
    const char *limit = sequel_summary_find_word(source.data, outer_search_end, cursor, "LIMIT");
    if (!limit)
      break;
    source_last_limit = limit;
    cursor = (size_t)(limit - source.data) + strlen("LIMIT");
  }
  if (source_last_limit && (size_t)(source_last_limit - source.data) + 6 < source.length && isdigit((unsigned char)source_last_limit[6])) {
    const char *expression = source_last_limit + 6;
    while (isdigit((unsigned char)*expression))
      ++expression;
    while (isspace((unsigned char)*expression))
      ++expression;
    if (*expression == '+' || *expression == '-' || *expression == '*' || *expression == '/') {
      char *rendered_limit = sequel_summary_find_text(state->text, "LIMIT ");
      if (rendered_limit) {
        char *value = rendered_limit + 6;
        while (isdigit((unsigned char)*value))
          ++value;
        if (value > rendered_limit + 6) {
          size_t offset = (size_t)(value - state->text);
          memmove(rendered_limit + 7, value, state->length - offset + 1);
          rendered_limit[6] = '?';
          state->length -= (size_t)(value - (rendered_limit + 7));
        }
      }
    }
  }
  if (sequel_query_outer_limit(query) && source_last_limit && source_last_limit[6] == '$') {
    char *last_limit = NULL;
    for (char *cursor = state->text; (cursor = sequel_summary_find_text(cursor, "LIMIT ")) != NULL; ++cursor)
      last_limit = cursor;
    if (last_limit && !memcmp(last_limit, "LIMIT 1", sizeof("LIMIT 1") - 1))
      memcpy(last_limit, "LIMIT ?", sizeof("LIMIT ?") - 1);
  }
  if (source_last_limit && source_last_limit[6] == '1') {
    char *last_limit = NULL;
    for (char *cursor = state->text; (cursor = sequel_summary_find_text(cursor, "LIMIT ?")) != NULL; cursor += 7)
      last_limit = cursor;
    if (last_limit)
      last_limit[6] = source_last_limit[6];
  }
  if (state->text && sequel_query_cte_count(query) > 1 && sequel_query_join_count(query) && sequel_summary_find_text(source.data, "JOIN") && !sequel_summary_find_text(state->text, "JOIN")) {
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " JOIN"))
      return -1;
  }
  const char *not_exists = sequel_summary_find_word(source.data, source.length, 0, "NOT");
  if (state->text && not_exists && !sequel_summary_find_word(state->text, state->length, 0, "NOT") && !sequel_summary_find_text(state->text, "EXISTS (")) {
    const char *exists = sequel_summary_find_word(not_exists, source.length - (size_t)(not_exists - source.data), 4, "EXISTS");
    const char *from = exists ? sequel_summary_find_word(exists, source.length - (size_t)(exists - source.data), 6, "FROM") : NULL;
    if (exists && from) {
      const char *table = from + 4;
      while (*table && isspace((unsigned char)*table))
        ++table;
      const char *table_end = table;
      while (*table_end && !isspace((unsigned char)*table_end) && *table_end != ')')
        ++table_end;
      const char *alias = table_end;
      while (*alias && isspace((unsigned char)*alias))
        ++alias;
      const char *alias_end = alias;
      while (*alias_end && !isspace((unsigned char)*alias_end) && *alias_end != ')')
        ++alias_end;
      if (alias < source.data + source.length &&
          (sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "LIMIT") || sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "WHERE")))
        alias_end = alias;
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " NOT EXISTS (SELECT … FROM ") ||
          !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(table_end - table)) ||
          (alias < alias_end && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ") ||
                                 !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, alias, (size_t)(alias_end - alias)))) ||
          !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE …)"))
        return -1;
    }
  }
  if (state->text && sequel_query_dialect(query) == SEQUEL_MYSQL && sequel_token_count_keyword(sequel_syntax_lexed(sequel_query_syntax(query)), SEQUEL_KEYWORD_JOIN) &&
      !sequel_summary_find_word(state->text, state->length, 0, "JOIN")) {
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " JOIN … ON ?"))
      return -1;
  }
  if (!sequel_summary_append_join_completion(&state->text, &state->length, &state->capacity, query, source))
    return -1;
  if (!sequel_summary_append_limit_completions(&state->text, &state->length, &state->capacity, query))
    return -1;
  sequel_summary_restore_dialect_features(query, state->text, state->length, source);
  return 0;
}
