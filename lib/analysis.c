/* Unified analysis uses the existing dialect lexer. It hashes framed token
 * classes and produces numeric provenance while values are replaced. */
#include "analysis_internal.h"

static void utf8_cut(char *text, size_t limit) {
  size_t length = strlen(text);
  if (length <= limit)
    return;
  limit = sequel_utf8_prefix(text, length, limit);
  text[limit] = 0;
  while (limit && isspace((unsigned char)text[limit - 1]))
    text[--limit] = 0;
}

static int shared_token_kind(sequel_token_kind_t kind) {
  switch (kind) {
  case SEQUEL_TOKEN_LINE_COMMENT:
    return 3;
  case SEQUEL_TOKEN_BLOCK_COMMENT:
    return 4;
  case SEQUEL_TOKEN_IDENTIFIER:
  case SEQUEL_TOKEN_KEYWORD:
  case SEQUEL_TOKEN_BOOLEAN:
  case SEQUEL_TOKEN_NULL:
    return 1;
  case SEQUEL_TOKEN_QUOTED_IDENTIFIER:
  case SEQUEL_TOKEN_STRING:
  case SEQUEL_TOKEN_PARAMETER:
    return 2;
  default:
    return 0;
  }
}

static sequel_status_t adapt_lexed_tokens(const sequel_lexed_document_t *lexed, format_token **out, size_t *count) {
  if (!lexed || !out || !count)
    return SEQUEL_ERR_NULL;
  *out = NULL;
  *count = 0;
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  sequel_source_view_t source = sequel_lexed_source(lexed);
  size_t capacity = 0;
  size_t previous_end = 0;
  bool gap = false;
  bool pending_newline = false;
  for (size_t i = 0; i < token_count; ++i) {
    const sequel_token_t *token = &tokens[i];
    if (token->kind == SEQUEL_TOKEN_EOF)
      break;
    if (token->range.end > source.length || token->range.start > token->range.end) {
      goto invalid;
    }
    if (token->kind == SEQUEL_TOKEN_WHITESPACE) {
      for (size_t offset = token->range.start; offset < token->range.end; ++offset)
        pending_newline |= source.data[offset] == '\r' || source.data[offset] == '\n';
      previous_end = token->range.end;
      gap = true;
      continue;
    }
    if (*count == capacity) {
      size_t next = capacity ? capacity * 2 : 64;
      if (next < *count || next > SIZE_MAX / sizeof(**out)) {
        goto overflow;
      }
      format_token *grown = calloc(next, sizeof(*grown));
      if (!grown)
        goto no_memory;
      if (*out) {
        memcpy(grown, *out, *count * sizeof(*grown));
        free(*out);
      }
      *out = grown;
      capacity = next;
    }
    bool newline = false;
    for (size_t offset = previous_end; offset < token->range.start; ++offset)
      newline |= source.data[offset] == '\r' || source.data[offset] == '\n';
    int kind = shared_token_kind(token->kind);
    bool opening = kind == 0 && token->range.end == token->range.start + 1 &&
                   strchr("([{", source.data[token->range.start]) != NULL;
    bool previous_open = *count && (*out)[*count - 1].kind == 0 && (*out)[*count - 1].t.n == 1 &&
                         strchr("([{", *(*out)[*count - 1].t.p) != NULL;
    (*out)[(*count)++] = (format_token){.t = {.p = source.data + token->range.start,
                                              .n = token->range.end - token->range.start,
                                              .word = kind == 1,
                                              .space = gap,
                                              .clickhouse = sequel_lexed_dialect(lexed) == SEQUEL_CLICKHOUSE},
                                        .gap = gap,
                                        .newline = newline || pending_newline,
                                        .kind = kind,
                                        .matching = 0,
                                        .multiline = newline || (pending_newline && (!opening || previous_open)),
                                        .flat_width = (*count > 1 ? (*out)[*count - 2].flat_width : 0) +
                                                      token->range.end - token->range.start + 1,
                                        .source_offset = token->range.start,
                                        .rendered_offset = 0};
    previous_end = token->range.end;
    gap = false;
    pending_newline = false;
  }
  return SEQUEL_OK;
invalid:
  free(*out);
  *out = NULL;
  *count = 0;
  return SEQUEL_ERR_INVALID_ARGUMENT;
overflow:
  free(*out);
  *out = NULL;
  *count = 0;
  return SEQUEL_ERR_OVERFLOW;
no_memory:
  free(*out);
  *out = NULL;
  *count = 0;
  return SEQUEL_ERR_NOMEM;
}

/* Clause boundaries used by the shared layout pass. */
static bool boundary(token t) {
  const char *words[] = {"FROM",  "WHERE",     "GROUP",  "HAVING",    "ORDER", "LIMIT", "OFFSET",  "FETCH",  "FOR",
                         "UNION", "INTERSECT", "EXCEPT", "RETURNING", "SET",   "USING", "QUALIFY", "WINDOW", "EXCLUDE"};
  if (t.clickhouse && (sequel_runtime_keyword(t, "PREWHERE") || sequel_runtime_keyword(t, "SETTINGS") ||
                       sequel_runtime_keyword(t, "FORMAT") || sequel_runtime_keyword(t, "SAMPLE") ||
                       sequel_runtime_keyword(t, "VALUES")))
    return true;
  for (size_t i = 0; (sequel_work_tick(1), i < sizeof(words) / sizeof(*words)); ++i)
    if (sequel_runtime_keyword(t, words[i]))
      return true;
  return sequel_runtime_symbol(t, ';') || sequel_runtime_symbol(t, ')');
}

typedef struct {
  char *p;
  size_t n, capacity;
} analysis_text;

typedef struct {
  sequel_map_segment_t *p;
  size_t n, capacity;
} analysis_map;

typedef struct {
  const char *key;
  size_t length, slot, identity;
  uint64_t hash;
  bool occupied, owned, numbered;
} analysis_binding;

