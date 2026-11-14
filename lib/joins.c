#include <string.h>

#include <sequel/query.h>

#include "model.h"
#include "tokens.h"

sequel_join_kind_t sequel_query_join_kind(const sequel_token_t *tokens, size_t token_count, size_t join_index, const sequel_source_view_t *source) {
  size_t previous[3] = {SIZE_MAX, SIZE_MAX, SIZE_MAX};
  size_t found = 0;
  for (size_t cursor = join_index; cursor-- > 0 && found < 3;)
    if (!sequel_token_is_trivia(&tokens[cursor]))
      previous[found++] = cursor;
  if (join_index + 1 < token_count) {
    size_t next = join_index + 1;
    while (next < token_count && sequel_token_is_trivia(&tokens[next]))
      ++next;
    if (next < token_count && sequel_query_word_equals_ci(source->data + tokens[next].range.start, tokens[next].range.end - tokens[next].range.start, "LATERAL"))
      return SEQUEL_JOIN_LATERAL;
  }
  if (found && sequel_query_word_equals_ci(source->data + tokens[previous[0]].range.start, tokens[previous[0]].range.end - tokens[previous[0]].range.start, "CROSS"))
    return SEQUEL_JOIN_CROSS;
  if (found && sequel_query_word_equals_ci(source->data + tokens[previous[0]].range.start, tokens[previous[0]].range.end - tokens[previous[0]].range.start, "NATURAL"))
    return SEQUEL_JOIN_NATURAL;
  size_t modifier = found > 1 && sequel_query_word_equals_ci(source->data + tokens[previous[0]].range.start, tokens[previous[0]].range.end - tokens[previous[0]].range.start, "OUTER") ? 1 : 0;
  const char *kinds[] = {"LEFT", "RIGHT", "FULL"};
  sequel_join_kind_t values[] = {SEQUEL_JOIN_LEFT, SEQUEL_JOIN_RIGHT, SEQUEL_JOIN_FULL};
  for (size_t i = 0; i < 3; ++i)
    if (found > modifier && sequel_query_word_equals_ci(source->data + tokens[previous[modifier]].range.start, tokens[previous[modifier]].range.end - tokens[previous[modifier]].range.start, kinds[i]))
      return values[i];
  return SEQUEL_JOIN_INNER;
}

uint32_t sequel_query_join_flags(const sequel_token_t *tokens, size_t token_count, size_t join_index, const sequel_source_view_t *source) {
  uint32_t flags = 0;
  size_t begin = join_index > 6 ? join_index - 6 : 0;
  const char *names[] = {"OUTER", "NATURAL", "ASOF", "GLOBAL", "ANY", "SEMI", "ANTI"};
  uint32_t values[] = {SEQUEL_JOIN_FLAG_OUTER, SEQUEL_JOIN_FLAG_NATURAL, SEQUEL_JOIN_FLAG_ASOF, SEQUEL_JOIN_FLAG_GLOBAL, SEQUEL_JOIN_FLAG_ANY, SEQUEL_JOIN_FLAG_SEMI, SEQUEL_JOIN_FLAG_ANTI};
  for (size_t i = begin; i < join_index; ++i)
    for (size_t name = 0; name < sizeof(names) / sizeof(*names); ++name)
      if (sequel_query_word_equals_ci(source->data + tokens[i].range.start, tokens[i].range.end - tokens[i].range.start, names[name]))
        flags |= values[name];
  size_t next = join_index + 1;
  while (next < token_count && sequel_token_is_trivia(&tokens[next]))
    ++next;
  if (next < token_count && sequel_query_word_equals_ci(source->data + tokens[next].range.start, tokens[next].range.end - tokens[next].range.start, "LATERAL"))
    flags |= SEQUEL_JOIN_FLAG_LATERAL;
  return flags;
}
