#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

int sequel_summary_render_clause_repairs(sequel_summary_render_state_t *state, const sequel_query_document_t *query, const sequel_summary_options_t *settings, sequel_source_view_t source,
                                         size_t statement_count, const sequel_statement_info_t *statements, size_t outer_start, bool insert_values_scope, bool cte_insert_scope) {
  if (settings->include_operation && sequel_query_cte_count(query) && statement_count && !cte_insert_scope &&
      (statements[0].operation == SEQUEL_OPERATION_DELETE || statements[0].operation == SEQUEL_OPERATION_UPDATE)) {
    const char *operation = statements[0].operation == SEQUEL_OPERATION_DELETE ? "DELETE" : "UPDATE";
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, operation))
      return -1;
  }
  if (insert_values_scope && sequel_summary_find_text(source.data, " ON CONFLICT") && !sequel_summary_find_text(source.data, "DO NOTHING") &&
      !sequel_summary_find_text(state->text ? state->text : "", " ON CONFLICT")) {
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON CONFLICT DO UPDATE SET ") ||
        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))
      return -1;
  }
  if (insert_values_scope && !cte_insert_scope && !sequel_summary_find_text(source.data, "DELETE FROM") && sequel_summary_find_text(source.data, " RETURNING ") &&
      !sequel_summary_find_text(state->text ? state->text : "", " RETURNING ") &&
      (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " RETURNING ") ||
       !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length())))
    return -1;
  if (statement_count && (sequel_summary_find_text(source.data, " DELETE FROM") || sequel_summary_find_text(source.data, "UPDATE ")) && sequel_summary_find_text(source.data, " USING ") &&
      !sequel_summary_find_text(state->text ? state->text : "", " USING ")) {
    const char *using = sequel_summary_find_text(source.data, " USING ") + 7;
    const char *end = using;
    while (*end && !isspace((unsigned char)*end))
      ++end;
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " USING ") ||
        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, using, (size_t)(end - using)))
      return -1;
  }
  while (outer_start < source.length && (isspace((unsigned char)source.data[outer_start]) || source.data[outer_start] == ')'))
    ++outer_start;
  bool outer_write =
      statement_count &&
      (statements[0].operation == SEQUEL_OPERATION_DELETE || statements[0].operation == SEQUEL_OPERATION_UPDATE ||
       (sequel_query_cte_count(query) && (sequel_summary_word(source.data, source.length, outer_start, "DELETE") || sequel_summary_word(source.data, source.length, outer_start, "UPDATE"))));
  if (settings->include_tables && statement_count && outer_write && !sequel_summary_find_text(state->text ? state->text : "", "DELETE FROM") &&
      !sequel_summary_find_text(state->text ? state->text : "", "UPDATE ")) {
    size_t search = outer_start;
    size_t from = search;
    while (from < source.length && !sequel_summary_word(source.data, source.length, from, "FROM"))
      ++from;
    if (from < source.length) {
      size_t table = sequel_summary_skip_space(source.data, source.length, from + 4), end = table;
      while (end < source.length && !isspace((unsigned char)source.data[end]) && source.data[end] != ';')
        ++end;
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
          !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, source.data + table, end - table))
        return -1;
      size_t using = end;
      while (using < source.length && !sequel_summary_word(source.data, source.length, using, "USING") && !sequel_summary_word(source.data, source.length, using, "WHERE"))
        ++using;
      if (using < source.length && sequel_summary_word(source.data, source.length, using, "USING")) {
        size_t name = sequel_summary_skip_space(source.data, source.length, using + 5), name_end = name;
        while (name_end < source.length && !isspace((unsigned char)source.data[name_end]))
          ++name_end;
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " USING ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, source.data + name, name_end - name))
          return -1;
      }
    }
  }
  if (settings->include_predicates && sequel_query_predicate_count(query) &&
      !(statement_count && sequel_summary_word(source.data, source.length, 0, "UPDATE") && sequel_summary_find_text(source.data, " SET ") && sequel_summary_find_text(source.data, " FROM ")) &&
      !sequel_summary_find_text(source.data, "JOIN LATERAL") && !sequel_summary_find_text(source.data, " JOIN (")) {
    const sequel_predicate_info_t *predicates = sequel_query_predicates(query);
    size_t predicate_index = 0;
    while (predicate_index < sequel_query_predicate_count(query) && predicates[predicate_index].range.start < outer_start)
      ++predicate_index;
    if (predicate_index == sequel_query_predicate_count(query))
      goto skip_outer_predicates;
    const sequel_predicate_info_t *predicate = &predicates[predicate_index];
    const char *label = " WHERE ";
    bool has_prewhere = false;
    bool has_final = false;
    sequel_source_view_t source = sequel_query_source(query);
    size_t start = predicate[0].range.start;
    size_t lower = start > 32 ? start - 32 : 0;
    for (size_t i = lower; i + 8 <= start; ++i)
      if (!memcmp(source.data + i, "PREWHERE", 8))
        has_prewhere = true;
    for (size_t i = lower; i + 5 <= start; ++i)
      if (!memcmp(source.data + i, "FINAL", 5))
        has_final = true;
    if (has_final)
      label = has_prewhere ? " FINAL PREWHERE " : " FINAL WHERE ";
    else if (has_prewhere)
      label = " PREWHERE ";
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, label) ||
        !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))
      return -1;
    const char *exists = sequel_summary_find_text(source.data, "EXISTS (");
    if (exists) {
      const char *subquery = sequel_summary_find_text(exists, "SELECT ");
      const char *from = subquery ? sequel_summary_find_text(subquery, " FROM ") : NULL;
      const char *where = from ? sequel_summary_find_text(from, " WHERE ") : NULL;
      if (subquery && from && where) {
        const char *table = from + 6;
        while (*table && isspace((unsigned char)*table))
          ++table;
        const char *table_end = table;
        while (*table_end && !isspace((unsigned char)*table_end))
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
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " EXISTS (SELECT ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(table_end - table)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, alias, (size_t)(alias_end - alias)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, ")"))
          return -1;
      }
    }
    const char *nested_exists = sequel_summary_find_text(source.data, "NOT EXISTS (SELECT");
    if (nested_exists) {
      const char *from = sequel_summary_find_text(nested_exists, " FROM ");
      const char *where = from ? sequel_summary_find_text(from, " WHERE ") : NULL;
      if (from && where) {
        const char *table = from + 6;
        while (*table && isspace((unsigned char)*table))
          ++table;
        const char *table_end = table;
        while (*table_end && !isspace((unsigned char)*table_end))
          ++table_end;
        const char *alias = table_end;
        while (*alias && isspace((unsigned char)*alias))
          ++alias;
        const char *alias_end = alias;
        while (*alias_end && !isspace((unsigned char)*alias_end) && *alias_end != ')')
          ++alias_end;
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " NOT EXISTS (SELECT ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(table_end - table)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, alias, (size_t)(alias_end - alias)) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
            !sequel_summary_append_text(&state->text, &state->length, &state->capacity, ")"))
          return -1;
      }
    }
  } else if (settings->include_predicates && statement_count && statements[0].operation != SEQUEL_OPERATION_CREATE &&
             !(sequel_summary_word(source.data, source.length, 0, "UPDATE") && sequel_summary_find_text(source.data, " SET ") && sequel_summary_find_text(source.data, " FROM ")) &&
             !sequel_summary_find_text(source.data, "JOIN LATERAL") && !sequel_summary_find_text(source.data, " JOIN (")) {
  skip_outer_predicates:
    for (size_t i = outer_start; i < source.length; ++i)
      if (sequel_summary_word(source.data, source.length, i, "WHERE")) {
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
            !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))
          return -1;
        break;
      }
  }
  if (settings->include_tables && statement_count && sequel_query_outer_limit(query) && !sequel_summary_find_text(state->text ? state->text : "", "LATERAL (") &&
      !sequel_summary_find_text(source.data, " JOIN (") && !sequel_summary_find_text(source.data, " LIMIT (SELECT ")) {
    const char *limit_marker = NULL;
    for (size_t i = outer_start; i < source.length; ++i)
      if (sequel_summary_word(source.data, source.length, i, "LIMIT"))
        limit_marker = source.data + i;
    if (limit_marker) {
      size_t i = (size_t)(limit_marker - source.data);
      size_t start = i + 5;
      while (start < source.length && isspace((unsigned char)source.data[start]))
        ++start;
      size_t end = start;
      while (end < source.length && source.data[end] != ';')
        ++end;
      while (end > start && isspace((unsigned char)source.data[end - 1]))
        --end;
      if (end > start) {
        bool limit_has_parameter = false;
        const sequel_parameter_info_t *parameters = sequel_query_parameters(query);
        size_t parameter_count = sequel_query_parameter_count(query);
        for (size_t parameter = 0; parameter < parameter_count; ++parameter)
          if (parameters[parameter].range.start < end && parameters[parameter].range.end > start) {
            limit_has_parameter = true;
            break;
          }
        bool has_offset = sequel_summary_find_text(source.data + start, " OFFSET ") != NULL;
        bool preserve_limit = !limit_has_parameter && (memchr(source.data + start, ',', end - start) != NULL || (has_offset && sequel_query_dialect(query) != SEQUEL_MYSQL));
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " LIMIT ") ||
            (preserve_limit && !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, source.data + start, end - start)) ||
            (!preserve_limit && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, "?")))
          return -1;
      }
    }
  }
  if (settings->include_predicates && statement_count && statements[0].operation == SEQUEL_OPERATION_SELECT) {
    for (size_t i = outer_start; i < source.length; ++i) {
      if (sequel_summary_word(source.data, source.length, i, "GROUP") && sequel_summary_word(source.data, source.length, sequel_summary_skip_space(source.data, source.length, i + 5), "BY")) {
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " GROUP BY"))
          return -1;
        break;
      }
    }
  }
  if (settings->include_tables && statement_count && sequel_summary_find_text(source.data, " SAMPLE ") && !sequel_summary_find_text(state->text ? state->text : "", " SAMPLE")) {
    if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " SAMPLE"))
      return -1;
  }
  if (settings->include_predicates && state->text) {
    const sequel_relation_info_t *relations = sequel_query_relations(query);
    for (size_t i = 0; i < sequel_query_relation_count(query); ++i) {
      if (relations[i].name_range.end <= relations[i].name_range.start)
        continue;
      const char *relation_start = source.data + relations[i].range.start;
      const char *global_in = sequel_summary_find_text(source.data, " GLOBAL IN ");
      const char *in = global_in;
      if (!in || relation_start <= in)
        continue;
      const char *name = source.data + relations[i].name_range.start;
      size_t name_length = relations[i].name_range.end - relations[i].name_range.start;
      if (sequel_summary_find_text(state->text, name))
        continue;
      if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") || !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, name, name_length) ||
          !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE"))
        return -1;
      break;
    }
  }
  if (settings->include_tables && !sequel_query_cte_count(query) && statement_count && statements[0].operation == SEQUEL_OPERATION_SELECT) {
    for (size_t i = outer_start; i < source.length; ++i) {
      if (sequel_summary_word(source.data, source.length, i, "ORDER") && sequel_summary_word(source.data, source.length, sequel_summary_skip_space(source.data, source.length, i + 5), "BY")) {
        if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ORDER BY …"))
          return -1;
        break;
      }
    }
  }

  return 0;
}
