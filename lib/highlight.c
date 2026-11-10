/* Presentation only. The shared lexer identifies opaque comments, identifiers,
 * strings and parameters; gaps are emitted directly from the original input. */
#include "base16.h"

static const char *const highlight_color_names[] = {"foreground", "background", "keyword",     "type",      "function",
                                                    "variable",   "string",     "number",      "boolean",   "null",
                                                    "comment",    "operator",   "punctuation", "identifier"};

size_t sequel_colorscheme_count(void) { return sizeof(highlight_schemes) / sizeof(highlight_schemes[0]); }
const char *sequel_colorscheme_name(size_t index) {
  return index < sequel_colorscheme_count() ? highlight_schemes[index].name : NULL;
}
sequel_status_t sequel_colorscheme(const char *name, sequel_colorscheme_t *out) {
  if (!name || !out)
    return SEQUEL_ERR_NULL;
  for (size_t i = 0; (sequel_work_tick(1), i < sequel_colorscheme_count()); ++i)
    if (!strcmp(name, highlight_schemes[i].name)) {
      *out = highlight_schemes[i].palette;
      return SEQUEL_OK;
    }
  return SEQUEL_ERR_INVALID_ARGUMENT;
}

static int highlight_hex_digit(unsigned char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}
static bool highlight_hex(const char *s, size_t n, uint32_t *color) {
  if (n == 7 && *s == '#') {
    ++s;
    --n;
  }
  if (n != 6)
    return false;
  uint32_t value = 0;
  for (size_t i = 0; (sequel_work_tick(1), i < n); ++i) {
    int digit = highlight_hex_digit((unsigned char)s[i]);
    if (digit < 0)
      return false;
    value = value * 16 + (unsigned)digit;
  }
  *color = value;
  return true;
}

sequel_status_t sequel_colorscheme_parse(const char *yaml, sequel_colorscheme_t *out) {
  if (!yaml || !out)
    return SEQUEL_ERR_NULL;
  size_t n = 0;
  while (n <= INPUT_MAX && yaml[n])
    ++n;
  if (n > INPUT_MAX)
    return SEQUEL_ERR_OVERFLOW;
  if (!sequel_utf8_valid(yaml, n))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  unsigned seen = 0;
  sequel_colorscheme_t parsed = {{0}};
  for (size_t start = 0; (sequel_work_tick(1), start < n);) {
    size_t end = start;
    while (end < n && yaml[end] != '\n')
      ++end;
    size_t p = start;
    start = end + 1;
    while (p < end && (yaml[p] == ' ' || yaml[p] == '\t' || yaml[p] == '\r'))
      ++p;
    char key_quote = p < end && (yaml[p] == '\'' || yaml[p] == '"') ? yaml[p++] : 0;
    if (end - p < 6 || memcmp(yaml + p, "base", 4))
      continue;
    int hi = highlight_hex_digit((unsigned char)yaml[p + 4]);
    int lo = highlight_hex_digit((unsigned char)yaml[p + 5]);
    if (hi != 0 || lo < 0)
      return SEQUEL_ERR_INVALID_ARGUMENT;
    p += 6;
    if (key_quote && (p == end || yaml[p++] != key_quote))
      return SEQUEL_ERR_INVALID_ARGUMENT;
    if (p == end || yaml[p++] != ':' || (seen & (1u << lo)))
      return SEQUEL_ERR_INVALID_ARGUMENT;
    while (p < end && (yaml[p] == ' ' || yaml[p] == '\t'))
      ++p;
    char quote = p < end && (yaml[p] == '\'' || yaml[p] == '"') ? yaml[p++] : 0;
    size_t value = p;
    if (p < end && yaml[p] == '#')
      ++p;
    p += 6;
    if (p > end || !highlight_hex(yaml + value, p - value, &parsed.base[lo]))
      return SEQUEL_ERR_INVALID_ARGUMENT;
    if (quote && (p == end || yaml[p++] != quote))
      return SEQUEL_ERR_INVALID_ARGUMENT;
    if (p < end && !isspace((unsigned char)yaml[p]) && yaml[p] != '#')
      return SEQUEL_ERR_INVALID_ARGUMENT;
    while (p < end && isspace((unsigned char)yaml[p]))
      ++p;
    if (p < end && yaml[p] != '#')
      return SEQUEL_ERR_INVALID_ARGUMENT;
    seen |= 1u << lo;
  }
  if (seen != 0xffff)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  *out = parsed;
  return SEQUEL_OK;
}