typedef struct {
  analysis_binding *p;
  size_t count, capacity, maximum, identities;
} analysis_bindings;

typedef struct {
  format_token *p;
  size_t n, capacity;
} analysis_layout;

typedef struct {
  sequel_analysis_t *out;
  sequel_analysis_options_t options;
} analysis_state;

static const char *analysis_dialect_name(sequel_dialect_t d) {
  static const char *names[] = {"generic", "sqlite", "postgresql", "duckdb", "mysql", "clickhouse"};
  return d >= SEQUEL_GENERIC && d <= SEQUEL_CLICKHOUSE ? names[d] : "invalid";
}

static void analysis_charge(analysis_state *a, size_t bytes) {
  (void)a;
  sequel_output_tick(bytes);
}

static void *analysis_array(void *pointer, size_t count, size_t element) {
  if (count > SIZE_MAX / element)
    sequel_abort(SEQUEL_ERR_LIMIT);
  size_t bytes = count * element,
         capacity = pointer ? ((sequel_allocation *)pointer - 1)->h.bytes - sizeof(sequel_allocation) : 0;
  if (bytes <= capacity)
    return pointer;
  size_t next = capacity ? capacity : element * 8;
  while (next < bytes) {
    if (next > SIZE_MAX / 2) {
      next = bytes;
      break;
    }
    next *= 2;
  }
  return realloc(pointer, next);
}

static void analysis_layout_add(analysis_layout *layout, size_t start, size_t length, int kind, bool gap,
                                sequel_dialect_t dialect) {
  layout->p = analysis_array(layout->p, layout->n + 1, sizeof(*layout->p));
  format_token t = {0};
  t.t.n = length;
  t.t.word = kind == 1;
  t.t.clickhouse = dialect == SEQUEL_CLICKHOUSE;
  t.kind = kind;
  t.gap = gap;
  t.source_offset = start;
  t.flat_width = (layout->n ? layout->p[layout->n - 1].flat_width : 0) + length + 1;
  layout->p[layout->n++] = t;
}

static void analysis_append(analysis_text *b, const char *p, size_t n) {
  sequel_work_tick(n);
  if (n > SIZE_MAX - b->n - 1)
    sequel_abort(SEQUEL_ERR_LIMIT);
  size_t need = b->n + n + 1;
  if (need > b->capacity) {
    size_t next = b->capacity ? b->capacity : 32;
    while (next < need) {
      if (next > SIZE_MAX / 2) {
        next = need;
        break;
      }
      next *= 2;
    }
    b->p = realloc(b->p, next);
    b->capacity = next;
  }
  memcpy(b->p + b->n, p, n);
  b->n += n;
  b->p[b->n] = 0;
}

static void analysis_segment(analysis_map *b, size_t fs, size_t fe, size_t ts, size_t te, sequel_map_kind_t kind) {
  if (fs == fe && ts == te)
    return;
  if (b->n) {
    sequel_map_segment_t *last = &b->p[b->n - 1];
    if (last->kind == kind && last->from_end == fs && last->to_end == ts && kind != SEQUEL_MAP_REPLACE) {
      last->from_end = fe;
      last->to_end = te;
      return;
    }
  }
  if (b->n == b->capacity) {
    size_t next = b->capacity ? b->capacity * 2 : 16;
    if (next < b->capacity || next > SIZE_MAX / sizeof(*b->p))
      sequel_abort(SEQUEL_ERR_LIMIT);
    b->p = realloc(b->p, next * sizeof(*b->p));
    b->capacity = next;
  }
  sequel_output_tick(sizeof(*b->p));
  b->p[b->n++] = (sequel_map_segment_t){fs, fe, ts, te, kind};
}

#include "analysis_literal.c"

#include "analysis_binding_token.c"

/* Return identity key and explicit numbered arity. Marker prefixes distinguish
 * SQLite binding namespaces; fingerprint frames exclude those spellings. */
static uint64_t analysis_key_hash(const char *p, size_t n) {
  sequel_sha256 s;
  sha_init(&s);
  sha_update(&s, p, n);
  char hex[65];
  sha_final(&s, hex);
  uint64_t v = 0;
  for (size_t i = 0; i < 16; i++) {
    sequel_work_tick(1);
    v = (v << 4) | (uint64_t)(hex[i] <= '9' ? hex[i] - '0' : hex[i] - 'a' + 10);
  }
  return v;
}

static analysis_binding *analysis_binding_slot(analysis_bindings *b, const char *key, size_t length, size_t number) {
  if (!b->capacity || b->count >= b->capacity / 2) {
    size_t next = b->capacity ? b->capacity * 2 : 16;
    if (next < b->capacity || next > SIZE_MAX / sizeof(*b->p))
      sequel_abort(SEQUEL_ERR_LIMIT);
    analysis_binding *grown = calloc(next, sizeof(*grown));
    for (size_t i = 0; i < b->capacity; i++) {
      sequel_work_tick(1);
      if (b->p[i].occupied) {
        size_t at = (size_t)b->p[i].hash & (next - 1);
        while (grown[at].occupied)
          at = (at + 1) & (next - 1);
        grown[at] = b->p[i];
      }
    }
    free(b->p);
    b->p = grown;
    b->capacity = next;
  }
  uint64_t hash = length ? analysis_key_hash(key, length) : (uint64_t)(b->identities + 1);
  size_t at = (size_t)hash & (b->capacity - 1);
  while (b->p[at].occupied) {
    analysis_binding *v = &b->p[at];
    if (length && v->hash == hash && v->length == length && v->numbered == (number != 0)) {
      sequel_work_tick(length);
      if (!memcmp(v->key, key, length))
        return v;
    }
    at = (at + 1) & (b->capacity - 1);
  }
  if (b->maximum == SIZE_MAX || b->identities == SIZE_MAX)
    sequel_abort(SEQUEL_ERR_LIMIT);
  size_t slot = number ? number : b->maximum + 1;
  if (slot > b->maximum)
    b->maximum = slot;
  b->p[at] = (analysis_binding){key, length, slot, ++b->identities, hash, true, false, number != 0};
  ++b->count;
  return &b->p[at];
}

