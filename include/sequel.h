#ifndef SEQUEL_H
#define SEQUEL_H
/** @file sequel.h
 * @brief SQL formatting, sanitization, inspection, diagnostics and highlighting.
 *
 * All input is UTF-8. No SQL is executed. Buffers and options are caller-owned;
 * formatting and highlighting preserve potentially sensitive SQL contents.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/** Status returned by operations; SEQUEL_OK indicates success. */
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
/** Dialect selects quote, identifier, comment and bind-marker rules. */
typedef enum {
  SEQUEL_GENERIC,
  SEQUEL_SQLITE,
  SEQUEL_POSTGRESQL,
  SEQUEL_DUCKDB,
  SEQUEL_MYSQL,
  SEQUEL_CLICKHOUSE
} sequel_dialect_t;

/** Stable diagnostic categories; use sequel_diagnostic_code_name for text. */
typedef enum {
  SEQUEL_DIAG_NONE,
  SEQUEL_DIAG_NULL_INPUT,
  SEQUEL_DIAG_INVALID_ARGUMENT,
  SEQUEL_DIAG_INPUT_LIMIT,
  SEQUEL_DIAG_INVALID_UTF8,
  SEQUEL_DIAG_CONTROL_CHARACTER,
  SEQUEL_DIAG_TOKEN_LIMIT,
  SEQUEL_DIAG_NESTING_LIMIT,
  SEQUEL_DIAG_UNTERMINATED_STRING,
  SEQUEL_DIAG_UNTERMINATED_IDENTIFIER,
  SEQUEL_DIAG_UNTERMINATED_COMMENT,
  SEQUEL_DIAG_UNTERMINATED_PARAMETER,
  SEQUEL_DIAG_INVALID_PARAMETER,
  SEQUEL_DIAG_UNMATCHED_CLOSE,
  SEQUEL_DIAG_UNCLOSED_GROUP,
  SEQUEL_DIAG_UNSUPPORTED_PAYLOAD,
  SEQUEL_DIAG_UNSUPPORTED_LAYOUT,
  SEQUEL_DIAG_OUTPUT_LIMIT,
  SEQUEL_DIAG_OUT_OF_MEMORY
} sequel_diagnostic_code_t;
/** Diagnostic severity; incomplete SQL is a warning. */
typedef enum { SEQUEL_NOTE, SEQUEL_WARNING, SEQUEL_ERROR } sequel_severity_t;
/** Source range, completion state and explanations for a lexical/delimiter issue. */
typedef struct {
  sequel_diagnostic_code_t code;
  sequel_severity_t severity;
  size_t offset, end_offset;                 /** zero-based UTF-8 bytes, end exclusive */
  size_t line, column, end_line, end_column; /** one-based Unicode scalar columns */
  bool incomplete, statement_complete;
  char message[128], explanation[384], hint[256];
} sequel_diagnostic_t;
/** Return a stable lowercase code name, or NULL for an unknown code. */
const char *sequel_diagnostic_code_name(sequel_diagnostic_code_t code);
/** Reports the first lexical/delimiter issue. Not a SQL grammar validator and
 * never executes SQL. Incomplete input returns INVALID_ARGUMENT with a warning.
 * statement_complete requires balanced delimiters and a final semicolon outside
 * quotes/comments. CRLF counts as one newline; tabs count as one column.
 * _n accepts non-NUL-terminated buffers and reports embedded NULs explicitly. */
sequel_status_t sequel_diagnose(const char *sql, sequel_dialect_t dialect, sequel_diagnostic_t *diagnostic);
/** Diagnose an explicit byte length, including embedded NULs. @see sequel_diagnose */
sequel_status_t sequel_diagnose_n(const char *sql, size_t length, sequel_dialect_t dialect,
                                  sequel_diagnostic_t *diagnostic);
