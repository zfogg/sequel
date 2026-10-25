#include "embedded.h"
#include "../utf8.h"
#include <ctype.h>
#include <sequel.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { GO = 1, C, PY, JS, TS, RUST, HS, CLOJURE };
typedef struct {
  char *p;
  size_t n, cap;
  int bad;
} buffer;
static void append(buffer *b, const char *p, size_t n) {
  if (b->bad)
    return;
  if (n > 64 * 1024 * 1024 - b->n - 1) {
    b->bad = 1;
    return;
  }
  size_t need = b->n + n + 1;
  if (need > b->cap) {
    size_t cap = need * 2;
    char *next = realloc(b->p, cap);
    if (!next) {
      b->bad = 1;
      return;
    }
    b->p = next;
    b->cap = cap;
  }
  memcpy(b->p + b->n, p, n);
  b->n += n;
  b->p[b->n] = 0;
}
static void str(buffer *b, const char *s) { append(b, s, strlen(s)); }
static int word(const char *s, const char *w) {
  while (*w) {
    if (toupper((unsigned char)*s++) != *w++)
      return 0;
  }
  return !isalnum((unsigned char)*s) && *s != '_';
}
static int hasword(const char *s, const char *w) {
  for (size_t i = 0; s[i]; ++i)
    if ((!i || !isalnum((unsigned char)s[i - 1])) && word(s + i, w))
      return 1;
  return 0;
}
static int sql_candidate(const char *s) {
  for (;;) {
    while (isspace((unsigned char)*s))
      ++s;
    if (s[0] == '-' && s[1] == '-') {
      const char *end = strchr(s, '\n');
      if (!end)
        return 0;
      s = end + 1;
    } else if (s[0] == '/' && s[1] == '*') {
      const char *end = strstr(s + 2, "*/");
      if (!end)
        return 0;
      s = end + 2;
    } else
      break;
  }
  if (word(s, "SELECT")) {
    s += 6;
    while (isspace((unsigned char)*s))
      ++s;
    return hasword(s, "FROM") || hasword(s, "AS") ||
           isdigit((unsigned char)*s) || *s == '\'' || *s == '"' || *s == '*' ||
           strchr(s, '(') != NULL;
  }
  return (word(s, "WITH") && hasword(s, "SELECT")) ||
         (word(s, "INSERT") && hasword(s, "INTO")) ||
         (word(s, "UPDATE") && hasword(s, "SET")) ||
         (word(s, "DELETE") && hasword(s, "FROM")) ||
         (word(s, "CREATE") &&
          (hasword(s, "TABLE") || hasword(s, "INDEX") || hasword(s, "VIEW"))) ||
         (word(s, "ALTER") && hasword(s, "TABLE")) ||
         (word(s, "DROP") &&
          (hasword(s, "TABLE") || hasword(s, "INDEX") || hasword(s, "VIEW"))) ||
         (word(s, "EXPLAIN") && hasword(s, "SELECT")) ||
         (word(s, "TRUNCATE") && hasword(s, "TABLE")) || word(s, "PRAGMA") ||
         (word(s, "REPLACE") && hasword(s, "INTO"));
}
static char *format_sql(const char *s, int dialect) {
  if (!sql_candidate(s) || strstr(s, "{{") || strstr(s, "{}") ||
      strstr(s, "{:"))
    return NULL;
  for (size_t i = 0; s[i] && s[i + 1]; ++i) {
    if (s[i] == '\'' || s[i] == '"' || s[i] == '`') {
      char quote = s[i];
      for (++i; s[i]; ++i) {
        if (s[i] == '\\' && s[i + 1]) {
          ++i;
          continue;
        }
        if (s[i] == quote) {
          if (s[i + 1] == quote) {
            ++i;
            continue;
          }
          break;
        }
      }
      if (!s[i])
        break;
      continue;
    }
    if (s[i] != '%')
      continue;
    size_t p = i + 1;
    if (s[p] == '%' || strchr("sdvfxXqTbgopacCeEGrtuwUO", s[p]))
      return NULL;
    while (s[p] && strchr("-+# 0123456789.*[]$hlzjtLI", s[p]))
      ++p;
    if (s[p] && strchr("sdvfxXqTbgopacCeEGrtuwUO", s[p]))
      return NULL;
  }
  size_t length;
  if (sequel_format(s, (sequel_dialect_t)dialect, NULL, 0, &length) !=
      SEQUEL_OK)
    return NULL;
  char *out = malloc(length + 1);
  if (!out)
    return NULL;
  if (sequel_format(s, (sequel_dialect_t)dialect, out, length + 1,
                       &length) != SEQUEL_OK) {
    free(out);
    return NULL;
  }
  return out;
}
int embedded_language(const char *name, const char *path) {
  if (!name && path) {
    const char *dot = strrchr(path, '.');
    name = dot ? dot + 1 : NULL;
  }
  if (!name || strlen(name) > 31)
    return 0;
  char lower[32];
  size_t i = 0;
  for (; name[i]; ++i)
    lower[i] = (char)tolower((unsigned char)name[i]);
  lower[i] = 0;
  if (!strcmp(lower, "go"))
    return GO;
  const char *c[] = {"c", "c++", "cpp", "h", "hpp", "cc", "cxx", "hxx", NULL};
  for (i = 0; c[i]; ++i)
    if (!strcmp(lower, c[i]))
      return C;
  if (!strcmp(lower, "python") || !strcmp(lower, "py"))
    return PY;
  if (!strcmp(lower, "javascript") || !strcmp(lower, "js") ||
      !strcmp(lower, "mjs"))
    return JS;
  if (!strcmp(lower, "typescript") || !strcmp(lower, "ts"))
    return TS;
  if (!strcmp(lower, "rust") || !strcmp(lower, "rs"))
    return RUST;
  if (!strcmp(lower, "haskell") || !strcmp(lower, "hs"))
    return HS;
  if (!strcmp(lower, "clojure") || !strcmp(lower, "clj") ||
      !strcmp(lower, "cljs") || !strcmp(lower, "cljc") || !strcmp(lower, "edn"))
    return CLOJURE;
  return 0;
}
typedef struct {
  size_t start, body, end, tail;
  char quote;
  int triple, raw, skip, cppraw;
} literal;
/* Interpolation is opaque, but its nested strings/comments/braces must be
 * scanned so a nested template cannot masquerade as a separate SQL string. */