sequel_status_t sequel_highlight_set_colorscheme(sequel_highlight_options_t *options,
                                                 const sequel_colorscheme_t *scheme) {
  if (!options || !scheme)
    return SEQUEL_ERR_NULL;
  for (size_t i = 0; (sequel_work_tick(1), i < 16); ++i)
    if (scheme->base[i] > 0xffffff)
      return SEQUEL_ERR_INVALID_ARGUMENT;
  static const unsigned slots[] = {5, 0, 14, 10, 13, 8, 11, 9, 9, 9, 3, 5, 5, 5};
  for (size_t i = 0; (sequel_work_tick(1), i < SEQUEL_COLOR_COUNT); ++i)
    options->colors[i] = scheme->base[slots[i]];
  return SEQUEL_OK;
}
static unsigned highlight_luminance(uint32_t color) {
  return 2126u * (color >> 16) + 7152u * ((color >> 8) & 255) + 722u * (color & 255);
}
const char *sequel_colorscheme_variant_name(const char *name, sequel_appearance_t appearance) {
  if (!name || appearance < SEQUEL_APPEARANCE_AUTO || appearance > SEQUEL_APPEARANCE_LIGHT)
    return NULL;
  for (size_t i = 0; (sequel_work_tick(1), i < sequel_colorscheme_count()); ++i)
    if (!strcmp(name, highlight_schemes[i].name)) {
      if (appearance == SEQUEL_APPEARANCE_AUTO)
        return highlight_schemes[i].name;
      return appearance == SEQUEL_APPEARANCE_DARK ? highlight_schemes[i].dark : highlight_schemes[i].light;
    }
  return NULL;
}
sequel_status_t sequel_colorscheme_for_appearance(const char *name, sequel_appearance_t appearance,
                                                  sequel_colorscheme_t *out) {
  if (!name || !out)
    return SEQUEL_ERR_NULL;
  const char *variant = sequel_colorscheme_variant_name(name, appearance);
  return variant ? sequel_colorscheme(variant, out) : SEQUEL_ERR_INVALID_ARGUMENT;
}
sequel_status_t sequel_colorscheme_set_appearance(sequel_colorscheme_t *scheme, sequel_appearance_t appearance) {
  if (!scheme)
    return SEQUEL_ERR_NULL;
  if (appearance < SEQUEL_APPEARANCE_AUTO || appearance > SEQUEL_APPEARANCE_LIGHT)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  for (size_t i = 0; (sequel_work_tick(1), i < 16); ++i)
    if (scheme->base[i] > 0xffffff)
      return SEQUEL_ERR_INVALID_ARGUMENT;
  if (appearance == SEQUEL_APPEARANCE_AUTO)
    return SEQUEL_OK;
  for (size_t i = 0; (sequel_work_tick(1), i < sequel_colorscheme_count()); ++i)
    if (!memcmp(scheme->base, highlight_schemes[i].palette.base, sizeof(scheme->base)))
      return sequel_colorscheme_for_appearance(highlight_schemes[i].name, appearance, scheme);
  bool light = highlight_luminance(scheme->base[0]) > highlight_luminance(scheme->base[5]);
  return light == (appearance == SEQUEL_APPEARANCE_LIGHT) ? SEQUEL_OK : SEQUEL_ERR_INVALID_ARGUMENT;
}
void sequel_highlight_options_init(sequel_highlight_options_t *options) {
  if (!options)
    return;
  memset(options, 0, sizeof(*options));
  sequel_colorscheme_t palette;
  if (sequel_colorscheme("default-dark", &palette) == SEQUEL_OK)
    (void)sequel_highlight_set_colorscheme(options, &palette);
}
const char *sequel_highlight_color_name(sequel_highlight_color_t type) {
  return type >= 0 && type < SEQUEL_COLOR_COUNT ? highlight_color_names[type] : NULL;
}
sequel_status_t sequel_highlight_set_color(sequel_highlight_options_t *options, const char *type, const char *hex) {
  if (!options || !type || !hex)
    return SEQUEL_ERR_NULL;
  uint32_t color;
  if (!highlight_hex(hex, strlen(hex), &color))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  for (size_t i = 0; (sequel_work_tick(1), i < SEQUEL_COLOR_COUNT); ++i)
    if (!strcmp(type, highlight_color_names[i])) {
      options->colors[i] = color;
      return SEQUEL_OK;
    }
  return SEQUEL_ERR_INVALID_ARGUMENT;
}

