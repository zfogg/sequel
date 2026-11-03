\page sequel_analysis_contract Unified C analysis

`sequel_analyze_n` performs one fail-closed analysis of explicit UTF-8 bytes. It
returns sanitized query text, an optional formatted version of that sanitized
text, a structural summary and operation, a versioned fingerprint, diagnostics,
bind descriptors, statement ranges, and source maps. It never executes SQL or
reads supplied bind values. `sequel_analyze` accepts NUL-terminated input instead.
Lexical and delimiter checks do not establish database grammar validity.

## Call and ownership

```c
sequel_analysis_options_t options = {0};
options.dialect = SEQUEL_POSTGRESQL;
options.format = true;
options.max_query_text_bytes = 4096;
options.max_memory_bytes = 8 * 1024 * 1024;
options.max_work_units = 1000000;

sequel_analysis_t result = {0};
sequel_status_t status = sequel_analyze_n(sql, sql_length, &options, &result);
if (status == SEQUEL_OK) {
    /* result.text and result.formatted contain sanitized SQL only. */
    consume_metadata(&result);
} else {
    /* Locations and catalog explanations are safe; original SQL is absent. */
    consume_diagnostics(result.diagnostics, result.diagnostic_count);
}
sequel_analysis_free(&result);
```

Options and the input buffer belong to the caller and only need to remain valid
for the call. The result owns its strings and arrays. Do not copy the owning
struct, retain its pointers after free, modify internal storage, or free strings
individually. Call `sequel_analysis_free` after success or failure and before
reusing a result. Free accepts NULL and is safe to repeat on an initialized
result. Go and Python bindings copy into language-owned results and release
native storage automatically.

`original_length` records the supplied UTF-8 byte length even when analysis
fails. The explicit-length API can report the entire original length after an
input-budget failure without scanning the rejected buffer. The terminated API
scans only through its input ceiling plus the overflow sentinel, so it cannot
report a longer unknown suffix's full length. Embedded NUL is a control-character
diagnostic when using the explicit-length API.

The stable `status` strings are `ok`, `malformed`, `limit`, `cancelled`, and
`error`. An absent query text has one `omission_reason`: `empty`, `policy`,
`malformed`, `analysis_limit`, `cancelled`, or `analysis_error`. Present text has
an empty omission reason. `omit_query_text` produces policy omission while
retaining structural metadata and the fingerprint. `omit_source_maps` and
`omit_bind_descriptors` avoid generating those optional result arrays.

Any malformed statement makes the entire document fail closed: text, formatted
text, fingerprint, maps, and bind descriptors are empty. A resource or
cancellation failure also discards partial exported data. Diagnostics use fixed
catalog prose with numeric original locations; they do not interpolate SQL,
comment contents, literal values, parameter names, or driver exception messages.

## Fingerprint version 1

A fingerprint has the form `sequel:v1:<dialect>:<64 lowercase SHA-256 hex digits>`.
The dialect is part of both its visible namespace and its hashed stream. The
stream has explicit version and dialect frames, length-framed token classes,
and parameter identities. Ordinary words use ASCII case folding in generic,
SQLite, PostgreSQL, and DuckDB modes. MySQL and ClickHouse preserve unquoted word
spelling conservatively because table and function name case can be semantic;
Recognized SQL keywords are ASCII-folded in every dialect. Quoted identifiers preserve
their spelling and quote class in every dialect. Token boundaries and operators
remain distinct. Comments and outside-token whitespace are excluded.

Every literal becomes a fresh anonymous fingerprint parameter. Literal values
are neither retained nor compared, so repeated equal literals remain separate
parameters. Untyped bind markers use that same parameter representation, while
repeated references to the same bind slot preserve their relationship:

| SQL shape, within one dialect | Relationship |
| --- | --- |
| `WHERE id=42` and `WHERE id=$1` | Same fingerprint |
| `SELECT 1,2` and `SELECT :a,:b` | Same fingerprint |
| `SELECT 42,42` and `SELECT :a,:b` | Same fingerprint; literal equality is ignored |
| `SELECT :a,:a` and `SELECT :b,:b` | Same fingerprint |
| `SELECT :a,:a` and `SELECT :a,:b` | Different fingerprints |
| `SELECT "A"` and `SELECT "a"` | Different fingerprints |

