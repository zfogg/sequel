/** @file sequel.h
 * @brief SQL parsing, metadata, formatting, sanitization, diagnostics and highlighting.
 *
 * All input is UTF-8. No SQL is executed. Buffers and options are caller-owned;
 * formatting and highlighting preserve potentially sensitive SQL contents.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <sequel/dialect.h>
#include <sequel/diagnostics.h>
#include <sequel/formatter.h>
#include <sequel/fingerprint.h>
#include <sequel/summary.h>
#include <sequel/sanitizer.h>
#include <sequel/binds.h>
#include <sequel/analysis.h>
#include <sequel/lexer.h>
#include <sequel/limits.h>
#include <sequel/parser.h>
#include <sequel/query.h>
#include <sequel/source.h>
#include <sequel/serialization.h>
#include <sequel/status.h>
#include <sequel/syntax.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Source range, completion state and explanations for a lexical/delimiter issue. */
/** Return a stable lowercase code name, or NULL for an unknown code. */
const char *sequel_diagnostic_code_name(sequel_diagnostic_code_t code);

/** Reports the first lexical/delimiter issue. Not a SQL grammar validator and
 * never executes SQL. Incomplete input returns INVALID_ARGUMENT with a warning.
 * statement_complete requires balanced delimiters and a final semicolon outside
 * quotes/comments. CRLF counts as one newline; tabs count as one column.
 * _n accepts non-NUL-terminated buffers and reports embedded NULs explicitly. */
sequel_status_t sequel_diagnose(const char *sql, sequel_dialect_t dialect, sequel_diagnostic_t *diagnostic);

/** Diagnose an explicit byte length, including embedded NULs. @see sequel_diagnose */
sequel_status_t sequel_diagnose_n(const char *sql, size_t length, sequel_dialect_t dialect, sequel_diagnostic_t *diagnostic);

/** Convert one recovered syntax issue into the canonical diagnostic ABI. */
sequel_status_t sequel_syntax_diagnostic(const sequel_syntax_document_t *document, size_t issue_index, sequel_diagnostic_t *diagnostic);

/** Analyze explicit UTF-8 bytes; embedded NUL is diagnosed. Safe statement-boundary
 * recovery collects multiple issues. Any malformed statement fails closed for the
 * entire document. Cancellation and budgets clear partial exported data.
 * Caller supplies out; free previous storage before reuse. On failure inspect
 * status/diagnostics; this API never throws or exports partial raw SQL. */
sequel_status_t sequel_analyze_n(const char *sql, size_t length, const sequel_analysis_options_t *options, sequel_analysis_t *out);

/** NUL-terminated counterpart of sequel_analyze_n. Length discovery also polls
 * cancellation and consumes work budget. A rejected/cancelled scan can report
 * only the discovered prefix length; use _n when the full length is known. */
sequel_status_t sequel_analyze(const char *sql, const sequel_analysis_options_t *options, sequel_analysis_t *out);

/** Release analysis storage and reset; NULL and repeated free are allowed. */
void sequel_analysis_free(sequel_analysis_t *out);

/** SHA-256 of the versioned/dialect length-framed canonical token stream. Literal
 * values/comments/whitespace and bind spelling are excluded; quoted identifiers,
 * operators, token boundaries and repeated bind identity are retained. Generic
 * and SQLite/PostgreSQL/MySQL/ClickHouse/DuckDB have distinct namespaces. SHA-256
 * collisions are possible but cryptographically unlikely. out requires 96 bytes. */
sequel_status_t sequel_fingerprint(const char *sql, sequel_dialect_t dialect, char out[96]);

/** Inspection policy. Initialize to zero to use generic SQL and default limits. */
typedef struct {
  sequel_dialect_t dialect;
  /** Zero uses SEQUEL_TEXT_MAX. Values above SEQUEL_TEXT_MAX are rejected. */
  size_t max_query_text_bytes;
  bool omit_query_text;
  bool keep_in_lists;
} sequel_metadata_options_t;

/** Bounded, sanitized query text and structural metadata from sequel_metadata.
 * Text is allocated to fit the actual result, not the maximum limits.
 * Do not copy this owning struct or modify its pointers. Release it with
 * sequel_metadata_free before reuse, including after a failed metadata extraction. */
typedef struct {
  const char *text;
  const char *summary;
  char operation[32];
  bool truncated;
  size_t statement_count;
  /** Internal allocation; callers must not access or free it directly. */
  void *_storage;
} sequel_metadata_t;

/** Release metadata storage and reset metadata. NULL is allowed. Only call
 * on a zero-initialized struct or one initialized by sequel_metadata. Safe to repeat. */
