#include "internal.h"

void sequel_analysis_emit_token(analysis_token_t token, analysis_token_t *tokens, size_t matching, analysis_state *state,
                                analysis_text *safe, analysis_map *map, bool literal, bool bind) {
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
    sequel_analysis_segment(map, start, end, before, safe->n,
                            literal || bind ? SEQUEL_MAP_REPLACE : SEQUEL_MAP_COPY);
}

void sequel_analysis_hash_literal(sequel_sha256 *hash, size_t *fingerprint_number) {
  unsigned char kind = 'B';
  sha_update(hash, &kind, 1);
  sha_u64(hash, ++*fingerprint_number);
  sha_frame(hash, 'Y', "", 0, false);
}

void sequel_analysis_hash_token(sequel_sha256 *hash, analysis_token_t token, sequel_dialect_t dialect) {
  sha_frame(hash,
            token.kind == 1   ? 'W'
            : token.kind == 2 ? 'Q'
                              : 'T',
            token.t.p, token.t.n,
            token.kind == 1 &&
                (token.t.keyword || (dialect != SEQUEL_MYSQL && dialect != SEQUEL_CLICKHOUSE) ||
                 isupper((unsigned char)token.t.p[0])));
}