Canonical parameter identities follow first occurrence and reset at statement
boundaries. Renaming markers or renumbering a consistent relationship does not
change the fingerprint. Numbered aliases such as `$1` and `$01` refer to the same
slot. Dialect rules still determine whether a token is a bind: PostgreSQL's `?`
operator is not treated as an anonymous parameter, and SQLite's `:x`, `@x`, and
`$x` retain separate driver binding namespaces.

A parameter's outer representation is a `B` byte, its big-endian 64-bit canonical
identity, and a length-framed `Y` type field. Untyped binds and literals have an
empty type field. Typed parameters include the 64 lowercase hexadecimal bytes
of a separate SHA-256 digest of their canonical type-token stream. The explicit
type frame prevents a parameter's type tokens from being confused with tokens
in the following SQL expression. Bind names and literal values are absent from
the canonical stream. Numeric literal and marker spelling are excluded, while
typed parameter structure and spelling are retained.

A length frame is one ASCII kind byte, an unsigned big-endian 64-bit payload
length, and that many payload bytes. The leading `V` payload is
`sequel-fingerprint-v1`; the `D` payload is the lowercase dialect name. `W` frames
contain ordinary words with the dialect case policy above, `Q` frames contain
verbatim quoted identifiers, and `T` frames contain other verbatim lexical
tokens. In a nested ClickHouse type digest, `L` frames preserve type-argument spelling,
`W` frames contain verbatim words, and `T` frames contain the remaining type
tokens. Thus `UInt64` and `uint64` remain distinct lexical type spellings; this
is not a type-grammar validation verdict. Precision/scale and enum labels inside a typed marker are structural type declarations, so their spelling is retained in the nested digest; supplied bind values are never inspected. Comments are excluded from both
streams. These framing rules make the version 1 stream independently testable.

SHA-256 collisions are possible but cryptographically unlikely. Structural
normalization intentionally groups different values; a fingerprint is not a
unique query identifier, proof of executable SQL, encryption of input, or a SQL
execution cache key. Empty/comment-only input and lexical or delimiter failures
produce no fingerprint. Aggregate by fingerprint together with the relevant
dialect, database, and service identity. Do not use unbounded fingerprint values
as metric dimensions. A future incompatible canonicalization policy requires a
new fingerprint version.

## Diagnostic catalog and schema

`SEQUEL_ANALYSIS_VERSION` and `SEQUEL_DIAGNOSTIC_VERSION` identify the public
transport contracts; both currently equal 1. `schema_version` travels with an
analysis. Reject unsupported schema versions instead of silently interpreting
new fields or code meanings under an older contract.

Use `sequel_diagnostic_catalog_count` and `sequel_diagnostic_catalog_get` to
enumerate codes and their canonical severity, incomplete state, message,
explanation, and hint. Numeric enum values and `sequel_diagnostic_code_name`
provide the code identity. The `none` entry describes a successful lexical and
delimiter check; a successful analysis exports an empty diagnostics array.

For an incoming diagnostic, call `sequel_diagnostic_canonicalize` with its schema
version and original source length. It rejects unknown codes or versions,
inconsistent severity/incomplete flags, out-of-bounds/reversed byte ranges, and
inconsistent line/column ordering. It replaces all incoming prose with catalog
strings, even when an incoming character array lacks NUL termination. This
validates transport structure; without the original SQL it cannot prove that a
claimed range corresponds to a particular character or construct.

Ranges are zero-based, half-open UTF-8 byte offsets into the original SQL.
Line/column coordinates are one-based Unicode scalar positions. Tabs count as
one column and CRLF is one newline. Multiple delimiter diagnostics recover at
lexically safe statement boundaries. Semicolons inside strings, quoted
identifiers, comments, or opaque parameter syntax cannot be used as recovery
boundaries. An unterminated quoted tail therefore consumes the remaining input.

## Provenance and browser coordinates