static bool highlight_word_in(token t, const char *words) {
  while (*words) {
    const char *end = strchr(words, ' ');
    size_t n = end ? (size_t)(end - words) : strlen(words);
    if (t.n == n) {
      size_t i = 0;
      while (i < n && toupper((unsigned char)t.p[i]) == (unsigned char)words[i])
        ++i;
      if (i == n)
        return true;
    }
    if (!end)
      break;
    words = end + 1;
  }
  return false;
}

static bool highlight_text_word(const char *text, size_t length, const char *words) {
  token value = {text, length};
  return highlight_word_in(value, words);
}

static sequel_highlight_color_t highlight_kind(format_token *ts, size_t count, size_t i, sequel_dialect_t dialect) {
  token t = ts[i].t;
  unsigned char c = (unsigned char)*t.p;
  if (ts[i].kind == 3 || ts[i].kind == 4)
    return SEQUEL_COLOR_COMMENT;
  if (ts[i].kind == 2) {
    if (c == '"' || c == '`' || c == '[')
      return dialect == SEQUEL_MYSQL && c == '"' ? SEQUEL_COLOR_STRING : SEQUEL_COLOR_IDENTIFIER;
    if (dialect == SEQUEL_CLICKHOUSE && clickhouse_quote(t.p, t.n, 0))
      return clickhouse_quote(t.p, t.n, 0) == 0x98 ? SEQUEL_COLOR_STRING : SEQUEL_COLOR_IDENTIFIER;
    if (c == '{' || c == '%' || c == '@' || (c == '$' && dialect == SEQUEL_SQLITE))
      return SEQUEL_COLOR_VARIABLE;
    return SEQUEL_COLOR_STRING;
  }
  if (isdigit(c) || (c == '.' && t.n > 1 && isdigit((unsigned char)t.p[1])))
    return SEQUEL_COLOR_NUMBER;
  if (c == '$' || c == '@' || c == ':' || c == '?') {
    if ((c == ':' && t.n > 1 && (t.p[1] == ':' || t.p[1] == '=')) ||
        (c == '@' && (t.n == 1 || !letter((unsigned char)t.p[1]))))
      return SEQUEL_COLOR_OPERATOR;
    /* PostgreSQL's existence operator occurs between an expression and a key.
     * In other positions ? remains a driver parameter. */
    size_t next = format_next(ts, count, i + 1);
    if (c == '?' && dialect == SEQUEL_POSTGRESQL && i && next < count &&
        (ts[i - 1].kind == 1 || ts[i - 1].kind == 2 || sym(ts[i - 1].t, ')')) && ts[next].kind == 2 &&
        *ts[next].t.p == '\'')
      return SEQUEL_COLOR_OPERATOR;
    return SEQUEL_COLOR_VARIABLE;
  }
  if (ts[i].kind == 1) {
    if (kw(t, "TRUE") || kw(t, "FALSE"))
      return SEQUEL_COLOR_BOOLEAN;
    if (kw(t, "NULL"))
      return SEQUEL_COLOR_NULL;
    if (highlight_word_in(t, "BIGINT BIGSERIAL BINARY BIT BLOB BOOL BOOLEAN BYTEA CHAR CHARACTER DATE DATETIME "
                             "DECIMAL DOUBLE ENUM FIXEDSTRING FLOAT FLOAT32 FLOAT64 INT INT8 INT16 INT32 INT64 INT128 "
                             "INT256 INTEGER INTERVAL JSON JSONB LOWCARDINALITY MAP MONEY NCHAR NULLABLE NUMERIC "
                             "NVARCHAR REAL SERIAL SMALLINT STRING TEXT TIME TIMESTAMP TIMESTAMPTZ TINYINT TUPLE "
                             "UINT8 UINT16 UINT32 UINT64 UINT128 UINT256 UUID VARBINARY VARCHAR VECTOR VOID"))
      return SEQUEL_COLOR_TYPE;
    if (highlight_word_in(t, "ABORT ACTION ADD AFTER ALL ALTER ANALYZE AND ANY ARRAY AS ASC ASOF ATTACH AUTO_INCREMENT "
                             "BEGIN BETWEEN BOTH BY CALL CASCADE CASE CAST CHECK CLUSTER COLLATE COLUMN COMMENT COMMIT "
                             "CONFLICT CONSTRAINT COPY CREATE CROSS CUBE CURRENT CURRENT_DATE CURRENT_TIME "
                             "CURRENT_TIMESTAMP DATABASE DEFAULT DEFERRABLE DELETE DESC DESCRIBE DETACH DISTINCT "
                             "DISTRIBUTED DO DROP EACH ELSE END ENGINE ESCAPE EXCEPT EXCLUDE EXCLUSIVE EXEC EXECUTE "
                             "EXISTS EXPLAIN EXTENSION FETCH FILTER FINAL FIRST FOLLOWING FOR FOREIGN FORMAT FROM "
                             "FULL FUNCTION GLOBAL GRANT GROUP GROUPING HAVING IF ILIKE IN INDEX INHERITS INITIALLY "
                             "INNER INSERT INTERSECT INTO IS JOIN KEY LATERAL LEADING LEFT LIKE LIMIT LOCAL LOCK "
                             "MATERIALIZED MERGE NATURAL NEXT NO NOT NOTHING NULLS OF OFFSET ON ONLY OPTIMIZE OPTION "
                             "OR ORDER OUTER OVER OVERLAPS PARTITION PRECEDING PREPARE PREWHERE PRIMARY PROCEDURE "
                             "QUALIFY RANGE RECURSIVE REFERENCES REGEXP RELEASE RENAME REPLACE RESTRICT RETURN "
                             "RETURNING REVOKE RIGHT RLIKE ROLLBACK ROLLUP ROW ROWS SAVEPOINT SCHEMA SELECT SEMI "
                             "SEQUENCE SET SETTINGS SHOW SOME START TABLE TABLESAMPLE TEMP TEMPORARY THEN TIES TO "
                             "TOP TRAILING TRANSACTION TRIGGER TRUNCATE UNBOUNDED UNION UNIQUE UNNEST UPDATE USE "
                             "USING VACUUM VALUES VIEW WHEN WHERE WINDOW WITH WITHIN WITHOUT XOR"))
      return SEQUEL_COLOR_KEYWORD;
    size_t next = format_next(ts, count, i + 1);
    if (next < count && sym(ts[next].t, '('))
      return SEQUEL_COLOR_FUNCTION;
    return SEQUEL_COLOR_IDENTIFIER;
  }
  return strchr("~!#%^&|+-*/<>=", c) ? SEQUEL_COLOR_OPERATOR : SEQUEL_COLOR_PUNCTUATION;
}

