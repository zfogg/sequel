# sequel

[Documentation](https://zfogg.github.io/sequel/) ·
[C API](https://zfogg.github.io/sequel/c/) ·
[Python API](https://zfogg.github.io/sequel/python/) ·
[Go API](https://zfogg.github.io/sequel/go/) ·
[Rust API](https://zfogg.github.io/sequel/rust/)

A C11 library and command-line program for SQL formatting, syntax highlighting, sanitization and
structural summaries. Supports generic SQL, PostgreSQL, SQLite, MySQL, DuckDB
and ClickHouse. No database or telemetry runtime is required.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
uv pip install --system -r tests/requirements-format.txt
ctest --test-dir build -C Release --output-on-failure
cmake --install build --prefix ./install
```

The library is `sequel::sequel`; the executable is `sequel`.
`SEQUEL_BUILD_TESTS` and `SEQUEL_BUILD_CLI` default to off when embedded through
CMake FetchContent or add_subdirectory. Both static and shared builds are supported.
Installed consumers can use `find_package(sequel CONFIG REQUIRED)`.

## Release downloads

[GitHub Releases](https://github.com/zfogg/sequel/releases) provides native CLI
and static/shared C SDK ZIPs for **Linux, macOS and Windows, each on x64 and
ARM64**, plus Python wheels/sdist, a Rust source crate, complete binding sources
and checksums. The static ZIP contains the CLI. Linux binaries are fully static;
Windows uses the static CRT; macOS retains Apple's required system library.
See [release installation and linking](docs/releases.md) for exact asset names,
Python compatibility, CMake/Rust prebuilt linking and Go module usage.

The CLI and bindings are independent: downloading the executable does not install
a Python module or Rust/Go package. Python wheels contain their native engine;
Rust and Go compile bundled C by default. Neither shells out to the executable.

```sh
echo 'select a,b from users where id=42' | sequel format --dialect postgresql
sequel sanitize "select * from users where token='secret'"
sequel format --dialect postgresql --edit-file queries.sql
sequel format --embedded --language Go queries.go
```

Commands accept SQL as one argument or stdin. `format` preserves literal values;
`sanitize` removes comments and replaces literals and bind markers with `?`.
With no selection flags, `sanitize` applies both transformations. Use
`--remove-comments` alone to retain values, `--replace-literals` alone to retain
comments, or both flags for both. Retained values or comments may contain secrets.
The equivalent library API is `sequel_sanitize_with_flags`, Go's
`SanitizeSelective`, or Python's `sanitize(remove_comments=..., replace_literals=...)`.
Formatting does not sanitize or execute SQL. See [CLI details](src/README.md)
for embedded source formatting and conservative file editing.

## Unified analysis

Use C `sequel_analyze_n`, Go `Analyze`, or Python `analyze` for sanitized text,
summary, operation, versioned SHA-256 fingerprints, canonical diagnostics, bind
arity metadata, original statement ranges and safe numeric source maps from one
shared token scan. Optional formatting uses sanitized tokens. Original SQL and
bind values are never retained in the result; malformed input fails closed.

[Analysis contracts](docs/c-analysis.md), [Go usage](docs/go-analysis.md), and
[Python usage](docs/python-analysis.md) document ownership, versioned transport,
UTF-8/UTF-16 conversions, configurable budgets and cancellation. Existing APIs
remain available. Fingerprints normalize literal values and marker spelling while
preserving repeated binding relationships and dialect semantics.

The existing C CLI provides multiple safe-boundary diagnostics with `sequel diagnose --all --json`
(and interactive `:explain-all`); its default diagnostic schema is preserved.

The full JSON analysis binding example is available with `go run ./cmd/sequel-analyze -dialect postgresql`;
pass SQL on stdin. `-format`, `-omit-maps`, `-omit-binds`, `-max-memory-bytes`,
`-max-output-bytes` and `-max-work-units` configure optional output and budgets.
Errors remain machine-readable and never echo the input.

## C API

Include `<sequel.h>` and link `sequel::sequel`. All public types and functions
use the `sequel_` prefix and constants use `SEQUEL_`.

```c
sequel_metadata_options_t options = {.dialect = SEQUEL_POSTGRESQL};
sequel_metadata_t query;
if (sequel_metadata("SELECT * FROM users WHERE id=$1", &options, &query) == SEQUEL_OK) {
  puts(query.text);     /* SELECT * FROM users WHERE id=? */
  puts(query.summary);  /* SELECT … FROM users WHERE … */
}
sequel_metadata_free(&query);
```

Inspection text and summary default to 1 GiB limits, with storage allocated for
the actual results. Call `sequel_metadata_free` before reusing or discarding a
result, including on failure. This replaces the previous inline-buffer C metadata
layout; C consumers must rebuild and add the cleanup call.

`sequel_metadata` returns bounded UTF-8 text, summary, operation, statement count
and truncation status. `sequel_sanitize` accepts a caller-sized buffer and limit.
`sequel_format` supports a measure pass with `out=NULL, capacity=0` before allocating.
`sequel_compact` preserves newlines while collapsing horizontal token gaps.
Malformed quoting fails closed. Input is limited to 1 GiB and nesting to 64 levels.
The public status enum is independent of OpenTelemetry.

## Go

```go
import sequel "github.com/zfogg/sequel/go"

formatted, err := sequel.FormatSQL(sequel.DialectPostgreSQL, sql)
summary := sequel.Summary(sequel.DialectPostgreSQL, sql)
summary, operation, safe, count, ok := sequel.Inspect(sql,
    sequel.DefaultSanitizeOptions(sequel.DialectPostgreSQL))
```

The module bundles the C implementation and utf8proc, so consumers need cgo and
a C compiler but no installed sequel library. Run `go test ./...` from this root.

## Python

```sh
uv sync --extra test
uv run --extra test pytest
```

The distribution is `sequel`; its import is `sequel`.

```python
from sequel import Dialect, metadata, format_sql
query = metadata("SELECT * FROM users WHERE token='secret'", dialect=Dialect.POSTGRESQL)
print(query.text, query.summary)
print(format_sql("select a,b from users", dialect=Dialect.POSTGRESQL))
```

Source installs compile the same C engine. Neither binding depends on gotel.
Gotel imports sequel directly and owns the OpenTelemetry adapters.

## Rust

```toml
[dependencies]
sequel = { git = "https://github.com/zfogg/sequel", tag = "v0.2.0" }
```

```rust
let options = sequel::AnalysisOptions {
    dialect: sequel::Dialect::PostgreSql,
    format: true,
    ..Default::default()
};
let query = sequel::analyze("SELECT * FROM users WHERE id=$1", &options)?;
query.check_binds(0, 1)?;
let html = sequel::highlight(&query.formatted, &Default::default())?;
# Ok::<(), sequel::Error>(())
```

The safe API owns its results, validates UTF-8/UTF-16 boundaries, contains callback
panics and supports the same budgets, multiple diagnostics, fingerprints, maps,
bind metadata and Base16 highlighting as C. The source crate includes C code and
licenses; Cargo needs a C11 compiler but no CMake, libclang or installed CLI.
[Rust details](rust/README.md) also explain using a matching prebuilt release.
Run `cargo test --locked` and `cargo clippy --all-targets -- -D warnings`.

## Syntax highlighting

`highlight` preserves SQL spelling and whitespace. It does not sanitize literals
or comments. To format and highlight a query, connect the two commands:

```sh
printf '%s' 'select id,name from users where id=:id' \
  | sequel format --dialect postgresql \
  | sequel highlight --dialect postgresql --colorscheme solarized-dark > query.html

sequel highlight --ansi --dark --colorscheme gruvbox-dark-medium query.sql
sequel highlight --light --color-variable '#123000' --color-comment '#666666' query.sql
sequel highlight --colorscheme ./my-base16.yaml query.sql
sequel highlight --list-colorschemes
```

Input can be piped/redirected stdin, one SQL string, or one or more file paths.
Files are processed in argument order. `--edit-file` explicitly writes the
highlighted result back to each file instead of stdout. `--embedded` is for
format/sanitize; highlight takes SQL directly.

`--html` (the default) and `--ansi` are mutually exclusive. HTML is a standalone
`<pre class="sequel"><code>…</code></pre>` fragment with escaped source, inline
colors and `sequel-keyword`, `sequel-string`, etc. span classes. It can be opened
in a browser or embedded into an existing page without a CSS dependency. ANSI
uses 24-bit foreground/background SGR sequences and ends with a full reset.
Both modes reject embedded terminal controls other than tabs and newlines,
malformed quotes/comments, invalid UTF-8, input over 1 GiB.
Highlighting accepts incomplete SQL grammar, including unmatched parentheses.

The default palette is `default-dark`. There are 360 bundled Base16 schemes,
including Chris Kempson's default palettes and the community palettes maintained
by [Tinted Theming](https://github.com/tinted-theming/schemes). Any legacy Base16
YAML file (`scheme`, `author`, `base00`…`base0F`) or current YAML file with a
`palette` block can also be loaded. Palette lookup never downloads anything.
Vendored source, author credits and the pinned upstream commit are recorded in
`third_party/base16.json`; the MIT license is included alongside it.

`--light` and `--dark` are mutually exclusive. Without either, the selected
palette is preserved. An explicit appearance selects its actual published
counterpart: for example `solarized-dark --light` uses `solarized-light`,
`gruvbox-dark-hard --light` uses `gruvbox-light-hard`, and
`catppuccin-mocha --light` uses `catppuccin-latte`. All sixteen palette colors
come from that variant, including its accent colors. Nothing is inverted or
synthesized. If a palette has no matching published variant, sequel reports an
error instead. Family mappings, including nonstandard variant names, are explicit
in the palette-table generator. A custom YAML palette can keep its natural
appearance; switching requires a recognized bundled counterpart. Alternatively,
pass the desired light or dark YAML file directly.

Colors can be overridden with `--color-TYPE '#RRGGBB'` (or `--color-TYPE=RRGGBB`).
Types are `foreground`, `background`, `keyword`, `type`, `function`, `variable`,
`string`, `number`, `boolean`, `null`, `comment`, `operator`, `punctuation`, and
`identifier`. Overrides always apply after the selected palette variant, regardless
of argument order. Variables include bind markers such as `?`, `$1`, `:name`,
`@name`, `%s`, `%(name)s`, SQLite Tcl-style variables and ClickHouse typed parameters.
Classification is lexical; it does not resolve SQL schemas or distinguish a table
name from a column name. Functions are recognized by call syntax.

### Library APIs

The C API uses the same caller-owned buffer/measurement convention as formatting:

```c
sequel_highlight_options_t options;
sequel_highlight_options_init(&options); /* HTML, generic SQL, default-dark */
options.dialect = SEQUEL_POSTGRESQL;
sequel_colorscheme_t palette;
sequel_colorscheme_for_appearance("solarized-dark", SEQUEL_APPEARANCE_LIGHT, &palette);
sequel_highlight_set_colorscheme(&options, &palette);
sequel_highlight_set_color(&options, "variable", "#123000");
size_t length;
sequel_highlight(sql, &options, NULL, 0, &length);
char *html = malloc(length + 1);
sequel_highlight(sql, &options, html, length + 1, &length);
/* Check every status in real code; free(html) when finished. */
```

`sequel_colorscheme_parse` loads a YAML string without filesystem I/O.
`sequel_colorscheme_variant_name` resolves a published counterpart (or returns
NULL). `sequel_colorscheme_for_appearance` loads that variant directly.
`sequel_colorscheme_count`/`sequel_colorscheme_name` enumerate bundled schemes;
`sequel_highlight_color_name` enumerates token-color names. All options and
palettes are caller-owned; no global state is mutated.

```go
formatted, err := sequel.FormatSQL(sequel.DialectPostgreSQL, sql)
// Handle err before highlighting.
html, err := sequel.HighlightSQL(sequel.DialectPostgreSQL, formatted, sequel.HighlightOptions{
    Colorscheme: "solarized-dark",
    Appearance: sequel.AppearanceLight,
    Colors: map[string]string{"variable": "#123000"},
})
```

```python
from sequel import Appearance, Dialect, format_sql, highlight_sql

html = highlight_sql(
    format_sql(sql, dialect=Dialect.POSTGRESQL),
    dialect=Dialect.POSTGRESQL,
    colorscheme="solarized-dark",
    appearance=Appearance.LIGHT,
    colors={"variable": "#123000"},
)
```

Go's `HighlightOptions.Mode` and Python's `mode` select `html` or `ansi`;
`SchemeYAML`/`scheme_yaml` load a custom palette string. Both expose the complete
palette registry through `Colorschemes()`/`colorschemes()`. Python implementations
live in `highlighting.py`, `formatting.py`, `sanitization.py`, `metadata.py` and
`dialect.py`; `__init__.py` only re-exports the public API.

The checked-in [HTML and ANSI examples](examples/highlight/README.md) are tested
against the C CLI, Go binding and Python binding. Regenerate them with
`uv run python scripts/highlight_samples.py build/sequel` (`build/sequel.exe` on
Windows). To refresh bundled palettes, clone the Tinted Theming schemes repository
and run `uv run python scripts/base16.py --upstream PATH`. Regenerating the C table
from vendored data requires only `uv run python scripts/base16.py`.

## Diagnostics and interactive editing

```sh
sequel explain "SELECT 'unfinished"
sequel explain --json --dialect postgresql query.sql
sequel highlight --tolerant --html "SELECT 'unfinished" > preview.html
sequel interactive --colorscheme solarized-dark --dark
# Equivalent: sequel highlight --interactive
```

`explain` reads stdin, a SQL string, or one or more files, like the other commands.
It reports a diagnostic code, severity, source range, explanation and suggested
fix. `--json` emits one JSON object per input. Issues exit nonzero; incomplete
input is a warning with a nonzero exit status. Formatting and strict highlighting
also show these explanations on stderr without emitting partial results.
Explanations never interpolate SQL contents. They check lexical constructs and
delimiter balance, not database grammar, schemas or execution plans; no SQL is
executed. For example, ClickHouse `INSERT ... FORMAT` payloads are explained as
unsupported data rather than accidentally processed as SQL.

Locations use zero-based UTF-8 byte offsets with an exclusive end. Lines and
columns are one-based; a Unicode scalar counts as one column, a tab counts as one
column, and CRLF counts as one newline. Limits (1 GiB, 64 levels)
and malformed UTF-8 or terminal controls have distinct diagnostic codes.

`--tolerant` highlights unfinished strings, dollar strings, quoted identifiers,
nested block comments and dialect-specific bind parameters through EOF, in
their usual token colors. It preserves the exact source without inserting a
closing quote or comment. HTML remains escaped and ANSI ends with a reset.
Invalid UTF-8, terminal controls, resource limits and unsupported data payloads
remain errors. Strict mode is still the default. Use `explain` for warnings
alongside tolerant output; output on stdout remains suitable for pipelines.

`interactive` is a line-oriented multiline workbench, with ANSI previews by
default. It accepts all highlighting color, palette and appearance options;
`--html` explicitly selects HTML previews. Each SQL line previews the accumulated
buffer and reports any issue on stderr. A final semicolon outside quotes/comments,
with balanced delimiters, marks a statement complete. The next SQL line then
starts a fresh buffer. Commands can still inspect the completed buffer:

| Command | Action |
| --- | --- |
| `:show` | Preview the current buffer |
| `:explain` | Show its diagnostic and completion state |
| `:format` | Format and preview it; retain the original if formatting fails |
| `:clear` | Discard the buffer and start again |
| `:help` | Show commands |
| `:quit` / `:q` | Exit (EOF also exits) |

Exact command lines are reserved even inside unfinished SQL, so `:clear` always
allows recovery. Other colon-prefixed lines are SQL, including bind markers.
Terminal sessions have `sql>` and continuation prompts on stderr; scripted
sessions have no prompts. `--json` makes explanations machine-readable, while
previews retain their chosen HTML/ANSI mode. Interactive mode reads stdin and
does not edit files or extract embedded host-language SQL.

The C APIs `sequel_diagnose` and `sequel_diagnose_n` fill a caller-owned
`sequel_diagnostic_t`; the length-based form also accepts nonterminated buffers
and diagnoses embedded NULs. `sequel_format_ex` and `sequel_highlight_ex` report
transformation errors in that same structure. Set
`sequel_highlight_options_t.tolerant = true` for editing; successful tolerant
highlighting can return an incomplete warning in the diagnostic. Group balance
is checked by `sequel_diagnose`, independently of highlighting. The `_ex` APIs
accept a NULL diagnostic pointer when explanations are not needed.

```python
from sequel import SQLDiagnosticError, diagnose, format_sql, highlight_sql

issue = diagnose("SELECT 'unfinished")
assert issue.code == "unterminated_string" and issue.incomplete
preview = highlight_sql("SELECT 'unfinished", tolerant=True)
try:
    format_sql("SELECT 'unfinished")
except SQLDiagnosticError as error:
    print(error.diagnostic.line, error.diagnostic.column, error.diagnostic.hint)
```

Go exposes `Diagnose(dialect, sql) Diagnostic` and
`HighlightOptions{Tolerant: true}`. SQL failures from formatting/highlighting
are `*DiagnosticError`, accessible with `errors.As`. Python exposes an immutable
`Diagnostic` from `diagnostics.py`, `diagnose`, `tolerant=True`, and
`SQLDiagnosticError` (a `ValueError` subclass). A diagnostic code of `none` means
no issue was found; `statement_complete` records the final-semicolon state.

## Tests and provenance

Extracted from gotel with its MIT license and utf8proc's license preserved.
The CTest suite owns the formatting, sanitization, ClickHouse, adversarial,
mutation and production regression corpora, plus API and CLI tests. Go and Python
own their binding tests here. Former Go summary tables and the complete named
SQLC corpus also run through the public C API, independently of the bindings.

## Hosted API documentation

The [documentation site](https://zfogg.github.io/sequel/) links the
[C](https://zfogg.github.io/sequel/c/),
[Python](https://zfogg.github.io/sequel/python/) and
[Go](https://zfogg.github.io/sequel/go/) references. C uses Doxygen, Python uses
pdoc, and Go uses Golds. Public source comments are the API documentation;
the site adds shared language navigation and a source-revision marker.

Install Doxygen, Go and uv, then build without configuring the native library:

```sh
cmake -S docs -B build/docs
cmake --build build/docs --target docs --parallel 2
uv run python -m http.server 8000 --directory build/docs/site
```

Open `http://localhost:8000/`. The `docs-c`, `docs-python` and `docs-go` targets
build individual references; `docs-site` assembles the site and `docs-check`
checks language entry points and local page/asset links. `docs` does all of them.
These targets are also available from the native build with `SEQUEL_BUILD_DOCS=ON`.
`DOCS_OUTPUT_DIR` overrides the output directory.

The Documentation workflow validates pull requests and publishes main-branch
updates through GitHub Pages' artifact deployment. It verifies the live landing
page, language endpoints, representative API pages and deployed commit. With
authenticated GitHub CLI access, `cmake --build build/docs --target docs-publish`
dispatches that workflow for remote `main`, waits and verifies publication.
Deployment is restricted to `main`; dispatch the workflow directly for other
build-only refs. Commit and push before publishing; generated HTML stays out of Git.

## Development tools

Run `uv sync --extra test --locked`. The locked development tools include
clang-format, Ruff and ty; gofmt comes with Go. `uv run python scripts/style.py`
checks C and Go formatting, Python formatting/lint and types. Add `--fix` to
apply formatting. CI runs those checks, static/shared C tests, binding tests,
three Go fuzz targets, and AddressSanitizer/UndefinedBehaviorSanitizer tests.
After editing native sources, run `uv run python scripts/sync_native.py` to
refresh cgo's engine fingerprint. Python's uv cache tracks the C sources too.