static analysis_binding *analysis_numeric_identity(analysis_bindings *b, size_t slot) {
  char encoded[32];
  snprintf(encoded, sizeof(encoded), "%zu", slot);
  size_t n = strlen(encoded);
  analysis_binding *v = analysis_binding_slot(b, encoded, n, slot);
  if (v->key == encoded) {
    char *owned = malloc(n);
    memcpy(owned, encoded, n);
    v->key = owned;
    v->owned = true;
  }
  return v;
}

static void analysis_dictionary_clear(analysis_bindings *b) {
  /* Numeric keys are tiny owned copies; named keys point into caller input. */
  for (size_t i = 0; i < b->capacity; i++) {
    sequel_work_tick(1);
    if (b->p[i].occupied && b->p[i].owned)
      free((void *)b->p[i].key);
  }
  free(b->p);
  *b = (analysis_bindings){0};
}

static void analysis_add_diagnostic(sequel_analysis_t *out, sequel_diagnostic_code_t code, size_t start, size_t end) {
  size_t count = out->diagnostic_count;
  sequel_output_tick(sizeof(sequel_diagnostic_t));
  sequel_diagnostic_t *ds = analysis_array((void *)out->diagnostics, count + 1, sizeof(*ds));
  diagnostic_report(NULL, 0, &ds[count], code, 0, 0);
  ds[count].offset = start;
  ds[count].end_offset = end;
  out->diagnostics = ds;
  out->diagnostic_count++;
}

static bool analysis_has_diagnostic(const sequel_analysis_t *out, sequel_diagnostic_code_t code, size_t start,
                                    size_t end) {
  for (size_t i = 0; i < out->diagnostic_count; ++i)
    if (out->diagnostics[i].code == code && out->diagnostics[i].offset == start &&
        out->diagnostics[i].end_offset == end)
      return true;
  return false;
}

static void analysis_sort_diagnostics(sequel_analysis_t *out) {
  sequel_diagnostic_t *diagnostics = (sequel_diagnostic_t *)out->diagnostics;
  for (size_t i = 1; i < out->diagnostic_count; ++i) {
    sequel_diagnostic_t value = diagnostics[i];
    size_t j = i;
    while (j && diagnostics[j - 1].offset > value.offset) {
      diagnostics[j] = diagnostics[j - 1];
      --j;
    }
    diagnostics[j] = value;
  }
}

static void analysis_add_statement(sequel_analysis_t *out, size_t start, size_t end) {
  size_t count = out->statement_count;
  sequel_output_tick(sizeof(sequel_range_t));
  sequel_range_t *rs = analysis_array((void *)out->statements, count + 1, sizeof(*rs));
  rs[count] = (sequel_range_t){start, end};
  out->statements = rs;
  out->statement_count++;
}

/* Recover statement spans only when syntax recovery prevents query construction. */
static void analysis_recovered_statement_ranges(const sequel_lexed_document_t *lexed, sequel_analysis_t *out) {
  size_t count = 0, start = SIZE_MAX, last = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &count);
  sequel_source_view_t source = sequel_lexed_source(lexed);
  for (size_t i = 0; i < count; ++i) {
    const sequel_token_t *token = &tokens[i];
    if (token->kind == SEQUEL_TOKEN_EOF)
      break;
    if (token->kind == SEQUEL_TOKEN_WHITESPACE || token->kind == SEQUEL_TOKEN_LINE_COMMENT ||
        token->kind == SEQUEL_TOKEN_BLOCK_COMMENT)
      continue;
    if (start == SIZE_MAX)
      start = token->range.start;
    last = token->range.end;
    if (token->kind == SEQUEL_TOKEN_PUNCTUATION && token->range.end == token->range.start + 1 &&
        source.data[token->range.start] == ';') {
      analysis_add_statement(out, start, last);
      start = SIZE_MAX;
    }
  }
  if (start != SIZE_MAX)
    analysis_add_statement(out, start, last);
}

static void analysis_positions(const char *sql, sequel_analysis_t *out) {
  if (!sql || !out)
    return;
  sequel_source_view_t source = {sql, out->original_length};
  for (size_t i = 0; i < out->diagnostic_count; ++i) {
    sequel_diagnostic_t *diagnostic = (sequel_diagnostic_t *)out->diagnostics + i;
    sequel_location_t location;
    if (sequel_source_range_position(&source, (sequel_source_range_t){diagnostic->offset, diagnostic->end_offset},
                                     &location) != SEQUEL_OK)
      continue;
    diagnostic->line = location.start.line;
    diagnostic->column = location.start.column;
    diagnostic->end_line = location.end.line;
    diagnostic->end_column = location.end.column;
  }
}

#include "analysis_text.c"

static bool analysis_word(format_token t, const char *word) {
  size_t n = strlen(word);
  if (t.kind != 1 || t.t.n != n)
    return false;
  for (size_t i = 0; i < n; ++i)
    if (toupper((unsigned char)t.t.p[i]) != toupper((unsigned char)word[i]))
      return false;
  return true;
}

#include "analysis_policy.c"

/* Whitespace is delayed across comments so deleted comment text never appears
 * in output. Every consumed source byte still has explicit map provenance. */