void sequel_metadata_free(sequel_metadata_t *metadata);

/** Dialect-independent metadata extraction, usable without SQLite. Never reads bind
 * values. Malformed quoting fails closed: text is empty and summary is SQL.
 * Input is limited to SEQUEL_INPUT_MAX and nesting to 64 levels. Output is UTF-8
 * bounded. out need not be initialized, but any previous result must be freed. */
sequel_status_t sequel_metadata(const char *sql, const sequel_metadata_options_t *options, sequel_metadata_t *out);

/** Length-aware metadata extraction for caller-owned UTF-8 bytes. Embedded NUL bytes are rejected. */
sequel_status_t sequel_metadata_n(const char *sql, size_t length, const sequel_metadata_options_t *options, sequel_metadata_t *out);

/** Sanitizes into a caller-owned buffer. capacity includes the NUL
 * terminator; limit is the maximum emitted bytes (zero uses SEQUEL_TEXT_MAX).
 * Capacity need only fit the actual output or its truncated prefix plus NUL;
 * insufficient capacity returns SEQUEL_ERR_OVERFLOW without partial output. */
sequel_status_t sequel_sanitize(const char *sql, sequel_dialect_t dialect, bool collapse_in_lists, size_t limit, char *out, size_t capacity, bool *truncated);

/** Independent transformations. Retained comments and literals may contain secrets.
 * Zero flags preserves lexical contents while normalizing outside whitespace.
 * IN-list collapsing applies only when replacing literals. */
#define SEQUEL_REMOVE_COMMENTS 1u
#define SEQUEL_REPLACE_LITERALS 2u
#define SEQUEL_SANITIZE_ALL (SEQUEL_REMOVE_COMMENTS | SEQUEL_REPLACE_LITERALS)

/** Sanitize with independently selected comment removal and literal replacement. */
sequel_status_t sequel_sanitize_with_flags(const char *sql, sequel_dialect_t dialect, unsigned flags, bool collapse_in_lists, size_t limit, char *out, size_t capacity, bool *truncated);

/** Layout-only SQL formatting. Comments, literals, identifiers and bind markers
 * are preserved verbatim. No SQL is executed or sanitized. Input is UTF-8,
 * limited to SEQUEL_INPUT_MAX / 64 nesting levels. Not a SQL validator.
 * length receives the output byte count (excluding NUL). Pass out=NULL and
 * capacity=0 to measure. On failure a supplied output buffer is emptied. */
sequel_status_t sequel_format(const char *sql, sequel_dialect_t dialect, char *out, size_t capacity, size_t *length);

/** Format with a structured diagnostic; NULL diagnostic is permitted. @see sequel_format */
sequel_status_t sequel_format_ex(const char *sql, sequel_dialect_t dialect, char *out, size_t capacity, size_t *length, sequel_diagnostic_t *diagnostic);

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
sequel_status_t sequel_colorscheme_for_appearance(const char *name, sequel_appearance_t appearance, sequel_colorscheme_t *out);

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
sequel_status_t sequel_highlight_set_colorscheme(sequel_highlight_options_t *options, const sequel_colorscheme_t *scheme);

/** Named types match the enum suffixes in lowercase. Hex is `#RRGGBB` or `RRGGBB`. */
const char *sequel_highlight_color_name(sequel_highlight_color_t type);

/** Override a token or wrapper color. @see sequel_highlight_color_name */
sequel_status_t sequel_highlight_set_color(sequel_highlight_options_t *options, const char *type, const char *hex);

/** Preserve SQL spelling and whitespace. HTML is an escaped `<pre><code>` fragment
 * with inline colors and sequel-* token classes. ANSI uses truecolor SGR and a
 * final reset. Does not format, sanitize, execute SQL, or validate its grammar.
 * Rejects invalid UTF-8, terminal control bytes other than TAB/CR/LF, malformed
 * quotes/comments (unless tolerant), >SEQUEL_INPUT_MAX input. Unbalanced
 * groups are allowed; use sequel_diagnose to check delimiter structure.
 * NULL options uses defaults. out=NULL/capacity=0 measures; length excludes NUL.
 * Insufficient capacity reports required length and empties out. Other errors
 * also empty out. Input and output buffers must not overlap. */
sequel_status_t sequel_highlight(const char *sql, const sequel_highlight_options_t *options, char *out, size_t capacity, size_t *length);

/** Tolerant success may still return an incomplete warning in diagnostic. */
sequel_status_t sequel_highlight_ex(const char *sql, const sequel_highlight_options_t *options, char *out, size_t capacity, size_t *length, sequel_diagnostic_t *diagnostic);

#ifdef __cplusplus
}
#endif