static size_t template_end(const char *s, size_t n, size_t start,
                           unsigned depth, int *dynamic) {
  if (depth >= 64)
    return SIZE_MAX;
  for (size_t i = start + 1; i < n; ++i) {
    if (s[i] == '\\') {
      if (++i == n)
        return SIZE_MAX;
      continue;
    }
    if (s[i] == '`')
      return i + 1;
    if (s[i] != '$' || i + 1 == n || s[i + 1] != '{')
      continue;
    *dynamic = 1;
    size_t braces = 1;
    i += 2;
    char previous = '{';
    for (; i < n && braces; ++i) {
      char c = s[i];
      if (c == '`') {
        int nested = 0;
        size_t end = template_end(s, n, i, depth + 1, &nested);
        if (end == SIZE_MAX)
          return SIZE_MAX;
        i = end - 1;
        previous = 'x';
        continue;
      }
      if (c == '\'' || c == '"') {
        size_t q = i + 1;
        for (; q < n; ++q) {
          if (s[q] == '\\') {
            if (++q == n)
              return SIZE_MAX;
            continue;
          }
          if (s[q] == c)
            break;
          if (s[q] == '\n' || s[q] == '\r')
            return SIZE_MAX;
        }
        if (q == n)
          return SIZE_MAX;
        i = q;
        previous = 'x';
        continue;
      }
      if (c == '/' && i + 1 < n && s[i + 1] == '/') {
        while (i < n && s[i] != '\n' && s[i] != '\r')
          ++i;
        if (i == n)
          return SIZE_MAX;
        continue;
      }
      if (c == '/' && i + 1 < n && s[i + 1] == '*') {
        size_t q = i + 2;
        while (q + 1 < n && !(s[q] == '*' && s[q + 1] == '/'))
          ++q;
        if (q + 1 == n)
          return SIZE_MAX;
        i = q + 1;
        continue;
      }
      if (c == '/' && strchr("(=:[{!,?&|;", previous)) {
        size_t q = i + 1;
        int bracket = 0;
        for (; q < n; ++q) {
          if (s[q] == '\\') {
            if (++q == n)
              return SIZE_MAX;
            continue;
          }
          if (s[q] == '[')
            bracket = 1;
          else if (s[q] == ']')
            bracket = 0;
          else if (s[q] == '/' && !bracket)
            break;
          if (s[q] == '\n' || s[q] == '\r')
            return SIZE_MAX;
        }
        if (q == n)
          return SIZE_MAX;
        i = q;
        previous = 'x';
        continue;
      }
      if (c == '{') {
        if (++braces > 64)
          return SIZE_MAX;
      } else if (c == '}')
        --braces;
      if (!isspace((unsigned char)c))
        previous = c;
    }
    if (braces)
      return SIZE_MAX;
    --i;
  }
  return SIZE_MAX;
}
static int scan_literal(const char *s, size_t n, size_t start, int lang,
                        literal *l) {
  memset(l, 0, sizeof(*l));
  l->start = start;
  l->quote = s[start];
  l->raw = lang == GO && s[start] == '`';
  l->triple = lang == PY && start + 2 < n && s[start + 1] == s[start] &&
              s[start + 2] == s[start];
  l->body = start + (l->triple ? 3 : 1);
  if ((lang == JS || lang == TS) && s[start] == '`') {
    int dynamic = 0;
    size_t end = template_end(s, n, start, 0, &dynamic);
    if (end == SIZE_MAX)
      return 0;
    l->end = end - 1;
    l->tail = end;
    l->skip = dynamic;
    return 1;
  }

  if (lang == C && start && s[start - 1] == 'R' && s[start] == '"') {
    size_t p = start + 1;
    while (p < n && p - start <= 17 && s[p] != '(')
      ++p;
    if (p == n || s[p] != '(' || p - start > 17)
      return 0;
    char closing[20];
    size_t dn = p - start - 1;
    closing[0] = ')';
    memcpy(closing + 1, s + start + 1, dn);
    closing[dn + 1] = '"';
    closing[dn + 2] = 0;
    const char *end = strstr(s + p + 1, closing);
    if (!end)
      return 0;
    l->raw = 1;
    l->cppraw = 1;
    l->body = p + 1;
    l->end = (size_t)(end - s);
    l->tail = l->end + dn + 2;
    return 1;
  }

  if ((lang == GO || lang == C || lang == RUST || lang == HS) &&
      s[start] == '\'')
    l->skip = 1;
  if (lang == RUST) {
    size_t p = start;
    while (p && s[p - 1] == '#')
      --p;
    if (p && s[p - 1] == 'r') {
      size_t hashes = start - p;
      if (hashes > 255)
        return 0;
      if (p > 1 && s[p - 2] == 'b')
        l->skip = 1;
      for (size_t q = start + 1; q < n; ++q)
        if (s[q] == '"') {
          size_t k = 0;
          while (k < hashes && q + 1 + k < n && s[q + 1 + k] == '#')
            ++k;
          if (k == hashes) {
            l->body = start + 1;
            l->end = q;
            l->tail = q + 1 + hashes;
            l->raw = 1;
            l->cppraw = 1;
            return 1;
          }
        }
      return 0;
    }
    if (start && s[start - 1] == 'b')
      l->skip = 1;
  }
  if (lang == CLOJURE && start && s[start - 1] == '#')
    l->skip = 1;
  if (lang == PY) {
    size_t p = start;
    while (p && isalpha((unsigned char)s[p - 1]))
      --p;
    for (size_t i = p; i < start; ++i) {
      if (s[i] == 'r' || s[i] == 'R')
        l->raw = 1;
      if (strchr("fFbB", s[i]))
        l->skip = 1;
    }
  }
  for (size_t i = l->body; i < n; ++i) {
    if (!l->raw && s[i] == '\\') {
      if (++i == n)
        return 0;
      if (s[i] == '\r' && i + 1 < n && s[i + 1] == '\n')
        ++i;
      continue;
    }
    if (lang == PY && l->raw && s[i] == '\\') {
      if (++i == n)
        return 0;
      if (s[i] == '\r' && i + 1 < n && s[i + 1] == '\n')
        ++i;
      continue;
    }
    if (s[i] == l->quote && (!l->triple || (i + 2 < n && s[i + 1] == l->quote &&
                                            s[i + 2] == l->quote))) {
      l->end = i;
      l->tail = i + (l->triple ? 3 : 1);
      return 1;
    }
    if (s[i] == '\n' && lang != CLOJURE && !l->triple && !l->raw &&
        !(s[start] == '`' && (lang == JS || lang == TS)))
      return 0;
  }
  return 0;
}
static int decode(const char *s, literal l, buffer *b) {
  for (size_t i = l.body; i < l.end; ++i) {
    char c = s[i];
    if (!l.raw && c == '\\') {
      c = s[++i];
      switch (c) {
      case 'n':
        c = '\n';
        break;
      case 'r':
        c = '\r';
        break;
      case 't':
        c = '\t';
        break;
      case '\\':
      case '\'':
      case '"':
      case '`':
        break;
      default:
        return 0;
      }
    }
    append(b, &c, 1);
  }
  if (!b->p)
    str(b, "");
  return !b->bad;
}
static void encode(buffer *out, const char *sql, literal l, int lang) {
  (void)lang;
  size_t n = strlen(sql);
  for (size_t i = 0; i < n; ++i) {
    char c = sql[i];
    if (l.raw)
      append(out, &c, 1);
    else if (c == '\n' && (l.triple || l.quote == '`'))
      append(out, &c, 1);
    else if (c == '\n')
      str(out, "\\n");
    else if (c == '\r' && (l.triple || l.quote == '`'))
      append(out, &c, 1);
    else if (c == '\r')
      str(out, "\\r");
    else if (c == '\t')
      str(out, "\\t");
    else {
      if (c == '\\' || c == l.quote)
        str(out, "\\");
      append(out, &c, 1);
    }
  }
}
static int quoted_character(const char *s, size_t n, size_t i) {
  unsigned char c = (unsigned char)s[i + 1];
  size_t width = c < 128 ? 1 : c < 224 ? 2 : c < 240 ? 3 : 4;
  return i + 1 + width < n && s[i + 1 + width] == '\'';
}
static int continuation(const char *s) {
  if (*s == ' ' || *s == '\t' || *s == ')' || *s == ',')
    return 1;
  const char *words[] = {"FROM",  "WHERE",  "AND",       "OR",    "JOIN",
                         "LEFT",  "RIGHT",  "INNER",     "GROUP", "ORDER",
                         "LIMIT", "OFFSET", "HAVING",    "UNION", "VALUES",
                         "SET",   "ON",     "RETURNING", NULL};
  for (size_t i = 0; words[i]; ++i)
    if (word(s, words[i]))
      return 1;
  return 0;
}
static void comment_lines(buffer *out, const char *sql, const char *indent,
                          size_t indent_n, const char *prefix,
                          const char *newline) {
  const char *p = sql;
  for (;;) {
    const char *end = strchr(p, '\n');
    if (!end) {
      str(out, p);
      break;
    }
    append(out, p, (size_t)(end - p));
    str(out, newline);
    append(out, indent, indent_n);
    str(out, prefix);
    p = end + 1;
  }
}
char *format_embedded(const char *s, int lang, int dialect) {
  size_t n = strlen(s);
  if (!sequel_utf8_valid(s, n))
    return NULL;
  buffer out = {0};
  size_t i = 0;
  while (i < n && !out.bad) {
    size_t start = i;
    int line = (lang == PY && s[i] == '#') ||
               (lang == CLOJURE && s[i] == ';') ||
               (lang == HS && s[i] == '-' && i + 1 < n && s[i + 1] == '-') ||
               ((lang == GO || lang == C || lang == RUST || lang == JS ||
                 lang == TS) &&
                s[i] == '/' && i + 1 < n && s[i + 1] == '/');
    int block = (lang == HS && s[i] == '{' && i + 1 < n && s[i + 1] == '-') ||
                ((lang == GO || lang == C || lang == RUST || lang == JS ||
                  lang == TS) &&
                 s[i] == '/' && i + 1 < n && s[i + 1] == '*');
    if (line) {
      size_t marker = (lang == PY || lang == CLOJURE) ? 1 : 2,
             body = i + marker;
      if ((lang == RUST || lang == C) && body < n &&
          (s[body] == '/' || s[body] == '!'))
        ++body;
      if (lang == CLOJURE)
        while (body < n && s[body] == ';')
          ++body;
      if (body < n && s[body] == ' ')
        ++body;
      size_t end = body;
      while (end < n && s[end] != '\n' && s[end] != '\r')
        ++end;
      buffer query = {0};
      append(&query, s + body, end - body);
      size_t tail = end;
      // Combine only comment lines that look like SQL continuations.
      while (tail < n) {
        size_t p = tail;
        if (s[p] == '\r')
          ++p;
        if (p < n && s[p] == '\n')
          ++p;
        while (p < n && (s[p] == ' ' || s[p] == '\t'))
          ++p;
        if (p + marker > n || memcmp(s + p, s + start, marker))
          break;
        p += marker;
        if ((lang == RUST || lang == C) && p < n &&
            (s[p] == '/' || s[p] == '!'))
          ++p;
        if (lang == CLOJURE)
          while (p < n && s[p] == ';')
            ++p;
        if (p < n && s[p] == ' ')
          ++p;
        if (p >= n || !continuation(s + p))
          break;
        size_t q = p;
        while (q < n && s[q] != '\n' && s[q] != '\r')
          ++q;
        str(&query, "\n");
        append(&query, s + p, q - p);
        tail = q;
      }
      char *formatted = query.bad ? NULL : format_sql(query.p, dialect);
      if (formatted) {
        size_t line_start = start;
        while (line_start && s[line_start - 1] != '\n' &&
               s[line_start - 1] != '\r')
          --line_start;
        size_t indent_n = 0;
        while (line_start + indent_n < start &&
               (s[line_start + indent_n] == ' ' ||
                s[line_start + indent_n] == '\t'))
          ++indent_n;
        append(&out, s + start, body - start);
        char *prefix = malloc(body - start + 1);
        if (!prefix) {
          free(formatted);
          free(query.p);
          goto fail;
        }
        memcpy(prefix, s + start, body - start);
        prefix[body - start] = 0;
        comment_lines(&out, formatted, s + line_start, indent_n, prefix,
                      s[end] == '\r' ? "\r\n" : "\n");
        free(prefix);
        free(formatted);
      } else
        append(&out, s + start, tail - start);
      free(query.p);
      i = tail;
      continue;
    }
    if (block) {
      const char *open = lang == HS ? "{-" : "/*",
                 *closemark = lang == HS ? "-}" : "*/";
      size_t end = i + 2;
      int depth = 1, nested = 0;
      for (; end + 1 < n; ++end) {
        if ((lang == HS || lang == RUST) && !memcmp(s + end, open, 2)) {
          ++depth;
          nested = 1;
          ++end;
        } else if (!memcmp(s + end, closemark, 2)) {
          if (!--depth)
            break;
          ++end;
        }
      }
      if (depth)
        goto fail;
      buffer query = {0};
      int stars = 0;
      for (size_t p = i + 2; p < end;) {
        size_t q = p;
        while (q < end && s[q] != '\n' && s[q] != '\r')
          ++q;
        size_t content = p;
        while (content < q && (s[content] == ' ' || s[content] == '\t'))
          ++content;
        if (content < q && s[content] == '*' && lang != HS) {
          stars = 1;
          ++content;
          if (content < q && s[content] == ' ')
            ++content;
        } else
          content = p;
        append(&query, s + content, q - content);
        if (q < end) {
          str(&query, "\n");
          if (s[q] == '\r' && q + 1 < end && s[q + 1] == '\n')
            ++q;
          ++q;
        }
        p = q;
      }
      char *formatted =
          (!nested && !query.bad) ? format_sql(query.p, dialect) : NULL;
      if (formatted) {
        str(&out, open);
        if (lang != HS && s[i + 2] == '*')
          str(&out, "*");
        str(&out, " ");
        if (stars)
          comment_lines(&out, formatted, "", 0, " * ", "\n");
        else
          str(&out, formatted);
        str(&out, " ");
        str(&out, closemark);
        free(formatted);
      } else
        append(&out, s + i, end + 2 - i);
      free(query.p);
      i = end + 2;
      continue;
    }
    if ((lang == JS || lang == TS) && s[i] == '/') {
      // Ambiguous slash expressions are opaque: never rewrite regex bodies.
      size_t p = i + 1;
      int bracket = 0;
      for (; p < n && s[p] != '\n' && s[p] != '\r'; ++p) {
        if (s[p] == '\\' && p + 1 < n) {
          ++p;
          continue;
        }
        if (s[p] == '[')
          bracket = 1;
        else if (s[p] == ']')
          bracket = 0;
        else if (s[p] == '/' && !bracket) {
          ++p;
          break;
        }
      }
      append(&out, s + i, p - i);
      i = p;
      continue;
    }
    if (lang == HS && s[i] == '[' && i + 1 < n &&
        isalpha((unsigned char)s[i + 1])) {
      size_t p = i + 1;
      while (p < n &&
             (isalnum((unsigned char)s[p]) || s[p] == '_' || s[p] == '.'))
        ++p;
      if (p < n && s[p] == '|') {
        const char *end = strstr(s + p + 1, "|]");
        if (!end)
          goto fail;
        size_t closing = (size_t)(end - s);
        buffer query = {0};
        append(&query, s + p + 1, closing - p - 1);
        if (query.bad) {
          free(query.p);
          goto fail;
        }
        char *formatted = format_sql(query.p, dialect);
        if (formatted && strstr(formatted, "|]")) {
          free(formatted);
          formatted = NULL;
        }
        append(&out, s + i, p + 1 - i);
        if (formatted) {
          size_t lead = 0, trail = query.n;
          while (isspace((unsigned char)query.p[lead]))
            ++lead;
          while (trail > lead && isspace((unsigned char)query.p[trail - 1]))
            --trail;
          append(&out, query.p, lead);
          str(&out, formatted);
          append(&out, query.p + trail, query.n - trail);
          free(formatted);
        } else
          append(&out, query.p, query.n);
        str(&out, "|]");
        free(query.p);
        i = closing + 2;
        continue;
      }
    }
    if (s[i] == '\'' &&
        ((lang == HS && i &&
          (isalnum((unsigned char)s[i - 1]) || (unsigned char)s[i - 1] >= 128 ||
           s[i - 1] == '_')) ||
         ((lang == RUST || lang == HS) && i + 1 < n &&
          (isalpha((unsigned char)s[i + 1]) || (unsigned char)s[i + 1] >= 128 ||
           s[i + 1] == '_') &&
          !quoted_character(s, n, i)))) {
      append(&out, s + i, 1);
      ++i;
      continue;
    }
    if (lang == CLOJURE && s[i] == '\\') {
      size_t p = i + 1;
      if (p < n)
        ++p;
      while (p < n && !isspace((unsigned char)s[p]) && s[p] != ')' &&
             s[p] != ']' && s[p] != '}')
        ++p;
      append(&out, s + i, p - i);
      i = p;
      continue;
    }
    if (s[i] == '"' || (s[i] == '\'' && lang != CLOJURE) ||
        (s[i] == '`' && (lang == GO || lang == JS || lang == TS))) {
      literal first, last;
      if (!scan_literal(s, n, i, lang, &first))
        goto fail;
      last = first;
      buffer query = {0};
      int safe = !first.skip && decode(s, first, &query);
      // Only C adjacent strings and Go's literal + literal concatenation are
      // joined.
      for (;;) {
        size_t p = last.tail;
        while (p < n && isspace((unsigned char)s[p]))
          ++p;
        if (lang == C)
          while (p + 1 < n && s[p] == '\\' &&
                 (s[p + 1] == '\n' || s[p + 1] == '\r')) {
            ++p;
            while (p < n && isspace((unsigned char)s[p]))
              ++p;
          }
        if ((lang == GO || lang == JS || lang == TS || lang == HS) && p < n &&
            s[p] == '+') {
          if (lang == HS && (p + 1 == n || s[p + 1] != '+'))
            break;
          if (lang == HS)
            ++p;
          ++p;
          while (p < n && isspace((unsigned char)s[p]))
            ++p;
        } else if (lang != C && lang != PY)
          break;
        if (p >= n || (s[p] != '"' && !(lang == GO && s[p] == '`')))
          break;
        literal next;
        if (!scan_literal(s, n, p, lang, &next))
          goto literal_fail;
        if (first.raw != next.raw || first.quote != next.quote) {
          safe = 0;
        }
        if (!decode(s, next, &query))
          safe = 0;
        last = next;
      }
      {
        size_t p = last.tail;
        while (p < n && isspace((unsigned char)s[p]))
          ++p;
        if (p < n && s[p] == '+')
          safe = 0;
        if (safe && (strchr(query.p, '%') || strchr(query.p, '{') ||
                     strchr(query.p, '}'))) {
          size_t begin = i > 1024 ? i - 1024 : 0;
          buffer context = {0};
          append(&context, s + begin, i - begin);
          if (context.bad)
            safe = 0;
          else {
            if (strchr(query.p, '%') &&
                (strstr(context.p, "printf") || strstr(context.p, "Printf") ||
                 strstr(context.p, "format(")))
              safe = 0;
            if (lang == RUST &&
                (strchr(query.p, '{') || strchr(query.p, '}'))) {
              const char *macros[] = {
                  "format!", "format_args!", "write!", "writeln!",
                  "print!",  "println!",     "panic!", NULL};
              for (size_t j = 0; macros[j]; ++j)
                if (strstr(context.p, macros[j]))
                  safe = 0;
            }
            if (lang == C && (strstr(context.p, "fmt::format") ||
                              strstr(context.p, "std::format")))
              safe = 0;
          }
          free(context.p);
        }
        if (safe && lang == PY &&
            ((p < n && s[p] == '%') ||
             (p + 7 < n && !memcmp(s + p, ".format", 7))))
          safe = 0;
        char *formatted =
            safe && !query.bad ? format_sql(query.p, dialect) : NULL;
        if (formatted && first.cppraw) {
          size_t length = first.tail - first.end;
          char *closing = malloc(length + 1);
          if (!closing) {
            free(formatted);
            free(query.p);
            goto fail;
          }
          memcpy(closing, s + first.end, length);
          closing[length] = 0;
          if (strstr(formatted, closing)) {
            free(formatted);
            formatted = NULL;
          }
          free(closing);
        }
        if (formatted && !first.cppraw && (first.triple || first.raw) &&
            strstr(formatted, first.triple
                                  ? (first.quote == '"' ? "\"\"\"" : "'''")
                                  : "`")) {
          free(formatted);
          formatted = NULL;
        }
        if (formatted) {
          append(&out, s + i, first.body - i);
          // Preserve leading/trailing whitespace at fragment boundaries.
          size_t lead = 0;
          while (isspace((unsigned char)query.p[lead]))
            ++lead;
          size_t qn = query.n, trail = qn;
          while (trail > lead && isspace((unsigned char)query.p[trail - 1]))
            --trail;
          buffer value = {0};
          append(&value, query.p, lead);
          str(&value, formatted);
          append(&value, query.p + trail, qn - trail);
          if (value.bad) {
            free(value.p);
            free(formatted);
            free(query.p);
            goto fail;
          }
          encode(&out, value.p, first, lang);
          append(&out, s + first.end, first.tail - first.end);
          free(value.p);
          free(formatted);
        } else
          append(&out, s + i, last.tail - i);
        i = last.tail;
        free(query.p);
        continue;
      }
    literal_fail:
      free(query.p);
      goto fail;
    }
    // Do not interpret quotes inside C character literals or JS regex literals
    // as independent SQL. Single quotes above are copied without formatting in
    // C.
    append(&out, s + i, 1);
    ++i;
  }
  if (out.bad)
    goto fail;
  if (!out.p)
    str(&out, "");
  return out.p;
fail:
  free(out.p);
  return NULL;
}