static void analysis_sanitize(const char *sql, size_t n, format_token *ts, size_t count, analysis_state *a,
                              analysis_text *safe, analysis_map *map, analysis_layout *layout, sequel_sha256 *hash) {
  sequel_analysis_t *out = a->out;
  analysis_bindings dictionary = {0}, slot_dictionary = {0};
  size_t consumed = 0, statement = 0, bind_count = 0;
  bool gap = false;
  size_t collapse_end = SIZE_MAX, fingerprint_next = 0;
  for (size_t i = 0; i < count; i++) {
    sequel_work_tick(1);
    format_token t = ts[i];
    size_t start = t.source_offset, end = start + t.t.n;
    if (collapse_end != SIZE_MAX && i == collapse_end) {
      consumed = start;
      collapse_end = SIZE_MAX;
      gap = false;
    }
    bool suppressed = collapse_end != SIZE_MAX && i < collapse_end;
    if (!suppressed && start > consumed) {
      if (map)
        analysis_segment(map, consumed, start, safe->n, safe->n, SEQUEL_MAP_DELETE);
      gap = true;
    }
    consumed = end;
    if (t.kind >= 3) {
      if (map && !suppressed)
        analysis_segment(map, start, end, safe->n, safe->n, SEQUEL_MAP_DELETE);
      if (!suppressed)
        gap = true;
      continue;
    }
    bool literal = analysis_literal(t, a->options.dialect);
    const char *key;
    size_t key_length, number;
    sequel_bind_style_t style;
    while (statement + 1 < out->statement_count && start >= out->statements[statement].end_offset)
      statement++;
    bool bind = !literal && analysis_bind_token(t, a->options.dialect, &key, &key_length, &number, &style);
    if (bind) {
      char numeric[32];
      if (number) {
        snprintf(numeric, sizeof(numeric), "%zu", number);
        key = numeric;
        key_length = strlen(numeric);
      }
      analysis_binding *identity = analysis_binding_slot(&dictionary, key, key_length, number);
      if (number && identity->key == numeric) {
        char *owned = malloc(key_length);
        memcpy(owned, numeric, key_length);
        identity->key = owned;
        identity->owned = true;
      }
      size_t old_count = slot_dictionary.count;
      analysis_binding *canonical = analysis_numeric_identity(&slot_dictionary, identity->slot);
      if (slot_dictionary.count != old_count)
        canonical->identity = ++fingerprint_next;
      unsigned char kind = 'B';
      sha_update(hash, &kind, 1);
      sha_u64(hash, canonical->identity);
      if (style == SEQUEL_BIND_TYPED) {
        sequel_sha256 type_hash;
        sha_init(&type_hash);
        const char *colon = memchr(t.t.p, ':', t.t.n);
        size_t type_length = (size_t)((t.t.p + t.t.n - 1) - (colon + 1));
        sequel_parse_options_t type_options;
        sequel_parse_options_init(&type_options);
        type_options.dialect = a->options.dialect;
        type_options.retain_comments = false;
        type_options.retain_whitespace = false;
        sequel_source_view_t type_source = {colon + 1, type_length};
        sequel_lexed_document_t *type_document = NULL;
        if (sequel_lex(&type_source, &type_options, NULL, &type_document) != SEQUEL_OK)
          sequel_abort(SEQUEL_ERR_INVALID_ARGUMENT);
        size_t type_count = 0;
        const sequel_token_t *type_tokens = sequel_lexed_tokens(type_document, &type_count);
        sequel_source_view_t type_view = sequel_lexed_source(type_document);
        for (size_t k = 0; k < type_count; k++) {
          sequel_work_tick(1);
          if (type_tokens[k].kind == SEQUEL_TOKEN_EOF)
            continue;
          const sequel_token_t *type_token = &type_tokens[k];
          bool value = (type_token->flags & SEQUEL_TOKEN_FLAG_LITERAL) != 0;
          char frame = value                                                                                     ? 'L'
                       : type_token->kind == SEQUEL_TOKEN_KEYWORD || type_token->kind == SEQUEL_TOKEN_IDENTIFIER ? 'W'
                                                                                                                 : 'T';
          sha_frame(&type_hash, frame, type_view.data + type_token->range.start,
                    type_token->range.end - type_token->range.start, false);
        }
        char type_digest[65];
        sha_final(&type_hash, type_digest);
        sha_frame(hash, 'Y', type_digest, 64, false);
        sequel_lexed_document_free(type_document);
      } else
        sha_frame(hash, 'Y', "", 0, false);
      if (!a->options.omit_bind_descriptors) {
        sequel_output_tick(sizeof(sequel_bind_descriptor_t));
        sequel_bind_descriptor_t *bs = analysis_array((void *)out->binds, out->bind_count + 1, sizeof(*bs));
        bs[out->bind_count] = (sequel_bind_descriptor_t){++bind_count, identity->slot, statement, start, end, style};
        out->binds = bs;
        out->bind_count++;
      }
    } else if (literal) {
      unsigned char kind = 'B';
      sha_update(hash, &kind, 1);
      sha_u64(hash, ++fingerprint_next);
      sha_frame(hash, 'Y', "", 0, false);
    } else
      sha_frame(hash,
                t.kind == 1   ? 'W'
                : t.kind == 2 ? 'Q'
                              : 'T',
                t.t.p, t.t.n,
                t.kind == 1 && ((a->options.dialect != SEQUEL_MYSQL && a->options.dialect != SEQUEL_CLICKHOUSE) ||
                                isupper((unsigned char)t.t.p[0])));
    if (t.kind == 0 && sequel_runtime_symbol(t.t, ';')) {
      fingerprint_next = 0;
      analysis_dictionary_clear(&dictionary);
      analysis_dictionary_clear(&slot_dictionary);
    }
    while (statement + 1 < out->statement_count && start >= out->statements[statement].end_offset)
      statement++;
    if (suppressed) {
      gap = false;
      continue;
    }

    if (!a->options.keep_in_lists && t.kind == 1 && sequel_runtime_keyword(t.t, "IN")) {
      size_t open = i + 1;
      while (open < count && ts[open].kind >= 3)
        open++;
      if (open < count && sequel_runtime_symbol(ts[open].t, '(')) {
        size_t k = open + 1, items = 0;
        bool expecting = true, valid = true;
        for (; k < count; k++) {
          sequel_work_tick(1);
          if (ts[k].kind >= 3)
            continue;
          if (sequel_runtime_symbol(ts[k].t, ')'))
            break;
          if (expecting) {
            const char *bk;
            size_t bl, bn;
            sequel_bind_style_t st;
            bool value = analysis_literal(ts[k], a->options.dialect) ||
                         analysis_bind_token(ts[k], a->options.dialect, &bk, &bl, &bn, &st);
            if (!value) {
              valid = false;
              break;
            }
            items++;
            expecting = false;
          } else if (sequel_runtime_symbol(ts[k].t, ','))
            expecting = true;
          else {
            valid = false;
            break;
          }
        }
        /* Mark collapse when the opening token is visited. */
        if (valid && !expecting && items > 1 && k < count)
          ts[open].matching = k;
      }
    }
    size_t emitted_before = safe->n;
    if (gap && safe->n) {
      size_t at = safe->n;
      analysis_append(safe, " ", 1);
      if (map)
        analysis_segment(map, start, start, at, safe->n, SEQUEL_MAP_INSERT);
    }
    bool token_gap = gap;
    gap = false;
    size_t before = safe->n;
    if (t.kind == 0 && sequel_runtime_symbol(t.t, '(') && t.matching > i) {
      analysis_append(safe, "(?", 2);
      if (map) {
        analysis_segment(map, start, end, before, before + 1, SEQUEL_MAP_COPY);
        size_t close = ts[t.matching].source_offset;
        analysis_segment(map, end, close, before + 1, before + 2, SEQUEL_MAP_REPLACE);
      }
      analysis_layout_add(layout, before, 1, 0, token_gap, a->options.dialect);
      analysis_layout_add(layout, before + 1, 1, 0, false, a->options.dialect);
      collapse_end = t.matching;
    } else {
      analysis_append(safe, literal || bind ? "?" : t.t.p, literal || bind ? 1 : t.t.n);
      if (map)
        analysis_segment(map, start, end, before, safe->n, literal || bind ? SEQUEL_MAP_REPLACE : SEQUEL_MAP_COPY);
      analysis_layout_add(layout, before, safe->n - before, literal || bind ? 0 : t.kind, token_gap,
                          a->options.dialect);
    }
    if (!a->options.omit_query_text) {
      size_t cap = a->options.max_query_text_bytes ? a->options.max_query_text_bytes : SEQUEL_TEXT_MAX;
      size_t lo = emitted_before < cap ? emitted_before : cap, hi = safe->n < cap ? safe->n : cap;
      analysis_charge(a, hi - lo);
    }
  }
  analysis_dictionary_clear(&dictionary);
  analysis_dictionary_clear(&slot_dictionary);
  if (consumed < n && map)
    analysis_segment(map, consumed, n, safe->n, safe->n, SEQUEL_MAP_DELETE);
  if (!safe->p)
    analysis_append(safe, "", 0);
}

