# Sequel C API

Sequel formats, sanitizes, inspects and highlights SQL without executing it.
Include `<sequel.h>` and link the CMake target `sequel::sequel`.
See the [public header reference](sequel_8h.html) for all functions and types.
The [unified analysis contract](sequel_analysis_contract.html) covers fingerprints,
safe source maps, bind metadata, diagnostic transport, and per-call budgets.

## Formatting and highlighting

Formatting preserves comments, identifiers, literals and bind markers. Sanitizing
can remove comments and replace literals independently. Highlighting preserves
the exact source, escaping HTML or emitting truecolor ANSI.

```c
sequel_highlight_options_t options;
sequel_highlight_options_init(&options);
options.dialect = SEQUEL_POSTGRESQL;
size_t length;
if (sequel_highlight(sql, &options, NULL, 0, &length) == SEQUEL_OK) {
    char *html = malloc(length + 1);
    if (html) {
        sequel_status_t status = sequel_highlight(sql, &options, html, length + 1, &length);
        /* Use html only when status == SEQUEL_OK. */
        free(html);
    }
}
```

Output capacities include the terminating NUL. Measurement calls pass `out=NULL`
and `capacity=0`; lengths exclude NUL. SQL and output buffers must not overlap.
Input is UTF-8 and limited to 1 GiB and 64 nesting levels.

## Inspection ownership and limits

`SEQUEL_INPUT_MAX`, `SEQUEL_TEXT_MAX` and `SEQUEL_SUMMARY_MAX` are 1 GiB
byte constants. Inspection allocates storage for the actual text and summary;
a small query does not reserve the configured maximum. A zero text limit selects
`SEQUEL_TEXT_MAX`; explicit smaller limits still truncate on UTF-8 boundaries.

`sequel_metadata_t` now holds owned string pointers instead of inline arrays.
C consumers must rebuild and call `sequel_metadata_free(&metadata)` after each
`sequel_inspect` call, including failures, and before reusing the result struct.
Do not copy the struct or free its strings individually. Go and Python bindings
manage this ownership automatically.

## Diagnostics and editing

`sequel_diagnose` reports lexical and delimiter problems with explanations and
fix hints. `sequel_diagnose_n` additionally handles nonterminated buffers and
embedded NULs. Offsets are zero-based UTF-8 bytes with an exclusive end;
line/column locations are one-based Unicode scalars. CRLF is one newline.

Use `sequel_format_ex` or `sequel_highlight_ex` for structured transformation
errors. `options.tolerant = true` permits incomplete lexical constructs during
editing; a successful highlight can still carry an incomplete warning. Controls,
invalid UTF-8, limits and unsupported data payloads remain errors.

All options, diagnostics, palettes and output buffers are caller-owned. There
is no mutable global configuration. Retained SQL literals/comments may contain
secrets; formatting and highlighting do not sanitize them.
