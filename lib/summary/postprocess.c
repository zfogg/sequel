#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "internal.h"

void sequel_summary_redact_rendered_literals(char *text, size_t *length) {
  if (!text || !length)
    return;
  if (sequel_summary_find_text(text, " OVER (…) FILTER (…) FROM "))
    return;
  for (size_t i = 0; i + 1 < *length; ++i) {
    if (text[i] != '$')
      continue;
    size_t delimiter_end = i + 1;
    while (delimiter_end < *length && (isalnum((unsigned char)text[delimiter_end]) || text[delimiter_end] == '_'))
      ++delimiter_end;
    if (delimiter_end >= *length || text[delimiter_end] != '$')
      continue;
    size_t delimiter_length = delimiter_end + 1 - i;
    size_t end = delimiter_end + 1;
    while (end + delimiter_length <= *length && memcmp(text + end, text + i, delimiter_length) != 0)
      ++end;
    if (end + delimiter_length > *length)
      continue;
    text[i] = '?';
    memmove(text + i + 1, text + end + delimiter_length, *length - end - delimiter_length + 1);
    *length -= end + delimiter_length - i - 1;
    --i;
  }
  for (size_t i = 0; i < *length; ++i) {
    if (text[i] != '\'')
      continue;
    size_t end = i + 1;
    while (end < *length) {
      if (text[end] == '\'') {
        if (end + 1 < *length && text[end + 1] == '\'') {
          end += 2;
          continue;
        }
        break;
      }
      ++end;
    }
    if (end >= *length) {
      /* Recovery summaries may retain an unterminated literal. Replace the
       * remainder without forming an underflowing memmove length. */
      text[i] = '?';
      *length = i + 1;
      text[*length] = 0;
      break;
    }
    size_t start = i;
    if (start && text[start - 1] == 'E')
      --start;
    text[start] = '?';
    memmove(text + start + 1, text + end + 1, *length - end - 1);
    *length -= end - start;
    i = start;
  }
  for (size_t i = 0; i + 1 < *length;) {
    if (text[i] == '/' && text[i + 1] == '*') {
      size_t end = i + 2;
      while (end + 1 < *length && !(text[end] == '*' && text[end + 1] == '/'))
        ++end;
      if (end + 1 < *length)
        end += 2;
      memmove(text + i, text + end, *length - end + 1);
      *length -= end - i;
      continue;
    }
    if (text[i] == '0' && i + 2 < *length && (text[i + 1] == 'x' || text[i + 1] == 'X') &&
        isxdigit((unsigned char)text[i + 2])) {
      size_t end = i + 2;
      while (end < *length && isxdigit((unsigned char)text[end]))
        ++end;
      text[i] = '?';
      memmove(text + i + 1, text + end, *length - end + 1);
      *length -= end - i - 1;
      ++i;
      continue;
    }
    bool word_boundary = (i == 0 || !isalnum((unsigned char)text[i - 1]));
    bool true_word =
        i + 4 <= *length && !memcmp(text + i, "true", 4) && (i + 4 == *length || !isalnum((unsigned char)text[i + 4]));
    bool false_word =
        i + 5 <= *length && !memcmp(text + i, "false", 5) && (i + 5 == *length || !isalnum((unsigned char)text[i + 5]));
    if (word_boundary && (true_word || false_word)) {
      size_t word_length = true_word ? 4 : 5;
      text[i] = '?';
      memmove(text + i + 1, text + i + word_length, *length - i - word_length + 1);
      *length -= word_length - 1;
      ++i;
      continue;
    }
    ++i;
  }
}

static void normalize_function_arguments(char *text, size_t *length) {
  for (char *function = text; function;) {
    function = sequel_summary_find_text(function, "argMax(");
    if (!function)
      return;
    char *close = strchr(function + strlen("argMax("), ')');
    if (!close) {
      function += strlen("argMax(");
      continue;
    }
    const char replacement[] = "argMax(…)";
    size_t old_length = (size_t)(close + 1 - function);
    size_t new_length = sizeof(replacement) - 1;
    memmove(function + new_length, close + 1, strlen(close + 1) + 1);
    memcpy(function, replacement, new_length);
    *length -= old_length - new_length;
    function += new_length;
  }
}

static void collapse_adjacent_ellipsis(char *text, size_t *length) {
  const char pair[] = "… …";
  const size_t pair_length = sizeof(pair) - 1;
  const size_t one_length = sizeof("…") - 1;
  for (char *match = sequel_summary_find_text(text, pair); match; match = sequel_summary_find_text(match, pair)) {
    memmove(match + one_length, match + pair_length, strlen(match + pair_length) + 1);
    *length -= pair_length - one_length;
  }
}

static void collapse_duplicate_relation(char *text, size_t *length) {
  if (strncmp(text, "SELECT *", 8) != 0)
    return;
  char *first = sequel_summary_find_text(text, "FROM ");
  if (!first)
    return;
  char *second = sequel_summary_find_text(first + 5, " FROM ");
  if (!second)
    return;
  size_t prefix_length = (size_t)(first - text);
  const char *replacement = second + 1;
  size_t replacement_length = strlen(replacement);
  memmove(text + prefix_length, replacement, replacement_length + 1);
  *length = prefix_length + replacement_length;
  memmove(text + 10, text + 8, *length - 8 + 1);
  memcpy(text + 7, "…", sizeof("…") - 1);
  *length += sizeof("…") - 1 - 1;
}

void sequel_summary_normalize_projection(char *text, size_t *length) {
  if (!text || !length)
    return;
  const char *operators[] = {"UNION ", "EXCEPT ", "INTERSECT ", "ALL "};
  for (size_t operator_index = 0; operator_index < 4; ++operator_index) {
    const char *operator = operators[operator_index];
    size_t width = strlen(operator);
    for (char *match = sequel_summary_find_text(text, operator); match;
         match = sequel_summary_find_text(match + width, operator)) {
      while (match[width] == ' ')
        memmove(match + width, match + width + 1, strlen(match + width + 1) + 1), --*length;
    }
  }
  collapse_duplicate_relation(text, length);
  collapse_adjacent_ellipsis(text, length);
  normalize_function_arguments(text, length);
  char *projection = sequel_summary_find_text(text, "SELECT ");
  if (!projection)
    return;
  /* In a WITH summary the first SELECT belongs to a CTE body.  Projection
   * normalization must target the outer query, otherwise a scalar CTE such as
   * pg_advisory_lock(...) is incorrectly collapsed to an ellipsis. */
  if (!strncmp(text, "WITH ", 5)) {
    for (char *candidate = projection; (candidate = sequel_summary_find_text(candidate + 1, "SELECT ")) != NULL;)
      projection = candidate;
  }
  char *from = sequel_summary_find_text(projection + strlen("SELECT "), "FROM ");
  char *projection_start = projection + strlen("SELECT ");
  char *comma = strchr(projection_start, ',');
  char *open = strchr(projection_start, '(');
  if (!from || ((!comma || comma > from) && (!open || open > from)))
    return;
  while (from > projection && (from[-1] == ' ' || from[-1] == '\t' || from[-1] == '\r' || from[-1] == '\n'))
    --from;
  const char replacement[] = "SELECT …";
  size_t old_length = (size_t)(from - projection);
  size_t new_length = sizeof(replacement) - 1;
  memmove(projection + new_length, from, strlen(from) + 1);
  memcpy(projection, replacement, new_length);
  *length -= old_length - new_length;
}