static sequel_highlight_color_t highlight_token_kind(const sequel_token_t *tokens, size_t count, size_t index,
                                                     const sequel_source_view_t *source, sequel_dialect_t dialect) {
  const sequel_token_t *token = &tokens[index];
  const char *text = source->data + token->range.start;
  size_t length = token->range.end - token->range.start;
  if (token->kind == SEQUEL_TOKEN_LINE_COMMENT || token->kind == SEQUEL_TOKEN_BLOCK_COMMENT)
    return SEQUEL_COLOR_COMMENT;
  if (token->kind == SEQUEL_TOKEN_STRING)
    return SEQUEL_COLOR_STRING;
  if (token->kind == SEQUEL_TOKEN_QUOTED_IDENTIFIER)
    return SEQUEL_COLOR_IDENTIFIER;
  if (token->kind == SEQUEL_TOKEN_NUMBER)
    return SEQUEL_COLOR_NUMBER;
  if (token->kind == SEQUEL_TOKEN_BOOLEAN)
    return SEQUEL_COLOR_BOOLEAN;
  if (token->kind == SEQUEL_TOKEN_NULL)
    return SEQUEL_COLOR_NULL;
  if (token->kind == SEQUEL_TOKEN_PARAMETER)
    return SEQUEL_COLOR_VARIABLE;
  if (token->kind == SEQUEL_TOKEN_OPERATOR)
    return SEQUEL_COLOR_OPERATOR;
  if (token->kind == SEQUEL_TOKEN_KEYWORD) {
    if (highlight_text_word(text, length, "TRUE FALSE"))
      return SEQUEL_COLOR_BOOLEAN;
    if (highlight_text_word(text, length, "NULL"))
      return SEQUEL_COLOR_NULL;
    if (highlight_text_word(
            text, length,
            "BIGINT INTEGER INT DATE DATETIME DECIMAL DOUBLE FLOAT JSON NUMERIC REAL TEXT TIME TIMESTAMP "
            "UUID VARCHAR"))
      return SEQUEL_COLOR_TYPE;
    return SEQUEL_COLOR_KEYWORD;
  }
  if (token->kind == SEQUEL_TOKEN_IDENTIFIER && index + 1 < count &&
      tokens[index + 1].kind == SEQUEL_TOKEN_PUNCTUATION && source->data[tokens[index + 1].range.start] == '(')
    return SEQUEL_COLOR_FUNCTION;
  (void)dialect;
  return token->kind == SEQUEL_TOKEN_PUNCTUATION ? SEQUEL_COLOR_PUNCTUATION : SEQUEL_COLOR_IDENTIFIER;
}

