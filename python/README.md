# sequel for Python

[Documentation](https://zfogg.github.io/sequel/) ·
[Python API](https://zfogg.github.io/sequel/python/) ·
[Source on GitHub](https://github.com/zfogg/sequel)

The `sequel` Python package provides SQL formatting, sanitization, metadata,
structural analysis, diagnostics, and syntax highlighting backed by the shared
Sequel C engine. It supports generic SQL, PostgreSQL, SQLite, MySQL, DuckDB, and
ClickHouse. The package requires Python 3.11 or newer.

## Features

- `format_sql` and `format_sql_or_original` format SQL while preserving literals,
  comments, quoted identifiers, and bind markers; formatting never executes SQL.
- `sanitize` removes comments and replaces literals and bind markers with bounded
  placeholders, with selective flags and UTF-8-aware input/output limits.
- `inspect` returns immutable query text, structural summaries, operations,
  statement counts, truncation metadata, and dialect-aware safety checks without
  accepting or retaining bind values.
- `analyze` provides sanitized text, fingerprints, statement ranges, bind
  descriptors, source maps, optional formatting, cancellation, and native work,
  memory, input, and output budgets.
- `diagnose`, `diagnose_all`, `diagnostic_catalog`, and diagnostic validation
  provide canonical lexical/delimiter diagnostics with safe ranges and hints.
- `highlight_sql` supports terminal and HTML highlighting, appearance modes, and
  bundled color schemes for editor and command-line integrations.
- `utf8_to_utf16` and `utf16_to_utf8` convert document offsets for browser/editor
  APIs; `check_binds` validates statement bind arity without reading values.
- The shared native engine is bundled in wheels, is typed (`py.typed`), and
  exposes the same contracts used by the C, Go, Rust, and CLI implementations.

## Installation

```sh
uv add sequel
```

## Quick start

```python
import sequel

query = sequel.inspect(
    "SELECT * FROM users WHERE id = 42",
    dialect=sequel.Dialect.POSTGRESQL,
)
print(query.text)
print(query.summary)

formatted = sequel.format_sql(
    "select a,b from users where id=42",
    dialect=sequel.Dialect.POSTGRESQL,
)
safe = sequel.sanitize("SELECT * FROM users WHERE token = 'secret'")
```

`inspect` removes literals, comments, and bind markers while preserving safe
query structure. `format_sql` formats SQL without executing it, and `sanitize`
produces a bounded representation suitable for telemetry or diagnostics.

The public API also includes `analyze` and `diagnose` for structural analysis,
source maps, bind metadata, canonical diagnostics, and editor integrations, plus
`highlight_sql` for terminal and HTML syntax highlighting.

See the [full project README](https://github.com/zfogg/sequel#readme) for the
native C, Go, Rust, CLI, and release documentation.
