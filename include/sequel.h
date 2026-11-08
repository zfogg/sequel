#pragma once

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
  SEQUEL_ERR_NOMEM,
  SEQUEL_ERR_LIMIT,
  SEQUEL_ERR_CANCELLED
} sequel_status_t;

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum UTF-8 SQL input length, in bytes (1 GiB). */
#ifndef SEQUEL_INPUT_MAX
#define SEQUEL_INPUT_MAX ((size_t)1024 * 1024 * 1024)
#endif

/** Default maximum sanitized query text length, in bytes (1 GiB). */
#ifndef SEQUEL_TEXT_MAX
#define SEQUEL_TEXT_MAX ((size_t)1024 * 1024 * 1024)
#endif

/** Maximum summary length, in bytes (1 GiB). */
#ifndef SEQUEL_SUMMARY_MAX
#define SEQUEL_SUMMARY_MAX ((size_t)1024 * 1024 * 1024)
#endif

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
  SEQUEL_DIAG_TOKEN_LIMIT, /** Reserved for compatibility; no longer emitted. */
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
  SEQUEL_DIAG_OUT_OF_MEMORY,
  SEQUEL_DIAG_RESOURCE_LIMIT,
  SEQUEL_DIAG_CANCELLED,
  SEQUEL_DIAG_AMBIGUOUS_LITERAL,
  SEQUEL_DIAG_EMPTY_STATEMENT,
  SEQUEL_DIAG_MULTIPLE_STATEMENTS,
  SEQUEL_DIAG_MIXED_OPERATIONS,
  SEQUEL_DIAG_SELECT_STAR,
  SEQUEL_DIAG_UNBOUNDED_WRITE,
  SEQUEL_DIAG_LARGE_IN_LIST,
  SEQUEL_DIAG_DEEP_QUERY_NESTING,
  SEQUEL_DIAG_EXCESSIVE_JOINS,
  SEQUEL_DIAG_REPEATED_PREDICATE,
  SEQUEL_DIAG_UNKNOWN_OPERATION,
  SEQUEL_DIAG_NO_REFERENCED_TABLE,
  SEQUEL_DIAG_DIALECT_PORTABILITY,
  SEQUEL_DIAG_SQLC_ANNOTATION,
  SEQUEL_DIAG_BIND_MISSING,
  SEQUEL_DIAG_BIND_EXTRA,
  SEQUEL_DIAG_BIND_INVALID
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

/** Analysis/diagnostic wire schema version. Unknown versions must be rejected. */
#define SEQUEL_ANALYSIS_VERSION 1u

/** Diagnostic catalog/range wire schema version; currently 1. */
#define SEQUEL_DIAGNOSTIC_VERSION 1u

/** A zero-based half-open UTF-8 byte range. */
typedef struct {
  size_t offset;     /**< Inclusive starting byte. */
  size_t end_offset; /**< Exclusive ending byte. */
} sequel_range_t;

/** Mapping provenance: copied bytes, replaced bytes, deleted bytes or inserted layout. */
typedef enum { SEQUEL_MAP_COPY, SEQUEL_MAP_REPLACE, SEQUEL_MAP_DELETE, SEQUEL_MAP_INSERT } sequel_map_kind_t;

/** Numeric provenance only; never contains source text. */
typedef struct {
  size_t from_start;      /**< Inclusive source byte. */
  size_t from_end;        /**< Exclusive source byte. */
  size_t to_start;        /**< Inclusive destination byte. */
  size_t to_end;          /**< Exclusive destination byte. */
  sequel_map_kind_t kind; /**< Provenance for this entire segment. */
} sequel_map_segment_t;

/** A monotone complete map between two UTF-8 documents. */
typedef struct {
  size_t from_length;                   /**< Full source length in UTF-8 bytes. */
  size_t to_length;                     /**< Full destination length in UTF-8 bytes. */
  const sequel_map_segment_t *segments; /**< Immutable numeric segments; owned by analysis. */
  size_t count;                         /**< Number of segments; zero for an omitted map. */
} sequel_source_map_t;