/** Inspection policy. Initialize to zero to use generic SQL and default limits. */
typedef struct {
  sequel_dialect_t dialect;
  /** Zero uses 32768. Values above 32768 are rejected. */
  size_t max_query_text_bytes;
  bool omit_query_text;
  bool keep_in_lists;
} sequel_options_t;
/** Bounded, sanitized query text and structural metadata from sequel_inspect. */
typedef struct {
  char text[SEQUEL_TEXT_MAX + 1];
  char summary[SEQUEL_SUMMARY_MAX + 1];
  char operation[32];
  bool truncated;
  size_t statement_count;
} sequel_metadata_t;
/** Dialect-independent inspection, usable without SQLite. Never reads bind
 * values. Malformed quoting fails closed: text is empty and summary is SQL.
 * Input is limited to 1 MiB and nesting to 64 levels. Output is UTF-8 bounded. */
sequel_status_t sequel_inspect(const char *sql, const sequel_options_t *options, sequel_metadata_t *out);
/** Sanitizes without the fixed metadata text cap. capacity includes the NUL
 * terminator; limit is the maximum emitted bytes (zero uses 32768). */
sequel_status_t sequel_sanitize(const char *sql, sequel_dialect_t dialect, bool collapse_in_lists, size_t limit,
                                char *out, size_t capacity, bool *truncated);
/** Independent transformations. Retained comments and literals may contain secrets.
 * Zero flags preserves lexical contents while normalizing outside whitespace.
 * IN-list collapsing applies only when replacing literals. */
#define SEQUEL_REMOVE_COMMENTS 1u
#define SEQUEL_REPLACE_LITERALS 2u
#define SEQUEL_SANITIZE_ALL (SEQUEL_REMOVE_COMMENTS | SEQUEL_REPLACE_LITERALS)
sequel_status_t sequel_sanitize_with_flags(const char *sql, sequel_dialect_t dialect, unsigned flags,
                                           bool collapse_in_lists, size_t limit, char *out, size_t capacity,
                                           bool *truncated);

/** Layout-only SQL formatting. Comments, literals, identifiers and bind markers
 * are preserved verbatim. No SQL is executed or sanitized. Input is UTF-8,
 * limited to 1 MiB / 65536 tokens / 64 nesting levels. Not a SQL validator.
 * length receives the output byte count (excluding NUL). Pass out=NULL and
 * capacity=0 to measure. On failure a supplied output buffer is emptied. */
sequel_status_t sequel_format(const char *sql, sequel_dialect_t dialect, char *out, size_t capacity, size_t *length);
/** Format with a structured diagnostic; NULL diagnostic is permitted. @see sequel_format */
sequel_status_t sequel_format_ex(const char *sql, sequel_dialect_t dialect, char *out, size_t capacity, size_t *length,
                                 sequel_diagnostic_t *diagnostic);
/** Collapse horizontal token gaps while preserving comments, literals and newlines. */
sequel_status_t sequel_compact(const char *sql, sequel_dialect_t dialect, char *out, size_t capacity);

/** Highlight output encoding: escaped HTML fragment or truecolor ANSI. */
typedef enum { SEQUEL_HTML, SEQUEL_ANSI } sequel_highlight_mode_t;
/** Token and wrapper colors, mapped from Base16 with optional overrides. */
typedef enum {
  SEQUEL_COLOR_FOREGROUND,
  SEQUEL_COLOR_BACKGROUND,
  SEQUEL_COLOR_KEYWORD,
  SEQUEL_COLOR_TYPE,
  SEQUEL_COLOR_FUNCTION,
  SEQUEL_COLOR_VARIABLE,
  SEQUEL_COLOR_STRING,
  SEQUEL_COLOR_NUMBER,
  SEQUEL_COLOR_BOOLEAN,
  SEQUEL_COLOR_NULL,
  SEQUEL_COLOR_COMMENT,
  SEQUEL_COLOR_OPERATOR,
  SEQUEL_COLOR_PUNCTUATION,
  SEQUEL_COLOR_IDENTIFIER,
  SEQUEL_COLOR_COUNT
} sequel_highlight_color_t;

