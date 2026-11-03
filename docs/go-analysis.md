# Unified analysis in Go

Import `github.com/zfogg/sequel/go`. The existing `Inspect`, `Diagnose`, formatting and sanitization APIs retain their behavior. `Analyze` collects safe query text, structural metadata, diagnostics, fingerprints, bind identities and source maps in one result:

```go
result := sequel.Analyze(query, sequel.AnalysisOptions{
    Dialect: sequel.DialectPostgreSQL,
    Format: true,
    Context: ctx,
})
if result.Status != "ok" {
    // Diagnostics contain catalog prose and original numeric locations.
    // Text, Formatted and Fingerprint are empty on failure.
}
```

`Analysis.SchemaVersion` is `AnalysisSchemaVersion` (currently 1). `Status` is `ok`, `malformed`, `limit`, `cancelled` or `error`. A nonempty `Text` has an empty `OmissionReason`; omitted text uses `empty`, `policy`, `malformed`, `analysis_limit`, `analysis_error` or `cancelled`. Cancellation is checked inside native work through the provided Go context.

Zero `MaxInputBytes`, `MaxOutputBytes` and `MaxMemoryBytes` use the native 1 GiB ceilings. `MaxOutputBytes` accounts for the native generated result, including numeric metadata arrays; it is separate from `MaxQueryTextBytes`, which truncates query text on UTF-8 boundaries. Zero `MaxWorkUnits` permits unlimited work. `MemoryPeakBytes` and `WorkUnits` report native resource use. Budgets cover native working allocations before results are copied into Go-owned memory. `OmitSourceMaps` and `OmitBindDescriptors` avoid retaining metadata a caller does not need.

`Fingerprint(dialect, query)` returns a `sequel:v1:<dialect>:<SHA-256>` identifier. The versioned, framed token stream omits literal values, comments, whitespace and marker spelling, while distinguishing token boundaries, quoted identifiers, operators, dialect and repeated-binding relationships. Numbered descriptor slots retain driver arity (`$3` requires three supplied positions), while fingerprint identity normalizes marker spelling. Fingerprints are query-shape identifiers, not database grammar validation or unbounded metric dimensions.

Every literal contributes a fresh anonymous parameter identity; literal equality is ignored. `SELECT 1,2` and `SELECT :a,:b` therefore share a fingerprint within a dialect, while `SELECT :a,:a` preserves its repeated-slot relationship. Ordinary words use ASCII case folding in generic SQL, SQLite, PostgreSQL and DuckDB. MySQL and ClickHouse preserve identifier/function case conservatively while ASCII-folding recognized SQL keywords; ClickHouse parameter type tokens preserve case too. Quoted identifiers retain spelling in every dialect. Empty/comment-only input returns an empty fingerprint. The cross-language version 1 golden for generic `SELECT 1` is `sequel:v1:generic:2c01bfb45c6e98658d756ef07e1cd3a4c2d82cb4c0d841c61fd4516786a19145`.

`Binds` contains occurrence ordinal, per-statement slot, statement index, style and original byte ranges. No names, type text or values are exposed. `CheckBinds(result.Binds, statementIndex, suppliedSlots)` checks arity without accepting values.

All source-map and diagnostic offsets are zero-based UTF-8 bytes, with exclusive ends. Diagnostics also carry one-based Unicode-scalar line/column positions; CRLF is one newline. Never apply original diagnostic offsets directly to sanitized or formatted text:

```go
safe, err := result.OriginalToSanitized.MapRange(start, end, false)
shown, err := result.SanitizedToFormatted.MapRange(safe.Start, safe.End, false)
utf16Start, err := sequel.UTF8ToUTF16(result.Formatted, shown.Start)
utf16End, err := sequel.UTF8ToUTF16(result.Formatted, shown.End)
```

`MappedRange.Exact` is false when crossing a replacement, deletion or inserted layout. A removed comment maps to an empty anchor; an interior position in a literal maps to the complete replacement. Reverse mapping uses `reverse=true`. Numeric map segments contain no original or replacement bytes. UTF-16 conversion uses global code units, counting CR and LF separately, and rejects split UTF-8 scalars and split surrogate pairs instead of rounding. Combining marks remain separate scalars/code units. `OriginalLength` is always the supplied byte count, including failures; the original text is never retained.

`DiagnosticCatalog` exposes every canonical code and its trusted explanatory text. `CanonicalizeDiagnostic(DiagnosticSchemaVersion, originalLength, d)` validates a received diagnostic and discards its incoming prose. `ValidateDiagnostic` checks the same structure without returning a copy. Unknown versions/codes, invalid severities, inconsistent flags and invalid ranges are rejected.

Use `MarshalAnalysis` and `UnmarshalAnalysis` for the versioned JSON contract. They validate structure and canonicalize diagnostic prose. Decoding rejects unknown fields and versions, and errors never quote input payloads. Transport validation does not authenticate an arbitrary sender or establish that a sender's `text` or `summary` fields were sanitized; use results from the trusted analyzer. Standard `encoding/json` remains available when callers intentionally manage validation themselves.

The `go run ./cmd/sequel-analyze` command reads SQL from standard input and emits one JSON result. It supports `-dialect`, `-format`, `-omit-text`, and per-call budget flags. It exits 0 on successful analysis, 1 on a machine-readable analysis failure, and 2 on invocation/I/O errors. SQL is accepted only on stdin, and failures never print the original query.