/** Mapped range; exact is false across replacements, deletions or inserted layout. */
typedef struct {
  size_t start; /**< Inclusive mapped byte boundary. */
  size_t end;   /**< Exclusive mapped byte boundary, possibly equal to start. */
  bool exact;   /**< False if mapping touched replaced/deleted/inserted bytes. */
} sequel_mapped_range_t;

/** Map a byte range in either direction. Deleted ranges map to empty anchors.
 * Replacement interiors map to the entire replacement, never to original values.
 * Returns INVALID_ARGUMENT for invalid/incomplete maps or out-of-bounds ranges. */
sequel_status_t sequel_source_map_range(const sequel_source_map_t *map, size_t start, size_t end, bool reverse,
                                        sequel_mapped_range_t *out);

/** Convert a UTF-8 byte boundary to global UTF-16 code units; reject split scalars.
 * CR and LF each count one unit. No text is retained. */
sequel_status_t sequel_utf8_to_utf16(const char *text, size_t length, size_t offset, size_t *units);

/** Convert global UTF-16 units to UTF-8 bytes; reject split surrogate pairs. */
sequel_status_t sequel_utf16_to_utf8(const char *text, size_t length, size_t units, size_t *offset);

/** Number of immutable public diagnostic catalog entries, including none. */
size_t sequel_diagnostic_catalog_count(void);

/** Copy canonical code/severity/prose from the catalog; index is the numeric code. */
sequel_status_t sequel_diagnostic_catalog_get(size_t index, sequel_diagnostic_t *out);

/** Validate untrusted version/ranges/types and replace prose with canonical catalog
 * strings. Does not accept unknown codes/versions. Columns are Unicode scalars.
 * This validates transport structure, not correspondence to unavailable SQL. */
sequel_status_t sequel_diagnostic_canonicalize(unsigned version, size_t source_length, const sequel_diagnostic_t *input,
                                               sequel_diagnostic_t *out);

/** Bind syntax classification; descriptors never contain names, types or values. */
typedef enum { SEQUEL_BIND_POSITIONAL, SEQUEL_BIND_NUMBERED, SEQUEL_BIND_NAMED, SEQUEL_BIND_TYPED } sequel_bind_style_t;

/** Ordinal is one-based occurrence order; slot is one-based identity within a
 * zero-based statement_index. Repeated markers share a slot; anonymous markers
 * allocate a new slot. offset/end_offset refer to original UTF-8 bytes. */
typedef struct {
  size_t ordinal;            /**< One-based occurrence order across the document. */
  size_t slot;               /**< One-based driver slot, including explicit numbered gaps. */
  size_t statement_index;    /**< Zero-based index into analysis statements. */
  size_t offset;             /**< Inclusive original UTF-8 byte. */
  size_t end_offset;         /**< Exclusive original UTF-8 byte. */
  sequel_bind_style_t style; /**< Marker syntax only; never its name or value. */
} sequel_bind_descriptor_t;

/** Check supplied arity for one statement without reading values. Arity equals
 * the maximum driver slot, so $3 requires 3 supplied slots even if used once. */
sequel_status_t sequel_check_binds(const sequel_bind_descriptor_t *binds, size_t count, size_t statement_index,
                                   size_t supplied_slots);

/** Validate bind arity and return a canonical diagnostic on mismatch. */
sequel_status_t sequel_check_binds_diagnostic(const sequel_bind_descriptor_t *binds, size_t count,
                                              size_t statement_index, size_t supplied_slots,
                                              sequel_diagnostic_t *diagnostic);

/** Cancellation callback: return true to stop; must not re-enter this analysis. */
typedef bool (*sequel_cancel_fn)(void *userdata);

