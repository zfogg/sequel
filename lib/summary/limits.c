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
                                               sequel_source_view_t source, sequel_source_range_t clause_range) {
  if (!text || !*text || !length || !capacity || !source.data)
    return 1;

  const char *source_limit = (clause_range.end > clause_range.start && clause_range.end <= source.length)
                                 ? source.data + clause_range.start
                                 : sequel_summary_find_text(source.data, "LIMIT ");
  if (!source_limit)
    return 1;
  const char *source_value = source_limit + strlen("LIMIT ");
  while (isspace((unsigned char)*source_value))
    ++source_value;
  const char *source_value_end = source_value;
  while (isdigit((unsigned char)*source_value_end))
    ++source_value_end;
  bool source_all = !strncmp(source_value, "ALL", 3);
  if (source_value_end == source_value && !source_all)
    return 1;
  if (source_all)
    source_value_end = source_value + 3;

  const char *delimiter = source_value_end;
  while (isspace((unsigned char)*delimiter))
    ++delimiter;
  bool comma_limit = *delimiter == ',';
  bool offset_limit = sequel_summary_find_text(delimiter, "OFFSET ") != NULL;
  if (!comma_limit && !offset_limit) {
    if (source_all) {
      char *rendered_limit = NULL;
      for (char *cursor = *text; (cursor = sequel_summary_find_text(cursor, "LIMIT ")) != NULL; ++cursor)
        rendered_limit = cursor;
      if (!rendered_limit)
        return 1;
      char *rendered_value = rendered_limit + strlen("LIMIT ");
      if (*rendered_value != '?')
        return 1;
      size_t offset = (size_t)(rendered_value - *text);
      if (*capacity < *length + 2 + 1) {
        char *grown = realloc(*text, *length + 3);
        if (!grown)
          return 0;
        *text = grown;
        *capacity = *length + 3;
        rendered_value = *text + offset;
      }
      memmove(rendered_value + 3, rendered_value + 1, *length - offset);
      memcpy(rendered_value, "ALL", 3);
      *length += 2;
      return 1;
    }
    const char *operator = source_value_end;
    while (isspace((unsigned char)*operator))
      ++operator;
    if (*operator == '+' || *operator == '-' || *operator == '*' || *operator == '/') {
      char *rendered_limit = NULL;
      for (char *cursor = *text; (cursor = sequel_summary_find_text(cursor, "LIMIT ")) != NULL; ++cursor)
        rendered_limit = cursor;
      if (!rendered_limit)
        return 1;
      char *rendered_value = rendered_limit + strlen("LIMIT ");
      char *rendered_end = rendered_value;
      while (isdigit((unsigned char)*rendered_end))
        ++rendered_end;
      if (rendered_end > rendered_value) {
        *rendered_value = '?';
        memmove(rendered_value + 1, rendered_end, *length - (size_t)(rendered_end - *text) + 1);
        *length -= (size_t)(rendered_end - rendered_value) - 1;
      }
    }
    if (sequel_summary_find_text(source.data, " UNION ") && sequel_summary_find_text(source.data, "LIMIT $")) {
      char *parameter_limit = NULL;
      for (char *cursor = *text; (cursor = sequel_summary_find_text(cursor, "LIMIT 1")) != NULL; ++cursor)
        if (!isdigit((unsigned char)cursor[7]))
          parameter_limit = cursor;
      if (parameter_limit)
        parameter_limit[6] = '?';
    }
    return 1;
  }

  char *rendered_limit = sequel_summary_find_text(*text, "LIMIT ");
  if (!rendered_limit)
    return 1;
  char *rendered_value = rendered_limit + strlen("LIMIT ");
  char *rendered_value_end = rendered_value;
  while (isdigit((unsigned char)*rendered_value_end))
    ++rendered_value_end;
  if (rendered_value_end == rendered_value && *rendered_value_end != '?')
    return 1;

  size_t source_digits = (size_t)(source_value_end - source_value);
  size_t rendered_digits = (size_t)(rendered_value_end - rendered_value);
  if (rendered_digits == 0)
    rendered_digits = 1;
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
  size_t rendered_prefix = rendered_digits == 1 && *rendered_value == '?' ? 1 : 0;
  if (replacement_length != old_length)
    memmove(rendered_limit + strlen("LIMIT ") + replacement_length, rendered_value + rendered_prefix,
            *length - (size_t)(rendered_value - *text) - rendered_prefix + 1);
  memcpy(rendered_limit + strlen("LIMIT "), source_value, source_digits);
  if (comma_limit)
    memcpy(rendered_limit + strlen("LIMIT ") + source_digits, ", ?", suffix_length);
  *length += replacement_length - old_length;
  return 1;
}