static void analysis_clip_map(analysis_map *map, size_t limit) {
  sequel_current_call->output_bytes -= map->n * sizeof(*map->p);
  analysis_map clipped = {0};
  for (size_t i = 0; i < map->n; i++) {
    sequel_work_tick(1);
    sequel_map_segment_t s = map->p[i];
    size_t ts = s.to_start < limit ? s.to_start : limit, te = s.to_end < limit ? s.to_end : limit;
    if (s.kind == SEQUEL_MAP_COPY && te - ts < s.from_end - s.from_start) {
      size_t middle = s.from_start + (te - ts);
      analysis_segment(&clipped, s.from_start, middle, ts, te, SEQUEL_MAP_COPY);
      analysis_segment(&clipped, middle, s.from_end, te, te, SEQUEL_MAP_DELETE);
    } else
      analysis_segment(&clipped, s.from_start, s.from_end, ts, te, ts == te ? SEQUEL_MAP_DELETE : s.kind);
  }
  free(map->p);
  *map = clipped;
}

static void analysis_format_map(analysis_layout *layout, size_t fn, size_t tn, analysis_map *map) {
  size_t fp = 0, tp = 0;
  for (size_t i = 0; i < layout->n; i++) {
    sequel_work_tick(1);
    format_token t = layout->p[i];
    size_t f = t.source_offset, to = t.rendered_offset;
    if (f > fp || to > tp)
      analysis_segment(map, fp, f, tp, to,
                       f == fp    ? SEQUEL_MAP_INSERT
                       : to == tp ? SEQUEL_MAP_DELETE
                                  : SEQUEL_MAP_REPLACE);
    analysis_segment(map, f, f + t.t.n, to, to + t.t.n, SEQUEL_MAP_COPY);
    fp = f + t.t.n;
    tp = to + t.t.n;
  }
  if (fp < fn || tp < tn)
    analysis_segment(map, fp, fn, tp, tn,
                     fp == fn   ? SEQUEL_MAP_INSERT
                     : tp == tn ? SEQUEL_MAP_DELETE
                                : SEQUEL_MAP_REPLACE);
}

static void analysis_summary_from_query(const sequel_query_document_t *query, sequel_analysis_t *out) {
  sequel_summary_options_t options;
  sequel_summary_options_init(&options);
  options.include_bind_count = false;
  sequel_text_result_t summary = {0};
  if (sequel_summarize_query(query, &options, &summary) != SEQUEL_OK)
    sequel_abort(SEQUEL_ERR_NOMEM);
  size_t length = summary.length;
  char *copy = analysis_array(NULL, length + 1, sizeof(*copy));
  if (length && summary.data)
    memcpy(copy, summary.data, length);
  copy[length] = 0;
  out->summary = copy;
  sequel_text_result_free(&summary);
}

static void analysis_summary_default(sequel_analysis_t *out) {
  const char fallback[] = "SQL";
  char *summary = analysis_array(NULL, sizeof(fallback), sizeof(*summary));
  memcpy(summary, fallback, sizeof(fallback));
  out->summary = summary;
}