/** Unified analysis policy. Zero input/output/memory limits select 1 GiB; zero
 * work limit is unlimited. Work units count scan/transform steps. Memory includes
 * live native intermediates. Output includes strings and numeric result arrays.
 * Input/output limits above the compile-time ceilings are invalid. */
typedef struct {
  sequel_dialect_t dialect;    /**< Zero selects generic SQL. */
  size_t max_input_bytes;      /**< UTF-8 input ceiling; zero uses SEQUEL_INPUT_MAX. */
  size_t max_output_bytes;     /**< Generated string/array byte budget; zero uses SEQUEL_TEXT_MAX. */
  size_t max_memory_bytes;     /**< Live native requested allocation bytes, including headers; zero uses 1 GiB. */
  size_t max_work_units;       /**< Counted scan/hash/copy/transform steps; zero is unlimited. */
  size_t max_query_text_bytes; /**< Independent UTF-8 display prefix limit; zero uses SEQUEL_TEXT_MAX. */
  bool omit_query_text;        /**< Default false. Suppress text/formatted/maps; retain safe structural metadata. */
  bool format;                 /**< Default false. Format sanitized text if complete and layout supported. */
  bool keep_in_lists;          /**< Default false. Retain every IN-list value placeholder instead of collapsing. */
  bool omit_source_maps;       /**< Default false. Avoid generating either numeric map. */
  bool omit_bind_descriptors;  /**< Default false. Avoid exported bind descriptors; fingerprint still tracks identities.
                                */
  bool include_lints;          /**< Default false; emit opt-in structural, safety, shape and portability warnings. */
  sequel_cancel_fn cancelled;  /**< Optional per-call callback, polled during native work. */
  void *cancel_userdata;       /**< Opaque caller-owned context passed to cancelled. */
} sequel_analysis_options_t;

/** Unified owning result. Text and formatted are sanitized. Original SQL is never
 * retained. On malformed/resource/cancel failure text/fingerprint/maps/binds are
 * empty; canonical diagnostics retain original locations. Status: ok, malformed,
 * limit, cancelled, error. Omission reason is empty when text is present; otherwise
 * policy, malformed, analysis_limit, analysis_error, cancelled or empty. Never copy owning
 * structs; free before reuse. A fingerprint is not a SQL grammar validity verdict. */
typedef struct {
  unsigned schema_version;     /**< SEQUEL_ANALYSIS_VERSION, including failure results. */
  sequel_dialect_t dialect;    /**< Requested dialect; invalid arguments retain their supplied value. */
  const char *status;          /**< ok, malformed, limit, cancelled or error; always non-NULL after analysis. */
  const char *omission_reason; /**< Empty with present text; otherwise the documented safe enum. */
  const char *text;            /**< Owned sanitized UTF-8, or the empty string on omission/failure. */
  const char *summary;         /**< Owned safe structural summary; SQL on failure or empty input. */
  const char *formatted;       /**< Owned formatted sanitized text, or empty if disabled/unavailable. */
  char operation[32];          /**< Uppercase operation, empty for mixed/unknown operations or failure. */
  char fingerprint[96];        /**< Version/dialect/SHA-256 identifier, empty for empty or failed input. */
  bool truncated;              /**< Display text was shortened; fingerprint still covers full checked input. */
  size_t statement_count;      /**< Number of original nonempty statement ranges. */
  size_t original_length;      /**< Supplied UTF-8 length for _n, even when rejected before scanning. */
  const sequel_diagnostic_t *diagnostics;     /**< Owned canonical original-coordinate issues; empty on success. */
  size_t diagnostic_count;                    /**< Diagnostic array length. */
  const sequel_bind_descriptor_t *binds;      /**< Owned value-free descriptors; absent if omitted or failed. */
  size_t bind_count;                          /**< Bind array length. */
  const sequel_range_t *statements;           /**< Owned original byte ranges, length statement_count. */
  sequel_source_map_t original_to_sanitized;  /**< Maps original bytes to this result's text only. */
  sequel_source_map_t sanitized_to_formatted; /**< Maps this text to this formatted result only. */
  size_t memory_peak_bytes; /**< Peak tracked native requested allocations; excludes caller/language runtime memory. */
  size_t work_units;        /**< Observed counted work; units may change across implementation versions. */
  void *_storage;           /**< Private allocation registry; never read, modify or free directly. */
  sequel_diagnostic_t _failure_diagnostic; /**< Private inline fallback for allocation/budget failures. */
} sequel_analysis_t;