Original SQL offsets must be mapped before highlighting sanitized or formatted
text. `original_to_sanitized` maps original bytes to `text`;
`sanitized_to_formatted` maps `text` bytes to `formatted`. The maps contain only
numeric lengths, segments, and transformation kinds. They contain no original
SQL fragments or reverse-reconstruction payloads. They belong to these exact
analysis strings; do not apply them to separately transformed legacy outputs.

Each map completely and monotonically covers both documents. A `copy` segment
has equal byte lengths. `replace` maps a nonempty original range to a nonempty
replacement. `delete` retains a zero-length destination anchor. `insert` maps a
zero-length source anchor to inserted layout. Collapsed IN-list contents map to
the sanitized placeholder; UTF-8 truncation maps the removed suffix to a deletion
anchor.

`sequel_source_map_range` validates the map and maps a half-open byte range in
either direction. Copied interiors preserve byte offsets. A replacement interior
maps to the entire replacement envelope; offsets are never interpolated through
redacted values. Deleted ranges map to empty anchors. `exact=false` identifies
ranges involving transformed content. A nonempty range includes inserted layout
at interior anchors and excludes inserted layout touching only its boundaries.
An empty cursor at an insertion maps to its full inserted span with `exact=false`.

For browser offsets, first map the original byte range into the displayed
analysis string, then convert both endpoints with `sequel_utf8_to_utf16`.
`sequel_utf16_to_utf8` performs the inverse conversion. These functions reject
invalid UTF-8, split scalar boundaries, and split UTF-16 surrogate pairs.
Combining marks remain separate scalars. Conversion offsets are global code-unit
counts; CR and LF each contribute one unit even though diagnostic CRLF line
counting treats them together.

## Bind metadata and arity checks

A bind descriptor exports occurrence `ordinal`, driver `slot`,
`statement_index`, syntax `style`, and an original UTF-8 byte range. It exports
no parameter name, type spelling, or value. Ordinals are one-based across the
document; statement indices are zero-based. Driver slot numbering restarts per
statement. Named repeats share a slot; anonymous markers allocate a new slot.
Fingerprint parameter identities are separate from these driver slots, so
replacing a literal with a placeholder does not shift supplied driver metadata.

`sequel_check_binds` validates metadata and checks supplied arity for one
statement without accepting values. Supplied arity must equal the maximum used
driver slot, including explicit numbered gaps: `SELECT $3` requires an arity of
3 even though it contains one occurrence. SQLite explicit-number aliases reuse
the corresponding anonymous/named slot where its driver semantics require it.
This is an arity check, not database-specific type validation or proof that a
particular mapping of application values is correct.

## Budgets and cancellation

Zero input, output, and native-memory limits select the existing 1 GiB ceilings.
Zero work limit is unlimited. `max_input_bytes` limits accepted input;
`max_output_bytes` limits generated strings and numeric result arrays;
`max_memory_bytes` limits live native allocations and intermediates; and
`max_work_units` limits counted scanning, hashing, copying, and transformation
steps. Limits are per call. A small query allocates for its actual data rather
than reserving the configured maximum. `memory_peak_bytes` and `work_units`
report observed usage, including resource failures where accounting began.
Work units are implementation accounting units, not time or a cross-version
performance guarantee.

`max_query_text_bytes` is a separate display truncation limit. It preserves a
UTF-8 prefix and marks `truncated`; it does not replace validation or total
resource budgets. A truncated prefix can end within a quoted identifier or group,
so formatted output may be absent. The fingerprint describes the full checked
input, independent of display truncation, IN-list collapsing, or text omission.

Set `cancelled` and `cancel_userdata` to a callback returning true when the call
should stop. Cancellation is polled during native scanning and transforms,
including long tokens, and returns `SEQUEL_ERR_CANCELLED`. The callback must not
re-enter the same analysis. Cancellation callbacks and options are per call;
there is no mutable process-wide budget configuration. Budget and cancellation
failures return canonical resource diagnostics and empty text/fingerprint/maps.

Existing formatting, highlighting, sanitizing, inspection, and first-diagnostic
APIs retain their compatibility behavior. Formatting or highlighting original
SQL can preserve secrets; the unified analysis's formatted output is produced
from sanitized text.
