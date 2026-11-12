#include "internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

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

int sequel_summary_restore_numeric_limit_shape(char **text, size_t *length, size_t *capacity,
                                               sequel_source_view_t source) {
  if (!text || !*text || !length || !capacity || !source.data)
    return 1;

  const char *source_limit = sequel_summary_find_text(source.data, "LIMIT ");
  if (!source_limit)
    return 1;
  const char *source_value = source_limit + strlen("LIMIT ");
  while (isspace((unsigned char)*source_value))
    ++source_value;
  const char *source_value_end = source_value;
  while (isdigit((unsigned char)*source_value_end))
    ++source_value_end;
  if (source_value_end == source_value)
    return 1;

  bool comma_limit = *source_value_end == ',';
  bool offset_limit = sequel_summary_find_text(source_value_end, " OFFSET ") != NULL;
  if (!comma_limit && !offset_limit)
    return 1;

  char *rendered_limit = sequel_summary_find_text(*text, "LIMIT ");
  if (!rendered_limit)
    return 1;
  char *rendered_value = rendered_limit + strlen("LIMIT ");
  while (isdigit((unsigned char)*rendered_value))
    ++rendered_value;
  if (rendered_value == rendered_limit + strlen("LIMIT "))
    return 1;

  size_t source_digits = (size_t)(source_value_end - source_value);
  size_t rendered_digits = (size_t)(rendered_value - (rendered_limit + strlen("LIMIT ")));
  size_t suffix_length = comma_limit ? strlen(", ?") : 0;
  size_t replacement_length = source_digits + suffix_length;
  size_t old_length = rendered_digits;
  if (comma_limit && sequel_summary_find_text(rendered_value, ","))
    return 1;
  if (replacement_length > old_length && *capacity < *length + replacement_length - old_length + 1) {
    size_t needed = *length + replacement_length - old_length + 1;
    char *grown = realloc(*text, needed);
    if (!grown)
      return 0;
    *text = grown;
    *capacity = needed;
    rendered_limit = sequel_summary_find_text(*text, "LIMIT ");
    rendered_value = rendered_limit + strlen("LIMIT ") + rendered_digits;
  }
  if (replacement_length != old_length)
    memmove(rendered_limit + strlen("LIMIT ") + replacement_length, rendered_value,
            *length - (size_t)(rendered_value - *text) + 1);
  memcpy(rendered_limit + strlen("LIMIT "), source_value, source_digits);
  if (comma_limit)
    memcpy(rendered_limit + strlen("LIMIT ") + source_digits, ", ?", suffix_length);
  *length += replacement_length - old_length;
  return 1;
}
