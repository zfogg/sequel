#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

int sequel_summary_render_cte_shape(sequel_summary_render_state_t *state, const sequel_query_document_t *query, const sequel_summary_options_t *settings, sequel_source_view_t source,
                                    size_t statement_count, const sequel_statement_info_t *statements, size_t outer_start) {
  if (settings->include_tables && sequel_query_cte_count(query) && statement_count && (statements[0].operation == SEQUEL_OPERATION_SELECT || statements[0].operation == SEQUEL_OPERATION_UNKNOWN)) {
    size_t outer = sequel_query_cte_count(query) ? sequel_query_outer_range(query).start : outer_start;
    while (outer < source.length && (isspace((unsigned char)source.data[outer]) || source.data[outer] == ')'))
      ++outer;
    const char *select = sequel_summary_find_text(source.data + outer, "SELECT ");
    const char *from = select ? sequel_summary_find_word(source.data, source.length, (size_t)(select - source.data), "FROM") : NULL;
    if (from) {
      const char *table = from + 4;
      while (*table && isspace((unsigned char)*table))
        ++table;
      const char *end = table;
      while (*end && !isspace((unsigned char)*end) && *end != ';' && *end != ')')
        ++end;
      const char *alias = end;
      while (*alias && isspace((unsigned char)*alias))
        ++alias;
      const char *alias_end = alias;
      while (*alias_end && !isspace((unsigned char)*alias_end) && *alias_end != ';' && *alias_end != ')')
        ++alias_end;
      if (sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "LEFT") || sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "JOIN") ||
          sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "WHERE") || sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "ORDER") ||
          sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "UNION") || sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "INTERSECT") ||
          sequel_summary_word(alias, source.length - (size_t)(alias - source.data), 0, "EXCEPT"))
        alias = alias_end = end;
      if ((!sequel_query_outer_distinct_on(query) && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, "SELECT ") ||
                                                      !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))) ||
          !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
          !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(end - table)) ||
          (alias < alias_end && !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " AS ")) ||
          (alias < alias_end && !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, alias, (size_t)(alias_end - alias))))
        return -1;
      size_t first_outer_relation = SIZE_MAX;
      size_t relation_count = sequel_query_relation_count(query);
      const sequel_relation_info_t *relations = sequel_query_relations(query);
      for (size_t i = 0; i < relation_count; ++i) {
        if (relations[i].range.start >= outer) {
          first_outer_relation = i;
          break;
        }
      }
      if (first_outer_relation != SIZE_MAX) {
        size_t previous_outer_end = relations[first_outer_relation].range.end;
        for (size_t i = first_outer_relation + 1; i < relation_count; ++i) {
          bool lateral_subquery = relations[i].name_range.end - relations[i].name_range.start == 7 && !memcmp(source.data + relations[i].name_range.start, "LATERAL", 7);
          if ((relations[i].flags & SEQUEL_RELATION_FLAG_NESTED_SCOPE) && !lateral_subquery)
            continue;
          if (relations[i].kind == SEQUEL_RELATION_SUBQUERY && lateral_subquery) {
            const char *subquery = source.data + relations[i].range.start;
            const char *from =
                sequel_summary_word(subquery, source.length - relations[i].range.start, 0, "FROM") ? subquery : sequel_summary_find_word(subquery, source.length - relations[i].range.start, 0, "FROM");
            const char *where = from ? sequel_summary_find_word(from, source.length - (size_t)(from - source.data), 0, "WHERE") : NULL;
            const char *order = from ? sequel_summary_find_word(from, source.length - (size_t)(from - source.data), 0, "ORDER") : NULL;
            const char *alias = relations[i].alias_range.end > relations[i].alias_range.start ? source.data + relations[i].alias_range.start : NULL;
            const char *alias_end = alias ? source.data + relations[i].alias_range.end : NULL;
            const char *table = from ? from + 4 : NULL;
            while (table && isspace((unsigned char)*table))
              ++table;
            const char *table_end = table;
            while (table_end && !isspace((unsigned char)*table_end) && *table_end != ')')
              ++table_end;
            if (!from || !table || !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " LEFT JOIN LATERAL (SELECT ") ||
                !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()) ||
                !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " FROM ") ||
                !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, table, (size_t)(table_end - table)) ||
                (where && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " WHERE ") ||
                           !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))) ||
                (order && (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ORDER BY ") ||
                           !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, sequel_summary_ellipsis(), sequel_summary_ellipsis_length()))) ||
                !sequel_summary_append_text(&state->text, &state->length, &state->capacity, sequel_summary_find_text(from, " LIMIT ") ? " LIMIT ?) AS " : ") AS ") ||
                (alias && !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, alias, (size_t)(alias_end - alias))) ||
                !sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON ?"))
              return -1;
            previous_outer_end = relations[i].range.end;
            continue;
          }
          const char *join_marker = previous_outer_end < relations[i].range.start ? sequel_summary_find_word(source.data, source.length, previous_outer_end, "JOIN") : NULL;
          if (!join_marker || (size_t)(join_marker - source.data) >= relations[i].range.start)
            continue;
          if (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " JOIN ") ||
              !sequel_summary_append_relation_name(&state->text, &state->length, &state->capacity, source, &relations[i]))
            return -1;
          const sequel_join_info_t *model_joins = sequel_query_joins(query);
          size_t model_join_index = SIZE_MAX;
          for (size_t candidate = 0; candidate < sequel_query_join_count(query); ++candidate)
            if (model_joins[candidate].right_relation == i) {
              model_join_index = candidate;
              break;
            }
          if (model_join_index != SIZE_MAX && model_joins[model_join_index].has_condition && model_joins[model_join_index].condition_range.end > model_joins[model_join_index].condition_range.start &&
              (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " ON ") ||
               !sequel_summary_append_redacted_range(&state->text, &state->length, &state->capacity, query, model_joins[model_join_index].condition_range.start,
                                                     model_joins[model_join_index].condition_range.end)))
            return -1;
          size_t condition_start = relations[i].alias_range.end > relations[i].range.end ? relations[i].alias_range.end : relations[i].range.end;
          while (condition_start < source.length && isspace((unsigned char)source.data[condition_start]))
            ++condition_start;
          const char *using = sequel_summary_find_word(source.data, source.length, condition_start, "USING");
          const char *open = using ? strchr(using, '(') : NULL;
          const char *close = open ? strchr(open, ')') : NULL;
          if (using && open && close &&
              (!sequel_summary_append_text(&state->text, &state->length, &state->capacity, " USING (") ||
               !sequel_summary_append_bytes(&state->text, &state->length, &state->capacity, open + 1, (size_t)(close - open - 1)) ||
               !sequel_summary_append_text(&state->text, &state->length, &state->capacity, ")")))
            return -1;
          previous_outer_end = relations[i].range.end;
        }
      }
    }
  }

  return 0;
}
