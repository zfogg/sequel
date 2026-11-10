# sequel for Python

[Documentation](https://zfogg.github.io/sequel/) ·
[Python API](https://zfogg.github.io/sequel/python/) ·
[Source on GitHub](https://github.com/zfogg/sequel)

The `sequel` Python package provides SQL formatting, sanitization, inspection,
structural analysis, diagnostics, and syntax highlighting backed by the shared
Sequel C engine. It supports generic SQL, PostgreSQL, SQLite, MySQL, DuckDB, and
ClickHouse. The package requires Python 3.11 or newer.

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