/** Base16 base00 through base0F, stored as 24-bit RGB (0xRRGGBB). */
typedef struct {
  uint32_t base[16];
} sequel_colorscheme_t;
/** Preserve a palette or request its actual published light/dark counterpart. */
typedef enum { SEQUEL_APPEARANCE_AUTO, SEQUEL_APPEARANCE_DARK, SEQUEL_APPEARANCE_LIGHT } sequel_appearance_t;
/** Select actual published variants; never derive or invert colors. A missing
 * counterpart returns INVALID_ARGUMENT, leaving out/scheme unchanged. AUTO
 * preserves the selected palette. variant_name returns NULL if unavailable.
 * set_appearance recognizes bundled palettes by their colors. A custom palette
 * without a known counterpart can only retain its natural appearance (inferred
 * from foreground/background luminance). Use for_appearance for named palettes.
 * Apply before sequel_highlight_set_colorscheme and token overrides. */
const char *sequel_colorscheme_variant_name(const char *name, sequel_appearance_t appearance);
/** Load the selected published variant. @see sequel_colorscheme_variant_name */
sequel_status_t sequel_colorscheme_for_appearance(const char *name, sequel_appearance_t appearance,
                                                  sequel_colorscheme_t *out);
/** Switch a recognized palette to its counterpart. @see sequel_colorscheme_variant_name */
sequel_status_t sequel_colorscheme_set_appearance(sequel_colorscheme_t *scheme, sequel_appearance_t appearance);
/** Highlight configuration; initialize with sequel_highlight_options_init. */
typedef struct {
  sequel_dialect_t dialect;
  sequel_highlight_mode_t mode;
  uint32_t colors[SEQUEL_COLOR_COUNT];
  bool tolerant; /** Highlight unfinished strings/comments/identifiers/parameters. */
} sequel_highlight_options_t;

/** Immutable built-in palette registry. Invalid indices return NULL. */
size_t sequel_colorscheme_count(void);
/** Return the built-in name at index, or NULL when out of range. */
const char *sequel_colorscheme_name(size_t index);
/** Load a built-in Base16 palette by name. Failure leaves out unchanged. */
sequel_status_t sequel_colorscheme(const char *name, sequel_colorscheme_t *out);
/** Read any legacy or current Base16 YAML palette, without filesystem/network I/O.
 * All sixteen baseXX keys must occur exactly once and contain six hex digits.
 * Unrelated metadata is ignored. Failure leaves out unchanged. */
sequel_status_t sequel_colorscheme_parse(const char *yaml, sequel_colorscheme_t *out);
/** Defaults: generic SQL, HTML, default-dark. Applying a scheme resets overrides. */
void sequel_highlight_options_init(sequel_highlight_options_t *options);
/** Apply a palette, resetting token overrides; apply explicit colors afterward. */
/** Override a token or wrapper color. @see sequel_highlight_color_name */
sequel_status_t sequel_highlight_set_colorscheme(sequel_highlight_options_t *options,
                                                 const sequel_colorscheme_t *scheme);
/** Named types match the enum suffixes in lowercase. Hex is `#RRGGBB` or `RRGGBB`. */
const char *sequel_highlight_color_name(sequel_highlight_color_t type);
/** Override a token or wrapper color. @see sequel_highlight_color_name */
sequel_status_t sequel_highlight_set_color(sequel_highlight_options_t *options, const char *type, const char *hex);
/** Preserve SQL spelling and whitespace. HTML is an escaped `<pre><code>` fragment
 * with inline colors and sequel-* token classes. ANSI uses truecolor SGR and a
 * final reset. Does not format, sanitize, execute SQL, or validate its grammar.
 * Rejects invalid UTF-8, terminal control bytes other than TAB/CR/LF, malformed
 * quotes/comments (unless tolerant), >1 MiB input, or >65536 tokens. Unbalanced
 * groups are allowed; use sequel_diagnose to check delimiter structure.
 * NULL options uses defaults. out=NULL/capacity=0 measures; length excludes NUL.
 * Insufficient capacity reports required length and empties out. Other errors
 * also empty out. Input and output buffers must not overlap. */
sequel_status_t sequel_highlight(const char *sql, const sequel_highlight_options_t *options, char *out, size_t capacity,
                                 size_t *length);
/** Tolerant success may still return an incomplete warning in diagnostic. */
sequel_status_t sequel_highlight_ex(const char *sql, const sequel_highlight_options_t *options, char *out,
                                    size_t capacity, size_t *length, sequel_diagnostic_t *diagnostic);
#ifdef __cplusplus
}
#endif
#endif
