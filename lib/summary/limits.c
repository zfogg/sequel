#include "internal.h"

void sequel_summary_restore_clickhouse_limit_by(char *text, size_t length, sequel_source_view_t source) {
  if (!text || !source.data)
    return;

  const char *source_limit = sequel_summary_find_text(source.data, "LIMIT ");
  const char *source_by = source_limit ? sequel_summary_find_text(source_limit, " BY ") : NULL;
  char *rendered_limit = sequel_summary_find_text(text, "LIMIT ?");
  if (!source_limit || !source_by || !rendered_limit || source_by != source_limit + 7 || source_limit[6] < '0' ||
      source_limit[6] > '9')
    return;

  if ((size_t)(rendered_limit - text) + 7 <= length)
    rendered_limit[6] = source_limit[6];

  rendered_limit = sequel_summary_find_text(rendered_limit + 7, "LIMIT ?");
  if (rendered_limit && (size_t)(rendered_limit - text) + 7 <= length)
    rendered_limit[6] = source_limit[6];
}