typedef struct {
  char *out;
  size_t n, capacity;
} highlight_output;
static void highlight_put(highlight_output *b, const char *s, size_t n) {
  if (b->out && b->n < b->capacity && n < b->capacity - b->n)
    memcpy(b->out + b->n, s, n);
  b->n += n;
}
static void highlight_text(highlight_output *b, const char *s) { highlight_put(b, s, strlen(s)); }
static void highlight_source(highlight_output *b, const char *s, size_t n, sequel_highlight_mode_t mode) {
  if (mode == SEQUEL_ANSI) {
    highlight_put(b, s, n);
    return;
  }
  for (size_t i = 0; (sequel_work_tick(1), i < n); ++i) {
    const char *escaped = NULL;
    switch (s[i]) {
    case '&':
      escaped = "&amp;";
      break;
    case '<':
      escaped = "&lt;";
      break;
    case '>':
      escaped = "&gt;";
      break;
    case '"':
      escaped = "&quot;";
      break;
    case '\'':
      escaped = "&#39;";
      break;
    case '\r':
      escaped = "&#13;";
      break;
    }
    if (escaped)
      highlight_text(b, escaped);
    else
      highlight_put(b, s + i, 1);
  }
}
static void highlight_ansi(highlight_output *b, uint32_t color, bool background) {
  char code[40];
  snprintf(code, sizeof(code), "\033[%u;2;%u;%u;%um", background ? 48u : 38u, (unsigned)(color >> 16),
           (unsigned)((color >> 8) & 255), (unsigned)(color & 255));
  highlight_text(b, code);
}

