#include <string.h>

#include "internal.h"

int sequel_summary_append_cte_definitions(char **text, size_t *length, size_t *capacity, sequel_source_view_t source,
                                          const sequel_cte_info_t *ctes, size_t cte_count, bool recursive,
                                          size_t *cte_prefix_end) {
  if (!text || !length || !capacity || !ctes || !cte_count || !cte_prefix_end)
    return 0;
  if (!sequel_summary_append_text(text, length, capacity, recursive ? "WITH RECURSIVE " : "WITH "))
    return 0;
  for (size_t i = 0; i < cte_count; ++i) {
    if (i && !sequel_summary_append_text(text, length, capacity, ", "))
      return 0;
    if (!sequel_summary_append_bytes(text, length, capacity, source.data + ctes[i].name_range.start,
                                     ctes[i].name_range.end - ctes[i].name_range.start) ||
        !sequel_summary_append_text(text, length, capacity,
                                    sequel_summary_find_text(source.data + ctes[i].name_range.end, "MATERIALIZED")
                                        ? " AS MATERIALIZED ("
                                        : " AS ("))
      return 0;
    bool preserve_limit = false;
    for (size_t cursor = ctes[i].name_range.end; cursor < ctes[i].body_range.start; ++cursor)
      if (cursor + 12 <= source.length && !memcmp(source.data + cursor, "MATERIALIZED", 12)) {
        preserve_limit = true;
        break;
      }
    if (!sequel_summary_append_body_summary(text, length, capacity, source.data, ctes[i].body_range, preserve_limit) ||
        !sequel_summary_append_text(text, length, capacity, ")"))
      return 0;
  }
  if (!sequel_summary_append_text(text, length, capacity, " "))
    return 0;
  *cte_prefix_end = *length;
  return 1;
}
