#include "internal.h"

void sequel_analysis_emit_gap(analysis_text *safe, analysis_map *map, size_t source_offset) {
  if (!safe->n)
    return;
  size_t before = safe->n;
  sequel_analysis_append_text(safe, " ", 1);
  if (map)
    sequel_analysis_segment(map, source_offset, source_offset, before, safe->n, SEQUEL_MAP_INSERT);
}

bool sequel_analysis_consume_trivia(analysis_token_t token, bool suppressed, size_t *consumed, analysis_text *safe, analysis_map *map, bool *gap) {
  size_t start = token.source_offset;
  size_t end = start + token.t.n;
  if (!suppressed && start > *consumed) {
    if (map)
      sequel_analysis_segment(map, *consumed, start, safe->n, safe->n, SEQUEL_MAP_DELETE);
    *gap = true;
  }
  *consumed = end;
  if (token.kind < 3)
    return false;
  if (map && !suppressed)
    sequel_analysis_segment(map, start, end, safe->n, safe->n, SEQUEL_MAP_DELETE);
  if (!suppressed)
    *gap = true;
  return true;
}

bool sequel_analysis_update_collapse(size_t index, size_t *collapse_end, size_t *consumed, size_t start, bool *gap) {
  if (*collapse_end == SIZE_MAX || index != *collapse_end)
    return false;
  *consumed = start;
  *collapse_end = SIZE_MAX;
  *gap = false;
  return true;
}

void sequel_analysis_emit_token(analysis_token_t token, analysis_token_t *tokens, size_t matching, analysis_state *state, analysis_text *safe, analysis_map *map, bool literal, bool bind) {
  size_t start = token.source_offset;
  size_t end = start + token.t.n;
  size_t before = safe->n;
  if (token.kind == 0 && sequel_runtime_symbol(token.t, '(') && matching > 0) {
    sequel_analysis_append_text(safe, "(?", 2);
    if (map) {
      sequel_analysis_segment(map, start, end, before, before + 1, SEQUEL_MAP_COPY);
      size_t close = tokens[matching].source_offset;
      sequel_analysis_segment(map, end, close, before + 1, before + 2, SEQUEL_MAP_REPLACE);
    }
    return;
  }
  size_t sqlite_suffix = sequel_analysis_sqlite_suffix(token, state->options.dialect, bind);
  if (sqlite_suffix != SIZE_MAX) {
    sequel_analysis_append_text(safe, "?", 1);
    sequel_analysis_append_text(safe, token.t.p + sqlite_suffix, token.t.n - sqlite_suffix);
  } else {
    bool replace = literal || bind;
    sequel_analysis_append_text(safe, replace ? "?" : token.t.p, replace ? 1 : token.t.n);
  }
  if (map)
    sequel_analysis_segment(map, start, end, before, safe->n, literal || bind ? SEQUEL_MAP_REPLACE : SEQUEL_MAP_COPY);
}