static void analysis_operation_from_query(const sequel_query_document_t *query, sequel_analysis_t *out) {
  if (!query || !sequel_query_statement_count(query))
    return;
  const sequel_statement_info_t *statement = sequel_query_statements(query);
  const char *name = NULL;
  switch (statement[0].operation) {
  case SEQUEL_OPERATION_SELECT:
    name = "SELECT";
    break;
  case SEQUEL_OPERATION_INSERT:
    name = "INSERT";
    break;
  case SEQUEL_OPERATION_UPDATE:
    name = "UPDATE";
    break;
  case SEQUEL_OPERATION_DELETE:
    name = "DELETE";
    break;
  case SEQUEL_OPERATION_CREATE:
    name = "CREATE";
    break;
  case SEQUEL_OPERATION_ALTER:
    name = "ALTER";
    break;
  case SEQUEL_OPERATION_DROP:
    name = "DROP";
    break;
  case SEQUEL_OPERATION_EXPLAIN:
    name = "EXPLAIN";
    break;
  default:
    return;
  }
  snprintf(out->operation, sizeof(out->operation), "%s", name);
}

static void analysis_empty(sequel_analysis_t *out, size_t n, sequel_dialect_t dialect) {
  *out = (sequel_analysis_t){0};
  out->schema_version = SEQUEL_ANALYSIS_VERSION;
  out->dialect = dialect;
  out->original_length = n;
  out->status = "error";
  out->omission_reason = "analysis_error";
  out->text = "";
  out->summary = "SQL";
  out->formatted = "";
}

void sequel_analysis_free(sequel_analysis_t *out) {
  if (!out)
    return;
  sequel_release_allocations(out->_storage);
  analysis_empty(out, 0, SEQUEL_GENERIC);
}

static sequel_status_t analysis_fail(sequel_analysis_t *out, size_t n, sequel_dialect_t d, sequel_status_t status,
                                     sequel_diagnostic_code_t code) {
  analysis_empty(out, n, d);
  out->status = status == SEQUEL_ERR_CANCELLED                                ? "cancelled"
                : status == SEQUEL_ERR_LIMIT || status == SEQUEL_ERR_OVERFLOW ? "limit"
                : status == SEQUEL_ERR_INVALID_ARGUMENT                       ? "malformed"
                                                                              : "error";
  out->omission_reason = status == SEQUEL_ERR_CANCELLED      ? "cancelled"
                         : !strcmp(out->status, "limit")     ? "analysis_limit"
                         : !strcmp(out->status, "malformed") ? "malformed"
                                                             : "analysis_error";
  diagnostic_report(NULL, 0, &out->_failure_diagnostic, code, 0, 0);
  out->diagnostics = &out->_failure_diagnostic;
  out->diagnostic_count = 1;
  return status;
}

