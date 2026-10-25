#ifndef SEQUEL_H
#define SEQUEL_H
#include <stdbool.h>
#include <stddef.h>
typedef enum {
  SEQUEL_OK = 0,
  SEQUEL_ERR_NULL,
  SEQUEL_ERR_INVALID_ARGUMENT,
  SEQUEL_ERR_OVERFLOW,
  SEQUEL_ERR_NOMEM
} sequel_status_t;
#ifdef __cplusplus
extern "C" {
#endif
#define SEQUEL_TEXT_MAX 32768
#define SEQUEL_SUMMARY_MAX 32768
typedef enum {
  SEQUEL_GENERIC,
  SEQUEL_SQLITE,
  SEQUEL_POSTGRESQL,
  SEQUEL_DUCKDB,
  SEQUEL_MYSQL,
  SEQUEL_CLICKHOUSE
} sequel_dialect_t;
typedef struct {
  sequel_dialect_t dialect;
  /* Zero uses 32768. Values above 32768 are rejected. */
  size_t max_query_text_bytes;
  bool omit_query_text;
  bool keep_in_lists;
} sequel_options_t;
typedef struct {
  char text[SEQUEL_TEXT_MAX + 1];
  char summary[SEQUEL_SUMMARY_MAX + 1];
  char operation[32];
  bool truncated;
  size_t statement_count;
} sequel_metadata_t;
/* Dialect-independent inspection, usable without SQLite. Never reads bind
 * values. Malformed quoting fails closed: text is empty and summary is SQL.
 * Input is limited to 1 MiB and nesting to 64 levels. Output is UTF-8 bounded. */
sequel_status_t sequel_inspect(const char *sql, const sequel_options_t *options, sequel_metadata_t *out);
/* Sanitizes without the fixed metadata text cap. capacity includes the NUL
 * terminator; limit is the maximum emitted bytes (zero uses 32768). */
sequel_status_t sequel_sanitize(const char *sql, sequel_dialect_t dialect, bool collapse_in_lists, size_t limit,
                                char *out, size_t capacity, bool *truncated);
/* Independent transformations. Retained comments and literals may contain secrets.
 * Zero flags preserves lexical contents while normalizing outside whitespace.
 * IN-list collapsing applies only when replacing literals. */
#define SEQUEL_REMOVE_COMMENTS 1u
#define SEQUEL_REPLACE_LITERALS 2u
#define SEQUEL_SANITIZE_ALL (SEQUEL_REMOVE_COMMENTS | SEQUEL_REPLACE_LITERALS)
sequel_status_t sequel_sanitize_with_flags(const char *sql, sequel_dialect_t dialect, unsigned flags,
                                           bool collapse_in_lists, size_t limit, char *out, size_t capacity,
                                           bool *truncated);

/* Layout-only SQL formatting. Comments, literals, identifiers and bind markers
 * are preserved verbatim. No SQL is executed or sanitized. Input is UTF-8,
 * limited to 1 MiB / 65536 tokens / 64 nesting levels. Not a SQL validator.
 * length receives the output byte count (excluding NUL). Pass out=NULL and
 * capacity=0 to measure. On failure a supplied output buffer is emptied. */
sequel_status_t sequel_format(const char *sql, sequel_dialect_t dialect, char *out, size_t capacity, size_t *length);
/* Collapse horizontal token gaps while preserving comments, literals and newlines. */
sequel_status_t sequel_compact(const char *sql, sequel_dialect_t dialect, char *out, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