int sequel_summary_restore_numeric_offset_shape(char **text, size_t *length, size_t *capacity,
                                                sequel_source_view_t source) {
  if (!text || !*text || !length || !capacity || !source.data)
    return 1;
  *length = strlen(*text);
  const char *source_offset = sequel_summary_find_text(source.data, "OFFSET ");
  if (!source_offset)
    return 1;
  const char *source_limit = sequel_summary_find_text(source.data, "LIMIT ");
  const char *source_limit_value = source_limit ? source_limit + strlen("LIMIT ") : NULL;
  const char *source_limit_end = source_limit_value;
  while (source_limit_end && isdigit((unsigned char)*source_limit_end))
    ++source_limit_end;
  char *rendered_limit = sequel_summary_find_text(*text, "LIMIT ");
  if (source_limit_value && source_limit_end > source_limit_value && rendered_limit) {
    char *rendered_value = rendered_limit + strlen("LIMIT ");
    char *rendered_end = rendered_value;
    while (isdigit((unsigned char)*rendered_end))
      ++rendered_end;
    if (rendered_end == rendered_value && *rendered_value == '?')
      rendered_end++;
    if (rendered_end > rendered_value) {
      size_t prefix = (size_t)(rendered_value - *text);
      size_t suffix_offset = (size_t)(rendered_end - *text);
      size_t suffix = *length - suffix_offset;
      size_t digits = (size_t)(source_limit_end - source_limit_value);
      size_t result_length = prefix + digits + suffix;
      char *result = malloc(result_length + 1);
      if (!result)
        return 0;
      memcpy(result, *text, prefix);
      memcpy(result + prefix, source_limit_value, digits);
      memcpy(result + prefix + digits, *text + suffix_offset, suffix);
      result[result_length] = '\0';
      free(*text);
      *text = result;
      *length = result_length;
      *capacity = result_length + 1;
    }
  }
  const char *source_value = source_offset + strlen("OFFSET ");
  const char *source_end = source_value;
  while (isdigit((unsigned char)*source_end))
    ++source_end;
  if (source_end == source_value)
    return 1;
  char *rendered_offset = sequel_summary_find_text(*text, "OFFSET ?");
  if (!rendered_offset)
    return 1;
  size_t source_digits = (size_t)(source_end - source_value);
  size_t prefix_length = (size_t)(rendered_offset - *text) + strlen("OFFSET ");
  size_t suffix_offset = prefix_length + 1;
  size_t suffix_length = *length - suffix_offset;
  size_t replacement_length = prefix_length + source_digits + suffix_length;
  char *replacement = malloc(replacement_length + 1);
  if (!replacement)
    return 0;
  memcpy(replacement, *text, prefix_length);
  memcpy(replacement + prefix_length, source_value, source_digits);
  memcpy(replacement + prefix_length + source_digits, *text + suffix_offset, suffix_length);
  replacement[replacement_length] = '\0';
  free(*text);
  *text = replacement;
  *length = replacement_length;
  *capacity = replacement_length + 1;
  return 1;
}

int sequel_summary_restore_scalar_limit_shape(char **text, size_t *length, size_t *capacity,
                                              sequel_source_view_t source) {
  if (!text || !*text || !length || !capacity || !source.data)
    return 1;
  const char *source_limit = sequel_summary_find_text(source.data, "LIMIT (SELECT ");
  if (!source_limit)
    return 1;
  const char *source_open = source_limit + strlen("LIMIT ");
  const char *source_close = source_open;
  size_t depth = 0;
  while ((size_t)(source_close - source.data) < source.length) {
    if (*source_close == '(')
      ++depth;
    else if (*source_close == ')' && depth && --depth == 0)
      break;
    ++source_close;
  }
  if ((size_t)(source_close - source.data) >= source.length)
    return 1;
  const char *from = sequel_summary_find_text(source_open, " FROM ");
  const char *inner_limit = sequel_summary_find_text(source_open, " LIMIT ");
  if (!from || !inner_limit || from >= source_close || inner_limit >= source_close)
    return 1;
  const char *table = from + strlen(" FROM ");
  const char *table_end = table;
  while (table_end < source_close && !isspace((unsigned char)*table_end) && *table_end != ')')
    ++table_end;
  const char *inner_value = inner_limit + strlen(" LIMIT ");
  const char *inner_value_end = inner_value;
  while (inner_value_end < source_close && isdigit((unsigned char)*inner_value_end))
    ++inner_value_end;
  if (inner_value == inner_value_end)
    return 1;

  char *rendered_limit = sequel_summary_find_text(*text, " LIMIT ");
  char *rendered_inner_limit = rendered_limit ? sequel_summary_find_text(rendered_limit + 7, " LIMIT ?") : NULL;
  if (!rendered_limit || !rendered_inner_limit)
    return 1;
  const char *tail = rendered_inner_limit + strlen(" LIMIT ?");
  size_t prefix_length = (size_t)(rendered_limit - *text);
  size_t tail_length = *length - (size_t)(tail - *text);
  const char prefix[] = " LIMIT ? (SELECT \342\200\246 FROM ";
  const char separator[] = " LIMIT ";
  size_t replacement_length = sizeof(prefix) - 1 + (size_t)(table_end - table) + sizeof(separator) - 1 +
                              (size_t)(inner_value_end - inner_value) + 1;
  size_t result_length = prefix_length + replacement_length + tail_length;
  char *result = malloc(result_length + 1);
  if (!result)
    return 0;
  memcpy(result, *text, prefix_length);
  size_t offset = prefix_length;
  memcpy(result + offset, prefix, sizeof(prefix) - 1);
  offset += sizeof(prefix) - 1;
  memcpy(result + offset, table, (size_t)(table_end - table));
  offset += (size_t)(table_end - table);
  memcpy(result + offset, separator, sizeof(separator) - 1);
  offset += sizeof(separator) - 1;
  memcpy(result + offset, inner_value, (size_t)(inner_value_end - inner_value));
  offset += (size_t)(inner_value_end - inner_value);
  result[offset++] = ')';
  memcpy(result + offset, tail, tail_length);
  result[result_length] = '\0';
  free(*text);
  *text = result;
  *length = result_length;
  *capacity = result_length + 1;
  return 1;
}