sequel_status_t sequel_analyze_n(const char *sql, size_t n, const sequel_analysis_options_t *options,
                                 sequel_analysis_t *out) {
  if (!out)
    return SEQUEL_ERR_NULL;
  sequel_analysis_options_t o = options ? *options : (sequel_analysis_options_t){0};
  analysis_empty(out, n, o.dialect);
  if (!sql)
    return analysis_fail(out, n, o.dialect, SEQUEL_ERR_NULL, SEQUEL_DIAG_NULL_INPUT);
  if (o.dialect < SEQUEL_GENERIC || o.dialect > SEQUEL_CLICKHOUSE || o.max_input_bytes > SEQUEL_INPUT_MAX ||
      o.max_output_bytes > SEQUEL_TEXT_MAX || o.max_query_text_bytes > SEQUEL_TEXT_MAX || sequel_current_call)
    return analysis_fail(out, n, o.dialect, SEQUEL_ERR_INVALID_ARGUMENT, SEQUEL_DIAG_INVALID_ARGUMENT);
  size_t input_limit = o.max_input_bytes ? o.max_input_bytes : SEQUEL_INPUT_MAX;
  if (n > input_limit)
    return analysis_fail(out, n, o.dialect, SEQUEL_ERR_LIMIT, SEQUEL_DIAG_INPUT_LIMIT);
  size_t memory_limit = o.max_memory_bytes ? o.max_memory_bytes : SEQUEL_INPUT_MAX;
  if (memory_limit < sizeof(sequel_call))
    return analysis_fail(out, n, o.dialect, SEQUEL_ERR_LIMIT, SEQUEL_DIAG_RESOURCE_LIMIT);
  sequel_call *call = sequel_call_new();
  if (!call)
    return analysis_fail(out, n, o.dialect, SEQUEL_ERR_NOMEM, SEQUEL_DIAG_OUT_OF_MEMORY);
  call->source = sql;
  call->output_limit = o.max_output_bytes ? o.max_output_bytes : SEQUEL_TEXT_MAX;
  call->memory_limit = o.max_memory_bytes ? o.max_memory_bytes : SEQUEL_INPUT_MAX;
  call->work_limit = o.max_work_units;
  call->cancelled = o.cancelled;
  call->userdata = o.cancel_userdata;
  sequel_current_call = call;
  if (sequel_checkpoint(call)) {
    sequel_status_t failure = call->failure;
    size_t peak = call->peak, work = call->work;
    sequel_current_call = NULL;
    sequel_release_allocations(call->allocations);
    sequel_call_delete(call);
    sequel_status_t result = analysis_fail(out, n, o.dialect, failure,
                                           failure == SEQUEL_ERR_CANCELLED ? SEQUEL_DIAG_CANCELLED
                                           : failure == SEQUEL_ERR_LIMIT   ? SEQUEL_DIAG_RESOURCE_LIMIT
                                           : failure == SEQUEL_ERR_NOMEM   ? SEQUEL_DIAG_OUT_OF_MEMORY
                                                                           : SEQUEL_DIAG_INVALID_PARAMETER);
    out->memory_peak_bytes = peak;
    out->work_units = work;
    return result;
  }
  if (sizeof(*call) > call->memory_limit)
    sequel_abort(SEQUEL_ERR_LIMIT);
  call->live = call->peak = sizeof(*call);
  sequel_work_tick(1);
  analysis_state a = {out, o};
  sequel_diagnostic_t first;
  sequel_status_t valid = diagnostic_input(sql, n, o.dialect, &first);
  if (valid != SEQUEL_OK) {
    analysis_add_diagnostic(out, first.code, first.offset, first.end_offset);
    analysis_positions(sql, out);
    out->status = "malformed";
    out->omission_reason = "malformed";
  } else {
    const char *ambiguous = NULL;
    if ((o.dialect == SEQUEL_MYSQL && (ambiguous = sequel_text_find(sql, n, "$$"))) ||
        (o.dialect == SEQUEL_POSTGRESQL && (ambiguous = sequel_text_find(sql, n, "\\'")))) {
      call->ambiguous_literal = true;
      call->ambiguous_offset = (size_t)(ambiguous - sql);
    }
    sequel_parse_options_t parse_options;
    sequel_parse_options_init(&parse_options);
    parse_options.dialect = o.dialect;
    parse_options.retain_comments = true;
    sequel_source_view_t parse_source = {sql, n};
    sequel_syntax_document_t *syntax = NULL;
    sequel_query_document_t *query = NULL;
    size_t parse_issue_count = 0;
    sequel_status_t parse_status = sequel_parse_sql(&parse_source, &parse_options, NULL, &syntax);
    if (parse_status == SEQUEL_OK) {
      sequel_syntax_issues(syntax, &parse_issue_count);
      if (sequel_build_query_document(syntax, &parse_options, &query) != SEQUEL_OK) {
        valid = SEQUEL_ERR_NOMEM;
        analysis_add_diagnostic(out, SEQUEL_DIAG_OUT_OF_MEMORY, 0, 0);
      }
    } else {
      valid = parse_status;
      analysis_add_diagnostic(
          out, parse_status == SEQUEL_ERR_LIMIT ? SEQUEL_DIAG_RESOURCE_LIMIT : SEQUEL_DIAG_INVALID_ARGUMENT, 0,
          n ? 1 : 0);
    }
    format_token *tokens = NULL;
    size_t scanned = 0;
    size_t count = SIZE_MAX;
    sequel_status_t adapter_status = SEQUEL_ERR_INVALID_ARGUMENT;
    if (syntax)
      adapter_status = adapt_lexed_tokens(sequel_syntax_lexed(syntax), &tokens, &scanned);
    if (adapter_status == SEQUEL_OK)
      count = scanned;
    else {
      count = 0;
      scanned = 0;
    }
    if (call->ambiguous_literal) {
      diagnostic_report(sql, n, &first, SEQUEL_DIAG_AMBIGUOUS_LITERAL, call->ambiguous_offset,
                        call->ambiguous_offset + 2);
      count = SIZE_MAX;
    }
    if (syntax)
      analysis_recovered_statement_ranges(sequel_syntax_lexed(syntax), out);
    if (count == SIZE_MAX) {
      if (out->statement_count && scanned &&
          !(tokens[scanned - 1].kind == 0 && sequel_runtime_symbol(tokens[scanned - 1].t, ';')))
        ((sequel_range_t *)out->statements)[out->statement_count - 1].end_offset = n;
      else if (first.offset < n)
        analysis_add_statement(out, first.offset, n);
      if (!parse_issue_count)
        analysis_add_diagnostic(out, first.code, first.offset, first.end_offset);
      valid = SEQUEL_ERR_INVALID_ARGUMENT;
    } else if (syntax && parse_issue_count) {
      for (size_t issue_index = 0; issue_index < parse_issue_count; ++issue_index) {
        sequel_diagnostic_t parser_diagnostic;
        if (sequel_syntax_diagnostic(syntax, issue_index, &parser_diagnostic) == SEQUEL_OK &&
            !analysis_has_diagnostic(out, parser_diagnostic.code, parser_diagnostic.offset,
                                     parser_diagnostic.end_offset))
          analysis_add_diagnostic(out, parser_diagnostic.code, parser_diagnostic.offset, parser_diagnostic.end_offset);
      }
    }
    if (out->diagnostic_count) {
      analysis_sort_diagnostics(out);
      analysis_positions(sql, out);
      out->status = "malformed";
      out->omission_reason = "malformed";
      valid = SEQUEL_ERR_INVALID_ARGUMENT;
    } else {
      analysis_text safe = {0};
      analysis_map map = {0};
      analysis_layout layout = {0};
      sequel_sha256 hash;
      sha_init(&hash);
      sha_frame(&hash, 'V', "sequel-fingerprint-v1", 21, false);
      sha_frame(&hash, 'D', analysis_dialect_name(o.dialect), strlen(analysis_dialect_name(o.dialect)), false);
      bool maps = !o.omit_source_maps && !o.omit_query_text;
      analysis_sanitize(sql, n, tokens, count, &a, &safe, maps ? &map : NULL, &layout, &hash);
      if (o.keep_in_lists)
        analysis_compact_list_text(&safe);
      if (out->statement_count) {
        char digest[65];
        sha_final(&hash, digest);
        snprintf(out->fingerprint, sizeof(out->fingerprint), "sequel:v1:%s:%s", analysis_dialect_name(o.dialect),
                 digest);
        analysis_charge(&a, strlen(out->fingerprint) + 1);
      }
      if (query)
        analysis_operation_from_query(query, out);
      if (query && sequel_query_statement_count(query))
        analysis_summary_from_query(query, out);
      else
        analysis_summary_default(out);
      for (size_t i = 0; i < layout.n; ++i)
        layout.p[i].t.p = safe.p + layout.p[i].source_offset;
      analysis_charge(&a, strlen(out->operation) + 1);
      size_t limit = o.max_query_text_bytes ? o.max_query_text_bytes : SEQUEL_TEXT_MAX;
      if (!o.omit_query_text && safe.n > limit) {
        out->truncated = true;
        utf8_cut(safe.p, limit);
        safe.n = strlen(safe.p);
        if (maps)
          analysis_clip_map(&map, safe.n);
      }
      out->status = "ok";
      out->omission_reason = o.omit_query_text ? "policy" : !safe.n ? "empty" : "";
      if (o.include_lints && query) {
        sequel_diagnostic_list_t policy = {0};
        if (sequel_collect_policy_diagnostics(query, &policy) != SEQUEL_OK) {
          analysis_add_diagnostic(out, SEQUEL_DIAG_OUT_OF_MEMORY, 0, 0);
        } else {
          for (size_t i = 0; i < policy.count; ++i)
            analysis_add_diagnostic(out, policy.records[i].code, policy.records[i].range.start,
                                    policy.records[i].range.end);
        }
        sequel_diagnostic_list_free(&policy);
        analysis_positions(sql, out);
      } else if (o.include_lints) {
        analysis_policy_diagnostics_pass(sql, n, tokens, count, o.dialect, out);
        analysis_positions(sql, out);
      }
      if (o.omit_query_text)
        free(safe.p);
      else {
        /* The sanitizer may have no output allocation for comment-only input.
         * The public ABI promises non-NULL strings, including empty results. */
        out->text = safe.p ? safe.p : "";
        analysis_charge(&a, 1);
        if (maps) {
          out->original_to_sanitized = (sequel_source_map_t){n, safe.n, map.p, map.n};
        }
        if (o.format && safe.n && !out->truncated) {
          size_t capacity = safe.n + 128;
          char *formatted = malloc(capacity);
          size_t before = call->output_bytes;
          output b = {formatted, 0, capacity, false, true};
          formatted[0] = 0;
          while (!format_layout(layout.p, layout.n, &b)) {
            if (!b.overflow) {
              free(formatted);
              formatted = NULL;
              call->output_bytes = before;
              break;
            }
            if (capacity > SIZE_MAX / 2)
              sequel_abort(SEQUEL_ERR_LIMIT);
            capacity *= 2;
            formatted = realloc(formatted, capacity);
            formatted[0] = 0;
            call->output_bytes = before;
            b = (output){formatted, 0, capacity, false, true};
          }
          if (formatted) {
            analysis_charge(&a, 1);
            out->formatted = formatted;
            if (maps) {
              analysis_map fm = {0};
              analysis_format_map(&layout, safe.n, b.n, &fm);
              out->sanitized_to_formatted = (sequel_source_map_t){safe.n, b.n, fm.p, fm.n};
            }
          }
        }
      }
      free(layout.p);
    }
    sequel_query_document_free(query);
    sequel_syntax_document_free(syntax);
    free(tokens);
  }

  sequel_work_tick(1);
  out->memory_peak_bytes = call->peak;
  out->work_units = call->work;
  out->_storage = call->allocations;
  sequel_current_call = NULL;
  sequel_call_delete(call);
  return valid;
}

