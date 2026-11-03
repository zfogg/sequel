# Unified analysis in Python

`sequel.analyze` returns one immutable result with sanitized text, structural
summary, operation, statement ranges, diagnostics, fingerprint, binds and numeric
source maps. It never executes SQL, reads driver values, or retains original SQL.
The `formatted` field formats sanitized SQL; the separate `format_sql` API
preserves potentially sensitive input.
Optional formatting can be unavailable for unsupported layouts or truncated
text. In that case `formatted` and its map are empty while successful lexical
analysis, sanitized text and fingerprints remain available.

```python
from sequel import Dialect, analyze, check_binds, utf8_to_utf16

result = analyze("SELECT name FROM users WHERE id=:id", dialect=Dialect.SQLITE, format=True)
if result.status == "ok":
    check_binds(result.binds, supplied_slots=1)
    print(result.fingerprint, result.summary)
```

Fingerprints use `sequel:v1:<dialect>:<sha256>` and a length-framed canonical token
stream. Literal values, comments, whitespace and bind spelling are excluded;
quoted identifiers, token boundaries, operators and repeated bind relationships
remain significant. Each literal is a fresh anonymous parameter: `SELECT 42,42`
and `SELECT :a,:b` have the same fingerprint, while `SELECT :a,:a` differs.
Untyped markers and literals share a `B` identity plus an empty `Y` type frame;
typed markers have a separately hashed, explicitly framed type stream. Canonical
parameter identities reset at statement boundaries. Ordinary words use ASCII
case folding in generic, SQLite, PostgreSQL and DuckDB. MySQL and ClickHouse
preserve identifier/function spelling while folding recognized SQL keywords; quoted
identifiers and ClickHouse type words preserve spelling in every case.
The schema-v1 golden for generic `SELECT 1` is
`sequel:v1:generic:2c01bfb45c6e98658d756ef07e1cd3a4c2d82cb4c0d841c61fd4516786a19145`.
SHA-256 collisions are theoretically possible. A successful
fingerprint checks lexical/delimiter shape; it is not a database grammar verdict.
Do not use fingerprints as an unbounded metric dimension.

Zero `max_input_bytes`, `max_output_bytes` and `max_memory_bytes` select the native
1 GiB ceiling. `max_work_units=0` is unlimited. Memory accounts native working
allocations; output accounts generated strings and metadata. The independent
`max_query_text_bytes` truncates only exported text. `omit_query_text=True` keeps
structural metadata without exporting SQL. Set `omit_source_maps` and
`omit_bind_descriptors` to avoid generating unneeded metadata. An optional
`cancelled()` callback is checked during native work; callback exceptions
propagate. A failure clears partial text, fingerprints, maps and binds.

`status` is `ok`, `malformed`, `limit`, `cancelled` or `error`. An absent text has
an explicit `omission_reason`: `empty`, `policy`, `malformed`, `analysis_limit`,
`analysis_error` or `cancelled`. Empty diagnostics indicate successful lexical
checks only when status is `ok`. Multiple diagnostics recover at safe statement
boundaries; semicolons inside an unfinished quote are not recovery points.

Ranges use zero-based UTF-8 bytes with exclusive ends. Diagnostic line/column
positions use one-based Unicode scalar columns and treat CRLF as one newline.
`original_to_sanitized.map_range(start,end)` maps locations before display;
`sanitized_to_formatted` maps again when showing `formatted`. `reverse=True`
reverses either map. Replacement interiors cover the entire replacement; deleted
ranges map to empty anchors. `exact=False` records edits and redaction. Maps
contain only numeric provenance and cannot reconstruct deleted values.
`utf8_to_utf16` and `utf16_to_utf8` convert global document offsets for browser
APIs. They reject split UTF-8 scalars or UTF-16 surrogate pairs. CR and LF each
count one UTF-16 unit; combining marks remain separate scalars.

Bind descriptors expose one-based occurrence `ordinal`, one-based `slot`,
zero-based `statement_index`, source range and syntax style. Names, declared
types and values are omitted. Repeated named/numbered markers share identity;
explicit numbered markers retain arity (`$3` requires three slots).
`check_binds` validates supplied arity for one statement without reading values.

`diagnostic_catalog()` supplies public canonical text. Use
`canonicalize_diagnostic(d, source_length=...)` to validate metadata and replace
untrusted prose, or `validate_diagnostic` to reject unsupported metadata.
Unknown schema versions/codes, inconsistent types and invalid ranges fail.
Validation checks transport structure; matching a location to unavailable SQL
cannot be established from numbers alone.

`Analysis.to_dict()` emits schema-v1 JSON-compatible transport. The corresponding
`Analysis.from_dict()` validates types, map coverage, known document lengths,
status and diagnostic ranges, and replaces diagnostic prose from the catalog.
It cannot prove that incoming text, summaries or fingerprints were produced by
a sanitizer: those fields require a trusted analyzer as their source. Unknown
fields are rejected. No original SQL field is serialized.
An input-limit result may legitimately report an `original_length` larger than
the 1 GiB accepted-input ceiling: explicit-length native calls can reject the
buffer before reading it. Transport preserves that length and validates the
diagnostic against it without allocating or inspecting the rejected document.
