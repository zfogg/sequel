# sequel

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
Formatting does not sanitize or execute SQL. See [CLI details](src/binary/README.md)
for embedded source formatting and conservative file editing.

## C API

Include `<sequel.h>` and link `sequel::sequel`. All public types and functions
use the `sequel_` prefix and constants use `SEQUEL_`.

```c
sequel_options_t options = {.dialect = SEQUEL_POSTGRESQL};
sequel_metadata_t query;
if (sequel_inspect("SELECT * FROM users WHERE id=$1", &options, &query) == SEQUEL_OK) {
  puts(query.text);     /* SELECT * FROM users WHERE id=? */
  puts(query.summary);  /* SELECT … FROM users WHERE … */
}
```

`sequel_inspect` returns bounded UTF-8 text, summary, operation, statement count
and truncation status. `sequel_sanitize` accepts a caller-sized buffer and limit.
`sequel_format` supports a measure pass with `out=NULL, capacity=0` before allocating.
`sequel_compact` preserves newlines while collapsing horizontal token gaps.
Malformed quoting fails closed. Input is limited to 1 MiB and nesting to 64 levels.
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
from sequel import Dialect, inspect, format_sql
query = inspect("SELECT * FROM users WHERE token='secret'", dialect=Dialect.POSTGRESQL)
print(query.text, query.summary)
print(format_sql("select a,b from users", dialect=Dialect.POSTGRESQL))
```

Source installs compile the same C engine. Neither binding depends on gotel.
Gotel imports sequel directly and owns the OpenTelemetry adapters.

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
malformed quotes/comments, invalid UTF-8, input over 1 MiB or over 65536 tokens.
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
live in `highlighting.py`, `formatting.py`, `sanitization.py`, `inspection.py` and
`dialect.py`; `__init__.py` only re-exports the public API.

The checked-in [HTML and ANSI examples](examples/highlight/README.md) are tested
against the C CLI, Go binding and Python binding. Regenerate them with
`uv run python scripts/highlight_samples.py build/sequel` (`build/sequel.exe` on
Windows). To refresh bundled palettes, clone the Tinted Theming schemes repository
and run `uv run python scripts/base16.py --upstream PATH`. Regenerating the C table
from vendored data requires only `uv run python scripts/base16.py`.

## Tests and provenance

Extracted from gotel with its MIT license and utf8proc's license preserved.
The CTest suite owns the formatting, sanitization, ClickHouse, adversarial,
mutation and production regression corpora, plus API and CLI tests. Go and Python
own their binding tests here. Former Go summary tables and the complete named
SQLC corpus also run through the public C API, independently of the bindings.

## Development tools

Run `uv sync --extra test --locked`. The locked development tools include
clang-format, Ruff and ty; gofmt comes with Go. `uv run python scripts/style.py`
checks C and Go formatting, Python formatting/lint and types. Add `--fix` to
apply formatting. CI runs those checks, static/shared C tests, binding tests,
three Go fuzz targets, and AddressSanitizer/UndefinedBehaviorSanitizer tests.
After editing native sources, run `uv run python scripts/sync_native.py` to
refresh cgo's engine fingerprint. Python's uv cache tracks the C sources too.