sequel_status_t sequel_analyze(const char *sql, const sequel_analysis_options_t *options, sequel_analysis_t *out) {
  if (!out)
    return SEQUEL_ERR_NULL;
  sequel_analysis_options_t o = options ? *options : (sequel_analysis_options_t){0};
  size_t n = 0, limit = o.max_input_bytes ? o.max_input_bytes : SEQUEL_INPUT_MAX;
  if (limit > SEQUEL_INPUT_MAX)
    limit = SEQUEL_INPUT_MAX;
  if (sql)
    while (n <= limit && sql[n]) {
      if ((n & 255) == 0 && o.cancelled && o.cancelled(o.cancel_userdata))
        return analysis_fail(out, n, o.dialect, SEQUEL_ERR_CANCELLED, SEQUEL_DIAG_CANCELLED);
      if (o.max_work_units && n >= o.max_work_units)
        return analysis_fail(out, n, o.dialect, SEQUEL_ERR_LIMIT, SEQUEL_DIAG_RESOURCE_LIMIT);
      n++;
    }
  if (o.max_work_units) {
    if (n >= o.max_work_units)
      return analysis_fail(out, n, o.dialect, SEQUEL_ERR_LIMIT, SEQUEL_DIAG_RESOURCE_LIMIT);
    o.max_work_units -= n;
  }
  sequel_status_t status = sequel_analyze_n(sql, n, &o, out);
  if (n <= SIZE_MAX - out->work_units)
    out->work_units += n;
  return status;
}

sequel_status_t sequel_fingerprint(const char *sql, sequel_dialect_t dialect, char out[96]) {
  if (!out)
    return SEQUEL_ERR_NULL;
  out[0] = 0;
  if (sql) {
    const char *cursor = sql;
    while (*cursor && isspace((unsigned char)*cursor))
      ++cursor;
    if (!*cursor || (cursor[0] == '-' && cursor[1] == '-') || (cursor[0] == '/' && cursor[1] == '*'))
      return SEQUEL_OK;
  }
  sequel_analysis_options_t o = {0};
  o.dialect = dialect;
  o.omit_query_text = true;
  o.omit_source_maps = true;
  o.omit_bind_descriptors = true;
  sequel_analysis_t a;
  sequel_status_t status = sequel_analyze(sql, &o, &a);
  if (status == SEQUEL_OK)
    memcpy(out, a.fingerprint, sizeof(a.fingerprint));
  sequel_analysis_free(&a);
  return status;
}

sequel_status_t sequel_check_binds(const sequel_bind_descriptor_t *binds, size_t count, size_t statement,
                                   size_t supplied) {
  if (!binds && count)
    return SEQUEL_ERR_NULL;
  size_t needed = 0;
  for (size_t i = 0; i < count; i++) {
    sequel_work_tick(1);
    if (!binds[i].slot || !binds[i].ordinal || binds[i].offset >= binds[i].end_offset ||
        binds[i].style < SEQUEL_BIND_POSITIONAL || binds[i].style > SEQUEL_BIND_TYPED)
      return SEQUEL_ERR_INVALID_ARGUMENT;
    if (binds[i].statement_index == statement && binds[i].slot > needed)
      needed = binds[i].slot;
  }
  return supplied == needed ? SEQUEL_OK : SEQUEL_ERR_INVALID_ARGUMENT;
}