/** Analyze explicit UTF-8 bytes; embedded NUL is diagnosed. Safe statement-boundary
 * recovery collects multiple issues. Any malformed statement fails closed for the
 * entire document. Cancellation and budgets clear partial exported data.
 * Caller supplies out; free previous storage before reuse. On failure inspect
 * status/diagnostics; this API never throws or exports partial raw SQL. */
sequel_status_t sequel_analyze_n(const char *sql, size_t length, const sequel_analysis_options_t *options,
                                 sequel_analysis_t *out);

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
} sequel_options_t;

/** Bounded, sanitized query text and structural metadata from sequel_inspect.
 * Text is allocated to fit the actual result, not the maximum limits.
 * Do not copy this owning struct or modify its pointers. Release it with
 * sequel_metadata_free before reuse, including after a failed inspection. */
typedef struct {
  const char *text;
  const char *summary;
  char operation[32];
  bool truncated;
  size_t statement_count;
  /** Internal allocation; callers must not access or free it directly. */
  void *_storage;
} sequel_metadata_t;

/** Release inspection storage and reset metadata. NULL is allowed. Only call
 * on a zero-initialized struct or one initialized by sequel_inspect. Safe to repeat. */
void sequel_metadata_free(sequel_metadata_t *metadata);

/** Dialect-independent inspection, usable without SQLite. Never reads bind
 * values. Malformed quoting fails closed: text is empty and summary is SQL.
 * Input is limited to SEQUEL_INPUT_MAX and nesting to 64 levels. Output is UTF-8
 * bounded. out need not be initialized, but any previous result must be freed. */
sequel_status_t sequel_inspect(const char *sql, const sequel_options_t *options, sequel_metadata_t *out);

/** Sanitizes into a caller-owned buffer. capacity includes the NUL
 * terminator; limit is the maximum emitted bytes (zero uses SEQUEL_TEXT_MAX).
 * Capacity need only fit the actual output or its truncated prefix plus NUL;
 * insufficient capacity returns SEQUEL_ERR_OVERFLOW without partial output. */
sequel_status_t sequel_sanitize(const char *sql, sequel_dialect_t dialect, bool collapse_in_lists, size_t limit,
                                char *out, size_t capacity, bool *truncated);

/** Independent transformations. Retained comments and literals may contain secrets.
 * Zero flags preserves lexical contents while normalizing outside whitespace.
 * IN-list collapsing applies only when replacing literals. */
#define SEQUEL_REMOVE_COMMENTS 1u
#define SEQUEL_REPLACE_LITERALS 2u
#define SEQUEL_SANITIZE_ALL (SEQUEL_REMOVE_COMMENTS | SEQUEL_REPLACE_LITERALS)

/** Sanitize with independently selected comment removal and literal replacement. */
sequel_status_t sequel_sanitize_with_flags(const char *sql, sequel_dialect_t dialect, unsigned flags,
                                           bool collapse_in_lists, size_t limit, char *out, size_t capacity,
                                           bool *truncated);

/** Layout-only SQL formatting. Comments, literals, identifiers and bind markers
 * are preserved verbatim. No SQL is executed or sanitized. Input is UTF-8,
 * limited to SEQUEL_INPUT_MAX / 64 nesting levels. Not a SQL validator.
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
 * quotes/comments (unless tolerant), >SEQUEL_INPUT_MAX input. Unbalanced
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
