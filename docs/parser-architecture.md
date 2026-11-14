# Parser architecture

Sequel parses SQL once and retains the structure needed by every consumer:

```text
source -> lexed document -> recovered syntax document -> query document
                                  |                         |
                                  + diagnostics             + analysis
                                                            + binds
                                                            + formatting
                                                            + summaries
                                                            + fingerprints
                                                            + source maps
                                                            + serialization
```

The layers are intentionally separate. Lexical validity, syntactic validity,
semantic analyzability, policy safety, and dialect portability are different
properties. A query can be syntactically valid while producing an
`unbounded_write` policy diagnostic.

## Ownership and lifetime

The public object graph is:

| Object | Responsibility | Release function |
| --- | --- | --- |
| `sequel_source_t` | Owned or borrowed UTF-8 bytes and dialect | `sequel_source_free` |
| `sequel_lexed_document_t` | Lossless tokens and lexical records | `sequel_lexed_document_free` |
| `sequel_syntax_document_t` | Recovered concrete syntax tree | `sequel_syntax_document_free` |
| `sequel_query_document_t` | Normalized statements and query records | `sequel_query_document_free` |
| `sequel_query_analysis_t` | Optional derived outputs and diagnostics | `sequel_query_analysis_free` |
| `sequel_document_bundle_t` | Deserialized document bundle | `sequel_document_bundle_free` |

Accessor arrays are borrowed. A query document borrows its syntax document, so
the syntax document must outlive it. Analysis results borrow syntax/query
records and own only requested derived strings, maps, and diagnostics.

All public ranges are zero-based, half-open UTF-8 byte ranges. Byte offsets
are authoritative; line/column values are derived for presentation.

## Parse pipeline

Initialize `sequel_parse_options_t` with `sequel_parse_options_init`. The
options select dialect, comment/whitespace retention, tolerant parsing,
vendor-extension handling, recovery, nesting/token/node/diagnostic limits,
memory/work budgets, and incremental metadata.

```c
sequel_source_view_t view = {sql, length};
sequel_parse_options_t options;
sequel_parse_options_init(&options);
options.dialect = SEQUEL_POSTGRESQL;
options.retain_comments = true;
options.retain_whitespace = true;
options.recover_errors = true;

sequel_syntax_document_t *syntax = NULL;
sequel_parse_sql(&view, &options, NULL, &syntax);
sequel_query_document_t *query = NULL;
sequel_build_query_document(syntax, &options, &query);

/* Use diagnostics, analysis, formatting, summary, or fingerprint APIs. */
sequel_query_document_free(query);
sequel_syntax_document_free(syntax);
```

Cancellation callbacks belong to the parse context. They must be fast, must
not re-enter the parser, and cause partial results to be discarded.

## Lexical document

The lexer is public through `sequel_lex`. Tokens retain source ranges rather
than copied text and distinguish identifiers, quoted identifiers, keywords,
strings, numbers, booleans, NULL, parameters, operators, punctuation,
comments, whitespace, and errors. Flags record literal, bind, comment,
dialect-specific, recovered, case-folded, and unterminated state.

Unsupported-but-valid dialect syntax is retained and marked; it is not silently
discarded. `sequel_token_text` obtains the original spelling safely.

## Syntax document and recovery

Syntax nodes use indexes instead of pointers. Each node has a kind, flags,
source/header ranges, child span, token span, and production identifier. This
makes the tree relocatable, serializable, and safe across language bindings.

The parser recovers at statement and clause boundaries. Issues record exact
ranges, statement indexes, recovery strategy, and expected tokens where
available. `sequel_syntax_visit` is preferred over depending on arena layout.

## Normalized query model

`sequel_build_query_document` derives statements, nested scopes, CTEs, tables,
subqueries, joins, predicates, projections, clauses, parameters, operations,
and source ranges from syntax. Consumers use accessors or `sequel_query_visit`;
they do not rescan raw SQL or depend on internal arrays.

This separation lets lossless formatters use tokens/syntax while analysis,
policy, binding extraction, summaries, and fingerprints use normalized records.

## Diagnostics and analysis

Structured diagnostics identify lexical, syntax, semantic, policy,
portability, binding, and API origins. Use the independent collection passes:

```text
sequel_collect_lexical_diagnostics
sequel_collect_syntax_diagnostics
sequel_collect_semantic_diagnostics
sequel_collect_policy_diagnostics
sequel_collect_portability_diagnostics
```

Diagnostic prose is catalog-backed and never echoes literals, comments, bind
names, or arbitrary SQL. The aggregate analysis API consumes the same parser
layers and can request bindings, lints, portability, summaries, fingerprints,
sanitized/formatted SQL, and source maps independently.

## Formatting, sanitizing, and summaries

`sequel_format_tokens` preserves lexical spelling and supports incomplete SQL.
`sequel_format_syntax` uses recovered structure and reports whether output was
lexical, syntax-aware, or recovered. Sanitization consumes shared tokens and
emits numeric provenance maps. `sequel_summarize_query` consumes normalized
query records rather than guessing structure from recursive token scans.

Fingerprints support token, syntax, and query-shape modes. Dialect descriptors
centralize keyword, operator, clause, and feature rules. Vendor extensions are
retained and diagnosed deliberately.

## Incremental parsing and serialization

`sequel_reparse` accepts a previous syntax document and a half-open text edit.
It returns an independent document; unchanged ranges retain equivalent
coordinates, not pointer identity. `sequel_reparse_impact` reports affected
regions before reparsing.

`sequel_document_serialize` writes versioned syntax/query data, ranges,
diagnostics, binds, fingerprints, dialect, and feature flags. Unknown major
schemas are rejected; unknown minor fields are ignored. Deserialization treats
the buffer as untrusted and validates ranges, counts, enums, and limits.

## Source layout and packaging

```text
include/                 installed public SDK headers
lib/                     C SDK implementation
lib/analysis/            analysis passes
lib/summary/             summary passes
lib/visit/               visitors
src/                     CLI-only implementation
go/native.c              engine/third-party cgo translation unit
go/native_lib.c          generated single SDK cgo translation unit
```

Public headers never include private `internal.h` files. `cmake --install`
installs the binary, library, CMake package metadata, and public headers.
Source distributions include the private implementation required to build the
Python extension. Generated native files are refreshed with
`python scripts/sync_native.py`.

## Consumer rules

Do not retain borrowed arrays after releasing their owner, apply a sanitized
map to original SQL, use original offsets against formatted text, or interpret
successful formatting as proof of syntactic completeness. These rules preserve
the layer boundaries that make the parser safe for CLIs, telemetry, formatters,
IDEs, and future lint passes.
