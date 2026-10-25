# sequel

A C11 library and command-line program for SQL formatting, sanitization and
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
