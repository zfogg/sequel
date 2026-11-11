#include "util/utf8.h"
#include <ctype.h>
#include <sequel.h>
#include "diagnostics.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "resources.h"
#include "util/text.h"

typedef struct {
  const char *p;
  size_t n;
  bool word, space, clickhouse;
} token;

typedef struct {
  char *p;
  size_t n, cap;
  bool overflow, charge_output;
} output;

/* Private rendering view shared by the formatter and analysis consumers. */
typedef struct {
  token t;
  bool gap;
  bool newline;
  int kind;
  size_t matching;
  bool multiline;
  size_t flat_width;
  size_t source_offset;
  size_t rendered_offset;
} format_token;

/* Token arrays grow with parsed tokens, rather than the maximum input size. */
static void *grow_tokens(void *tokens, size_t *capacity, size_t element_size) {
  size_t maximum = SIZE_MAX / element_size;
  if (*capacity == maximum)
    return NULL;
  size_t next = *capacity ? (*capacity > maximum / 2 ? maximum : *capacity * 2) : 256;
  void *grown = realloc(tokens, next * element_size);
  if (grown)
    *capacity = next;
  return grown;
}

static bool letter(unsigned char c) { return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

static bool part(unsigned char c) { return letter(c) || isdigit(c) || c == '$' || c >= 128; }

static bool equal(const char *s, size_t n, const char *word) {
  if (strlen(word) != n)
    return false;
  for (size_t i = 0; (sequel_work_tick(1), i < n); ++i)
    if (toupper((unsigned char)s[i]) != toupper((unsigned char)word[i]))
      return false;
  return true;
}

static bool kw(token t, const char *word) { return t.word && t.p && equal(t.p, t.n, word); }

static bool sym(token t, char c) { return t.n == 1 && t.p && *t.p == c; }

static void put(output *b, const char *s, size_t n) {
  if (b->charge_output)
    sequel_output_tick(n);
  if (n > b->cap - b->n - 1) {
    b->overflow = true;
    n = b->cap - b->n - 1;
  }
  sequel_work_tick(n);
  memcpy(b->p + b->n, s, n);
  b->n += n;
  b->p[b->n] = 0;
}

static void text(output *b, const char *s) { put(b, s, strlen(s)); }

static void item(output *b, const char *s) {
  if (b->n && b->p[b->n - 1] != ' ')
    text(b, " ");
  text(b, s);
}

static void emit_token(output *b, token t) {
  if (t.p && t.n)
    put(b, t.p, t.n);
}

static void upper(output *b, token t) {
  if (!t.p)
    return;
  for (size_t i = 0; (sequel_work_tick(1), i < t.n); ++i) {
    char c = (char)toupper((unsigned char)t.p[i]);
    put(b, &c, 1);
  }
}

static size_t quoted(const char *s, size_t n, size_t i, char close, bool backslash) {
  for (++i; (sequel_work_tick(1), i < n); ++i) {
    if (!backslash && close == '\'' && sequel_current_call && s[i] == '\\' && i + 1 < n) {
      if (s[i + 1] == '\\') {
        ++i;
        continue;
      }
      if (s[i + 1] == close) {
        sequel_current_call->ambiguous_literal = true;
        sequel_current_call->ambiguous_offset = (size_t)(s + i - sequel_current_call->source);
        return 0;
      }
    }
    if (backslash && s[i] == '\\') {
      if (++i == n)
        return 0;
      continue;
    }
    if (s[i] == close) {
      if (i + 1 < n && s[i + 1] == close) {
        ++i;
        continue;
      }
      return i + 1;
    }
  }
  return 0;
}

static size_t number_end(const char *s, size_t n, size_t i) {
  if (i + 1 < n && s[i] == '0' && strchr("xXbB", s[i + 1])) {
    i += 2;
    while (i < n && (isxdigit((unsigned char)s[i]) || s[i] == '_'))
      ++i;
    return i;
  }
  while (i < n && (isdigit((unsigned char)s[i]) || s[i] == '_' || s[i] == '.'))
    ++i;
  if (i < n && (s[i] == 'e' || s[i] == 'E')) {
    ++i;
    if (i < n && (s[i] == '+' || s[i] == '-'))
      ++i;
    while (i < n && (isdigit((unsigned char)s[i]) || s[i] == '_'))
      ++i;
  }
  return i;
}

/* ClickHouse's lexer accepts Unicode quote pairs without escape processing. */
static unsigned char clickhouse_quote(const char *s, size_t n, size_t i) {
  if (i + 2 < n && (unsigned char)s[i] == 0xe2 && (unsigned char)s[i + 1] == 0x80 &&
      ((unsigned char)s[i + 2] == 0x98 || (unsigned char)s[i + 2] == 0x9c))
    return (unsigned char)s[i + 2];
  return 0;
}

static size_t clickhouse_quoted_end(const char *s, size_t n, size_t i) {
  unsigned char close = clickhouse_quote(s, n, i) + 1;
  for (size_t j = i + 3; (sequel_work_tick(1), j + 2 < n); ++j)
    if ((unsigned char)s[j] == 0xe2 && (unsigned char)s[j + 1] == 0x80 && (unsigned char)s[j + 2] == close)
      return j + 3;
  return 0;
}

static size_t dollar_tag_end(const char *s, size_t n, size_t i, sequel_dialect_t dialect) {
  size_t j = i + 1;
  if (j < n && (letter((unsigned char)s[j]) || (dialect == SEQUEL_CLICKHOUSE && isdigit((unsigned char)s[j]))))
    while (j < n && (letter((unsigned char)s[j]) || isdigit((unsigned char)s[j])))
      ++j;
  return j < n && s[j] == '$' ? j : 0;
}

static size_t dialect_number_end(const char *s, size_t n, size_t i, sequel_dialect_t dialect) {
  if (dialect != SEQUEL_CLICKHOUSE || i + 2 >= n || s[i] != '0' || !strchr("xX", s[i + 1]))
    return number_end(s, n, i);
  size_t j = i + 2;
  while (j < n && (isxdigit((unsigned char)s[j]) || s[j] == '_'))
    ++j;
  if (j < n && s[j] == '.') {
    ++j;
    while (j < n && (isxdigit((unsigned char)s[j]) || s[j] == '_'))
      ++j;
  }
  if (j < n && strchr("pP", s[j])) {
    ++j;
    if (j < n && strchr("+-", s[j]))
      ++j;
    while (j < n && (isdigit((unsigned char)s[j]) || s[j] == '_'))
      ++j;
  }
  return j;
}

/* Typed ClickHouse parameters are opaque lexical tokens, including nested types.
 * Ignore parameter names/types in telemetry; never inspect the supplied values. */
static size_t clickhouse_parameter(const char *s, size_t n, size_t i) {
  size_t j = i + 1;
  if (j >= n || !letter((unsigned char)s[j]))
    return 0;
  while (j < n && part((unsigned char)s[j]))
    ++j;
  while (j < n && isspace((unsigned char)s[j]))
    ++j;
  if (j == n || s[j++] != ':')
    return 0;
  size_t start = j, depth = 0;
  for (; (sequel_work_tick(1), j < n); ++j) {
    if (s[j] == '\'' || s[j] == '"' || s[j] == '`') {
      size_t end = quoted(s, n, j, s[j], true);
      if (!end)
        return 0;
      j = end - 1;
    } else if (s[j] == '(') {
      if (++depth > 64)
        return 0;
    } else if (s[j] == ')') {
      if (!depth)
        return 0;
      --depth;
    } else if (s[j] == '}') {
      return !depth && j > start ? j + 1 : 0;
    } else if (s[j] == '{' || s[j] == ';')
      return 0;
  }
  return 0;
}

// clang-format off
static bool boundary(token t);
static sequel_status_t adapt_lexed_tokens(const sequel_lexed_document_t *lexed, format_token **out, size_t *count);
/* Preserve lexical contents; only whitespace outside lexical tokens changes.
 * Keeping the formatter independent of a grammar also supports driver/sqlc
 * parameters and sanitized telemetry templates that aren't executable SQL. */
static void format_line(output *b, size_t indent) {
  if (b->n && b->p[b->n - 1] != '\n')
    text(b, "\n");
  if (b->n)
    for (size_t i = 0; (sequel_work_tick(1), i < indent); ++i)
      text(b, "  ");
}

/* Clause state belongs to a parenthesis level; function arguments must not
 * accidentally inherit SELECT-list or WHERE-list comma/bool rules. */
enum { F_NONE, F_SELECT, F_FROM, F_CONDITION, F_ORDER, F_SET, F_VALUES, F_WITH, F_OTHER, F_ON };

typedef struct {
  size_t base, closing, header_end;
  int clause;
  bool active, pending, multiline, between, deleting, inserting, values_seen, window;
} format_frame;

typedef struct {
  size_t depth, base;
  bool condition, multiline;
} format_case;

static size_t format_next(format_token *ts, size_t n, size_t i) {
  while (i < n && ts[i].kind >= 3)
    ++i;
  return i;
}

static bool format_list(int c) { return c == F_SELECT || c == F_ORDER || c == F_SET || c == F_VALUES; }

static bool format_short_scalar_select(format_token *ts, size_t count, size_t select) {
  size_t end = select;
  for (size_t j = select + 1; (sequel_work_tick(1), j < count); ++j) {
    token t = ts[j].t;
    if (sym(t, ';') || sym(t, ')'))
      break;
    bool typed_parameter_comma = sym(t, ',') && t.clickhouse && j > select && ts[j - 1].kind == 2;
    if (ts[j].kind >= 3 || (sym(t, ',') && !typed_parameter_comma) || boundary(t) || kw(t, "EXCLUDE") ||
        kw(t, "CASE") ||
        kw(t, "SELECT") || kw(t, "WITH"))
      return false;
    /* ClickHouse typed parameters use a brace-delimited payload whose type
     * grammar may contain commas and nested parentheses.  The payload is one
     * lexical parameter value for layout purposes, so its internal shape must
     * not force the surrounding SELECT list multiline. */
    if (sym(t, '{')) {
      if (ts[j].matching <= j || ts[j].matching >= count)
        return false;
      j = ts[j].matching;
      end = j;
      continue;
    }
    if (sym(t, '(') || sym(t, '[') || sym(t, '{')) {
      if (ts[j].multiline || ts[j].matching <= j || ts[j].matching >= count)
        return false;
      j = ts[j].matching;
    }
    end = j;
  }
  return end > select && ts[end].flat_width - ts[select].flat_width <= 60;
}

static size_t format_body(format_frame f) {
  return f.base + (format_list(f.clause) || f.clause == F_CONDITION || f.clause == F_ON ? 1 : 0);
}

static void format_space(output *b) {
  if (b->n && b->p[b->n - 1] != ' ' && b->p[b->n - 1] != '\n')
    text(b, " ");
}

static bool format_join(format_token *ts, size_t n, size_t i) {
  token t = ts[i].t;
  if (kw(t, "JOIN"))
    return true;
  if (!(kw(t, "LEFT") || kw(t, "RIGHT") || kw(t, "FULL") || kw(t, "INNER") || kw(t, "CROSS") || kw(t, "NATURAL")))
    return false;
  for (size_t j = i + 1; (sequel_work_tick(1), j < n && j < i + 4); ++j) {
    if (kw(ts[j].t, "JOIN"))
      return true;
    if (!(kw(ts[j].t, "OUTER") || kw(ts[j].t, "LEFT") || kw(ts[j].t, "RIGHT") || kw(ts[j].t, "FULL") ||
          kw(ts[j].t, "INNER")))
      break;
  }
  return false;
}

static bool format_comparison(token t) {
  if (!t.p)
    return false;
  return equal(t.p, t.n, "=") || equal(t.p, t.n, "!=") || equal(t.p, t.n, "<>") || equal(t.p, t.n, "<") ||
         equal(t.p, t.n, ">") || equal(t.p, t.n, "<=") || equal(t.p, t.n, ">=");
}

static bool format_spaced_operator(format_token *ts, size_t i, size_t previous) {
  token t = ts[i].t;
  if (!t.p)
    return false;
  if (format_comparison(t))
    return true;
  if (!(equal(t.p, t.n, "+") || equal(t.p, t.n, "-") || equal(t.p, t.n, "*") || equal(t.p, t.n, "/") ||
        equal(t.p, t.n, "//") || equal(t.p, t.n, "%") || equal(t.p, t.n, "||")))
    return false;
  if (previous == SIZE_MAX)
    return false;
  token p = ts[previous].t;
  if (sym(p, ')') || sym(p, ']') || ts[previous].kind == 2 || isdigit((unsigned char)*p.p))
    return true;
  return p.word && !(kw(p, "SELECT") || kw(p, "DISTINCT") || kw(p, "THEN") || kw(p, "ELSE") || kw(p, "WHEN") ||
                     kw(p, "BY") || kw(p, "AND") || kw(p, "OR") || kw(p, "NOT") || kw(p, "WHERE") || kw(p, "SET") ||
                     kw(p, "VALUES") || kw(p, "RETURNING") || kw(p, "AS"));
}

static bool format_layout(format_token *ts, size_t count, output *b) {
  /* Match groups once, then propagate multiline constructs out through their
   * containing function calls. This avoids repeatedly scanning nested SQL. */
  size_t opens[65] = {0}, level = 0;
  sequel_work_tick(count);
  for (size_t i = 0; i < count; ++i) {
    token t = ts[i].t;
    if (!t.p || !t.n)
      continue;
    if (ts[i].kind == 0 && t.n == 1 && strchr("([{", *t.p)) {
      if (level == 64)
        return false;
      opens[++level] = i;
      if (i && kw(ts[i - 1].t, "OVER"))
        ts[i].multiline = true;
    } else if (ts[i].kind == 0 && t.n == 1 && strchr(")]}", *t.p)) {
      if (!level)
        return false;
      size_t start = opens[level--];
      if (*ts[start].t.p != (*t.p == ')' ? '(' : *t.p == ']' ? '[' : '{'))
        return false;
      ts[start].matching = i;
      ts[i].matching = start;
      if (*ts[start].t.p == '(') {
        bool direct_boolean = false;
        for (size_t j = start + 1; j < i; ++j) {
          if (ts[j].kind == 0 && ts[j].t.n == 1 && strchr("([{", *ts[j].t.p) && ts[j].matching > j) {
            j = ts[j].matching;
            continue;
          }
          if (kw(ts[j].t, "AND") || kw(ts[j].t, "OR")) {
            direct_boolean = true;
            break;
          }
        }
        if (direct_boolean && ts[i].flat_width - ts[start].flat_width <= 70)
          ts[start].multiline = false;
      }
      ts[start].multiline |= ts[i].flat_width - ts[start].flat_width > 70;
      if (level && ts[start].multiline)
        ts[opens[level]].multiline = true;
    } else if (level && (kw(t, "SELECT") || kw(t, "WITH") || kw(t, "CASE") || ts[i].kind == 3)) {
      ts[opens[level]].multiline = true;
    }
  }
  if (level)
    return false;
  format_frame frames[65] = {{0}};
  format_case cases[64];
  size_t depth = 0, case_count = 0, line_indent = 0;
  size_t previous = SIZE_MAX;
  bool after_comparison = false, join_head = false;
  sequel_work_tick(count);
  for (size_t i = 0; i < count; ++i) {
    format_token f = ts[i];
    token t = f.t;
    bool open = f.kind == 0 && t.p && t.n == 1 && strchr("([{", *t.p);
    bool close = f.kind == 0 && t.p && t.n == 1 && strchr(")]}", *t.p);
    bool line = false;
    size_t indent = format_body(frames[depth]);
    if (close) {
      line = frames[depth].multiline;
      indent = frames[depth].closing;
      --depth;
    }
    format_frame *ctx = &frames[depth];
    size_t next = f.kind >= 3 ? i + 1 : format_next(ts, count, i + 1);
    bool head = ctx->pending && i <= ctx->header_end;
    bool select = kw(t, "SELECT");
    bool with = kw(t, "WITH") && !ctx->active;
    bool modifier = format_join(ts, count, i) && !join_head;
    bool from = kw(t, "FROM") && ctx->active && !head;
    bool condition =
        ((t.clickhouse && kw(t, "PREWHERE")) || kw(t, "WHERE") || kw(t, "HAVING") || kw(t, "QUALIFY")) && ctx->active;
    bool order =
        (kw(t, "ORDER") || kw(t, "GROUP") || kw(t, "PARTITION")) && next < count && kw(ts[next].t, "BY") && ctx->active;
    bool set = kw(t, "SET") && ctx->active;
    bool values =
        kw(t, "VALUES") && ctx->clause != F_SELECT && ((!ctx->active) || (ctx->inserting && !ctx->values_seen));
    bool returning = (kw(t, "RETURNING") || (t.clickhouse && (kw(t, "SETTINGS") || kw(t, "FORMAT")))) && ctx->active;
    bool on = kw(t, "ON") && ctx->clause == F_FROM && !head;
    bool conflict = kw(t, "ON") && next < count && (kw(ts[next].t, "CONFLICT") || kw(ts[next].t, "DUPLICATE"));
    bool bound = (kw(t, "LIMIT") || kw(t, "OFFSET")) && ctx->active;
    bool setop = (kw(t, "UNION") || kw(t, "INTERSECT") || kw(t, "EXCEPT")) && ctx->active;
    bool logical = (kw(t, "AND") || kw(t, "OR")) && (ctx->clause == F_CONDITION || ctx->clause == F_ON) &&
                   (!case_count || cases[case_count - 1].depth != depth);
    if (kw(t, "BETWEEN"))
      ctx->between = true;
    bool between_and = kw(t, "AND") && ctx->between;
    if (between_and) {
      ctx->between = false;
      logical = false;
    }
    bool case_when = case_count && cases[case_count - 1].depth == depth && (kw(t, "WHEN") || kw(t, "ELSE"));
    bool case_end = case_count && cases[case_count - 1].depth == depth && kw(t, "END");
    format_case *case_ctx = case_count && cases[case_count - 1].depth == depth ? &cases[case_count - 1] : NULL;
    if (case_ctx && kw(t, "WHEN")) {
      case_ctx->condition = true;
      case_ctx->multiline = false;
      size_t nested = 0;
      for (size_t j = i + 1; (sequel_work_tick(1), j < count); ++j) {
        if (sym(ts[j].t, '(') || sym(ts[j].t, '[') || sym(ts[j].t, '{'))
          j = ts[j].matching;
        else if (kw(ts[j].t, "CASE"))
          ++nested;
        else if (kw(ts[j].t, "END") && nested)
          --nested;
        else if (!nested && kw(ts[j].t, "THEN")) {
          case_ctx->multiline |= ts[j].flat_width - ts[i].flat_width > 60;
          break;
        } else if (!nested && (kw(ts[j].t, "AND") || kw(ts[j].t, "OR")))
          case_ctx->multiline = true;
      }
    }
    bool nested_case = kw(t, "CASE") && case_count && previous != SIZE_MAX &&
                       (kw(ts[previous].t, "THEN") || kw(ts[previous].t, "ELSE"));
    bool spaced_operator = format_spaced_operator(ts, i, previous);
    bool keyword_group = open && previous != SIZE_MAX &&
                         (kw(ts[previous].t, "AS") || kw(ts[previous].t, "IN") || kw(ts[previous].t, "EXISTS") ||
                          kw(ts[previous].t, "OVER") || kw(ts[previous].t, "FILTER") || kw(ts[previous].t, "USING") ||
                          kw(ts[previous].t, "ON") || kw(ts[previous].t, "AND") || kw(ts[previous].t, "OR") ||
                          kw(ts[previous].t, "NOT"));
    if (select || with || modifier || (from && !ctx->deleting) || condition || order || set || values || returning ||
        conflict || bound || setop) {
      line = true;
      indent = ctx->base;
    } else if (on || logical) {
      line = true;
      indent = ctx->base + 1;
    } else if (ctx->pending && !head && f.kind < 3 && !close) {
      line = true;
      indent = format_body(*ctx);
    }
    if (case_when) {
      line = true;
      indent = cases[case_count - 1].base + 1;
    }
    if (case_ctx && case_ctx->condition && case_ctx->multiline && !case_when &&
        (kw(t, "AND") || kw(t, "OR") || kw(t, "THEN"))) {
      line = true;
      indent = case_ctx->base + (kw(t, "THEN") ? 1 : 2);
      if (between_and)
        line = false;
    }
    if (case_end) {
      line = true;
      indent = cases[--case_count].base;
    }
    if (nested_case) {
      line = true;
      indent = cases[case_count - 1].base + 2;
    }
    if (f.kind >= 3) {
      /* Keep inline comments inline, especially those following semicolons.
       * Their contents (including trailing spaces) are never trimmed. */
      line = f.newline || (i && ts[i - 1].kind == 3);
      indent = line_indent;
    } else if (i && ts[i - 1].kind == 3) {
      line = true;
    }
    /* PostgreSQL concatenates adjacent quoted strings only across a newline.
     * Preserve such newlines, including those beside intervening comments. */
    if (f.newline && (f.kind == 2 || (f.kind >= 3 && i && ts[i - 1].kind == 2)))
      line = true;
    if (line) {
      format_line(b, indent);
      line_indent = indent;
    } else if (!close && !sym(t, ',') && !sym(t, ';') &&
               ((f.gap && !(previous != SIZE_MAX && ts[previous].kind == 0 && ts[previous].t.p && ts[previous].t.n == 1 &&
                            strchr("([{", *ts[previous].t.p))) ||
                after_comparison || spaced_operator || keyword_group ||
                (previous != SIZE_MAX && sym(ts[previous].t, ',')))) {
      format_space(b);
    }
    ts[i].rendered_offset = b->n;
    emit_token(b, t);
    after_comparison = spaced_operator;
    if (f.kind >= 3)
      continue;
    if (!head)
      ctx->pending = false;
    if (kw(t, "CASE")) {
      if (case_count == 64)
        return false;
      cases[case_count++] = (format_case){depth, line_indent};
    }
    if (case_ctx && (kw(t, "THEN") || kw(t, "ELSE")))
      case_ctx->condition = false;
    if (select) {
      ctx->active = true;
      ctx->deleting = false;
      ctx->clause = F_SELECT;
      ctx->pending = !format_short_scalar_select(ts, count, i);
      ctx->header_end = i;
      size_t j = next;
      if (j < count && sym(ts[j].t, '*')) {
        size_t modifier = format_next(ts, count, j + 1);
        if (modifier < count && kw(ts[modifier].t, "EXCLUDE"))
          ctx->pending = true;
      }
      if (j < count && (kw(ts[j].t, "DISTINCT") || kw(ts[j].t, "ALL"))) {
        ctx->header_end = j;
        size_t k = format_next(ts, count, j + 1);
        if (kw(ts[j].t, "DISTINCT") && k < count && kw(ts[k].t, "ON")) {
          k = format_next(ts, count, k + 1);
          if (k < count && sym(ts[k].t, '('))
            ctx->header_end = ts[k].matching;
        }
      }
    } else if (with) {
      ctx->active = true;
      ctx->clause = F_WITH;
    } else if (!ctx->active && (kw(t, "UPDATE") || kw(t, "INSERT") || kw(t, "DELETE") || kw(t, "MERGE"))) {
      ctx->active = true;
      ctx->deleting = kw(t, "DELETE");
      ctx->inserting = kw(t, "INSERT");
      ctx->clause = F_OTHER;
    } else if (from || modifier) {
      ctx->clause = F_FROM;
    } else if (condition || on) {
      ctx->clause = on ? F_ON : F_CONDITION;
      ctx->pending = !on;
      ctx->header_end = i;
      ctx->between = false;
    } else if (order || set || values || returning) {
      if (values) {
        ctx->active = true;
        ctx->values_seen = true;
      }
      ctx->clause = set ? F_SET : values ? F_VALUES : F_ORDER;
      ctx->pending = true;
      ctx->header_end = order ? next : i;
      if (order && ctx->window) {
        /* Short single-expression window clauses read naturally inline.
         * Count only commas at this level; function arguments are opaque. */
        size_t end = next;
        bool list = false;
        for (size_t j = next + 1; (sequel_work_tick(1), j < count); ++j) {
          token item = ts[j].t;
          if (sym(item, ')') || kw(item, "ORDER") || kw(item, "ROWS") || kw(item, "RANGE") || kw(item, "GROUPS"))
            break;
          if (sym(item, ','))
            list = true;
          if (sym(item, '(') || sym(item, '[') || sym(item, '{'))
            j = ts[j].matching;
          end = j;
        }
        if (!list && ctx->window)
          ctx->pending = false;
      }
    } else if (bound || setop || conflict) {
      ctx->clause = F_OTHER;
    } else if (sym(t, ',')) {
      ctx->pending = format_list(ctx->clause) || ctx->clause == F_WITH || ctx->clause == F_FROM ||
                     (ctx->multiline && ctx->clause == F_NONE);
      ctx->header_end = i;
    } else if (sym(t, ';')) {
      ctx->active = false;
      ctx->deleting = false;
      ctx->inserting = false;
      ctx->values_seen = false;
      ctx->clause = F_NONE;
      ctx->pending = true;
      ctx->header_end = i;
    }
    if (modifier)
      join_head = !kw(t, "JOIN");
    else if (kw(t, "JOIN"))
      join_head = false;
    if (open) {
      size_t closing = head ? ctx->base : format_body(*ctx);
      bool window = previous != SIZE_MAX && kw(ts[previous].t, "OVER");
      bool case_condition = case_ctx && case_ctx->condition && case_ctx->multiline;
      if (case_condition)
        closing = case_ctx->base + 2;
      bool subquery = next < count && (kw(ts[next].t, "SELECT") || kw(ts[next].t, "WITH") || kw(ts[next].t, "UPDATE") ||
                                       kw(ts[next].t, "DELETE"));
      bool boolean = false;
      if ((ctx->clause == F_CONDITION || ctx->clause == F_ON || case_condition) &&
          (previous == SIZE_MAX || !ts[previous].t.word || kw(ts[previous].t, "AND") || kw(ts[previous].t, "OR") ||
           kw(ts[previous].t, "NOT") || kw(ts[previous].t, "WHERE") || kw(ts[previous].t, "HAVING") ||
           kw(ts[previous].t, "ON"))) {
        for (size_t j = i + 1; (sequel_work_tick(1), j < ts[i].matching); ++j) {
          if (ts[j].kind == 0 && ts[j].t.n == 1 && strchr("([{", *ts[j].t.p))
            j = ts[j].matching;
          else if (kw(ts[j].t, "AND") || kw(ts[j].t, "OR"))
            boolean = true;
        }
      }
      /* Short boolean groups fit inline; only expanded groups need clause
       * state that breaks their AND/OR tokens onto separate lines. */
      boolean = boolean && (ts[i].multiline || subquery);
      bool multiline = ts[i].multiline || subquery;
      if (ctx->clause == F_WITH)
        closing = ctx->base;
      frames[++depth] = (format_frame){.base = closing + (multiline && !boolean ? 1 : 0),
                                       .closing = closing,
                                       .clause = boolean ? F_CONDITION : F_NONE,
                                       .active = boolean || window,
                                       .pending = multiline,
                                       .header_end = i,
                                       .multiline = multiline,
                                       .window = window};
    }
    previous = i;
  }
  return !case_count && !b->overflow;
}

sequel_status_t sequel_format(const char *sql, sequel_dialect_t dialect, char *out, size_t capacity, size_t *length) {
  if (out && capacity)
    *out = 0;
  if (!sql || !length)
    return SEQUEL_ERR_NULL;
  *length = 0;
  if (dialect < SEQUEL_GENERIC || dialect > SEQUEL_CLICKHOUSE || (!out && capacity) || (out && !capacity))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  size_t n = 0;
  while (n <= SEQUEL_INPUT_MAX && sql[n])
    ++n;
  if (n > SEQUEL_INPUT_MAX)
    return SEQUEL_ERR_OVERFLOW;
  if (!sequel_utf8_valid(sql, n))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  sequel_diagnostic_t input_diagnostic = {0};
  if (sequel_diagnose_n(sql, n, dialect, &input_diagnostic) != SEQUEL_OK &&
      input_diagnostic.code == SEQUEL_DIAG_UNSUPPORTED_PAYLOAD)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  /* The syntax-aware layout pass intentionally performs nesting analysis. For
   * very large inputs, preserving the already-valid source is both lossless
   * and bounded, and avoids turning formatter work into quadratic behavior on
   * generated SQL templates. */
  if (n > 256 * 1024) {
    *length = n;
    if (!out)
      return SEQUEL_OK;
    if (capacity <= n)
      return SEQUEL_ERR_OVERFLOW;
    memcpy(out, sql, n + 1);
    return SEQUEL_OK;
  }
  /* Preserve vendor join modifiers until the syntax-aware formatter has a
   * structured representation for them; rewriting them is not lossless. */
  if (dialect == SEQUEL_CLICKHOUSE &&
      (sequel_text_find(sql, n, "WITH ") == sql || sequel_text_find(sql, n, "GLOBAL ANY") ||
       sequel_text_find(sql, n, " LEFT SEMI ") || sequel_text_find(sql, n, " LEFT ANTI ") ||
       sequel_text_find(sql, n, " ASOF ") || sequel_text_find(sql, n, " ARRAY JOIN ") ||
       sequel_text_find(sql, n, " GLOBAL IN "))) {
    *length = n;
    if (!out)
      return SEQUEL_OK;
    if (capacity <= n)
      return SEQUEL_ERR_OVERFLOW;
    memcpy(out, sql, n + 1);
    return SEQUEL_OK;
  }
  format_token *ts = NULL;
  sequel_lexed_document_t *lexed = NULL;
  sequel_parse_options_t parse_options;
  sequel_parse_options_init(&parse_options);
  parse_options.dialect = dialect;
  parse_options.retain_comments = true;
  parse_options.retain_whitespace = true;
  sequel_source_view_t source = {sql, n};
  sequel_status_t status = sequel_lex(&source, &parse_options, NULL, &lexed);
  if (status != SEQUEL_OK)
    return status;
  size_t cap = n + 1024;
  char *buf = calloc(cap, 1);
  if (!buf) {
    sequel_lexed_document_free(lexed);
    free(buf);
    return SEQUEL_ERR_NOMEM;
  }
  size_t scanned = 0;
  status = adapt_lexed_tokens(lexed, &ts, &scanned);
  if (status != SEQUEL_OK)
    goto done;
  size_t count = scanned;
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  for (size_t i = 0; i < token_count; ++i)
    if (tokens[i].flags & (SEQUEL_TOKEN_FLAG_UNTERMINATED | SEQUEL_TOKEN_FLAG_RECOVERED)) {
      status = SEQUEL_ERR_INVALID_ARGUMENT;
      goto done;
    }
  output b = {buf, 0, cap};
  while (!format_layout(ts, count, &b)) {
    if (!b.overflow) {
      status = SEQUEL_ERR_INVALID_ARGUMENT;
      goto done;
    }
    /* Deep, valid queries can expand beyond the initial layout estimate.
     * Grow the private staging buffer; the caller still receives all or none. */
    if (cap > SIZE_MAX / 2) {
      status = SEQUEL_ERR_OVERFLOW;
      goto done;
    }
    cap *= 2;
    char *larger = realloc(buf, cap);
    if (!larger) {
      status = SEQUEL_ERR_NOMEM;
      goto done;
    }
    buf = larger;
    *buf = 0;
    b = (output){buf, 0, cap};
  }
  *length = b.n;
  if (out) {
    if (capacity <= b.n) {
      status = SEQUEL_ERR_OVERFLOW;
      goto done;
    }
    memcpy(out, b.p, b.n + 1);
  }
  status = SEQUEL_OK;
done:
  free(ts);
  free(buf);
  sequel_lexed_document_free(lexed);
  return status;
}


#undef malloc
#undef calloc
#undef realloc
#undef free
#include "diagnostics.c"
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#include "api.c"
#include "compact.c"
#include "highlight.c"
#include "sha256.c"
#include "analysis.c"
// clang-format on

#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#undef sequel_checkpoint