sequel_status_t sequel_highlight_ex(const char *sql, const sequel_highlight_options_t *options, char *out,
                                    size_t capacity, size_t *length, sequel_diagnostic_t *diagnostic) {
  sequel_diagnostic_t local;
  if (!diagnostic)
    diagnostic = &local;
  diagnostic_report(NULL, 0, diagnostic, SEQUEL_DIAG_NONE, 0, 0);
  if (out && capacity)
    *out = 0;
  if (length)
    *length = 0;
  if (!sql || !length) {
    diagnostic_report(NULL, 0, diagnostic, !sql ? SEQUEL_DIAG_NULL_INPUT : SEQUEL_DIAG_INVALID_ARGUMENT, 0, 0);
    return SEQUEL_ERR_NULL;
  }
  sequel_highlight_options_t defaults;
  if (!options) {
    sequel_highlight_options_init(&defaults);
    options = &defaults;
  }
  if (options->dialect < SEQUEL_GENERIC || options->dialect > SEQUEL_CLICKHOUSE ||
      (options->mode != SEQUEL_HTML && options->mode != SEQUEL_ANSI) || (!out && capacity) || (out && !capacity)) {
    diagnostic_report(sql, 0, diagnostic, SEQUEL_DIAG_INVALID_ARGUMENT, 0, 0);
    return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  for (size_t i = 0; (sequel_work_tick(1), i < SEQUEL_COLOR_COUNT); ++i)
    if (options->colors[i] > 0xffffff) {
      diagnostic_report(sql, 0, diagnostic, SEQUEL_DIAG_INVALID_ARGUMENT, 0, 0);
      return SEQUEL_ERR_INVALID_ARGUMENT;
    }
  size_t n = 0;
  while (n <= INPUT_MAX && sql[n])
    ++n;
  sequel_status_t status = diagnostic_input(sql, n, options->dialect, diagnostic);
  if (status != SEQUEL_OK)
    return status;
  sequel_parse_options_t parse_options;
  sequel_parse_options_init(&parse_options);
  parse_options.dialect = options->dialect;
  parse_options.retain_comments = true;
  parse_options.retain_whitespace = true;
  parse_options.tolerant = options->tolerant;
  sequel_source_view_t source = {sql, n};
  sequel_syntax_document_t *document = NULL;
  status = sequel_parse_sql(&source, &parse_options, NULL, &document);
  if (status != SEQUEL_OK)
    return status;
  size_t issue_count = 0;
  sequel_syntax_issues(document, &issue_count);
  if (issue_count)
    (void)sequel_syntax_diagnostic(document, 0, diagnostic);
  if (issue_count && (!options->tolerant || diagnostic->code == SEQUEL_DIAG_INVALID_PARAMETER ||
                      diagnostic->code == SEQUEL_DIAG_NESTING_LIMIT)) {
    sequel_syntax_document_free(document);
    return diagnostic->code == SEQUEL_DIAG_NESTING_LIMIT ? SEQUEL_ERR_OVERFLOW : SEQUEL_ERR_INVALID_ARGUMENT;
  }
  const sequel_lexed_document_t *lexed = sequel_syntax_lexed(document);
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  for (size_t i = 0; i < token_count; ++i) {
    if (tokens[i].kind == SEQUEL_TOKEN_BLOCK_COMMENT && (tokens[i].flags & SEQUEL_TOKEN_FLAG_RECOVERED)) {
      sequel_syntax_document_free(document);
      diagnostic_report(sql, n, diagnostic, SEQUEL_DIAG_NESTING_LIMIT, tokens[i].range.start,
                        tokens[i].range.end);
      return SEQUEL_ERR_OVERFLOW;
    }
  }
  highlight_output b = {out, 0, capacity};
  if (options->mode == SEQUEL_HTML) {
    char wrapper[128];
    snprintf(wrapper, sizeof(wrapper), "<pre class=\"sequel\" style=\"color:#%06x;background-color:#%06x\"><code>",
             (unsigned)options->colors[SEQUEL_COLOR_FOREGROUND], (unsigned)options->colors[SEQUEL_COLOR_BACKGROUND]);
    highlight_text(&b, wrapper);
  } else {
    highlight_ansi(&b, options->colors[SEQUEL_COLOR_FOREGROUND], false);
    highlight_ansi(&b, options->colors[SEQUEL_COLOR_BACKGROUND], true);
  }
  const char *cursor = sql;
  for (size_t i = 0; (sequel_work_tick(1), i < token_count); ++i) {
    const sequel_token_t *token = &tokens[i];
    if (token->kind == SEQUEL_TOKEN_EOF)
      continue;
    highlight_source(&b, cursor, token->range.start - (size_t)(cursor - sql), options->mode);
    size_t token_length = token->range.end - token->range.start;
    sequel_highlight_color_t kind = highlight_token_kind(tokens, token_count, i, &source, options->dialect);
    if (options->mode == SEQUEL_HTML) {
      char span[128];
      snprintf(span, sizeof(span), "<span class=\"sequel-%s\" style=\"color:#%06x\">", highlight_color_names[kind],
               (unsigned)options->colors[kind]);
      highlight_text(&b, span);
    } else
      highlight_ansi(&b, options->colors[kind], false);
    highlight_source(&b, source.data + token->range.start, token_length, options->mode);
    if (options->mode == SEQUEL_HTML)
      highlight_text(&b, "</span>");
    else
      highlight_ansi(&b, options->colors[SEQUEL_COLOR_FOREGROUND], false);
    cursor = source.data + token->range.end;
  }
  highlight_source(&b, cursor, n - (size_t)(cursor - sql), options->mode);
  highlight_text(&b, options->mode == SEQUEL_HTML ? "</code></pre>" : "\033[0m");
  sequel_syntax_document_free(document);
  *length = b.n;
  if (out) {
    if (capacity <= b.n) {
      *out = 0;
      diagnostic_report(sql, n, diagnostic, SEQUEL_DIAG_OUTPUT_LIMIT, 0, 0);
      return SEQUEL_ERR_OVERFLOW;
    }
    out[b.n] = 0;
  }
  return SEQUEL_OK;
}
sequel_status_t sequel_highlight(const char *sql, const sequel_highlight_options_t *options, char *out, size_t capacity,
                                 size_t *length) {
  return sequel_highlight_ex(sql, options, out, capacity, length, NULL);
}
