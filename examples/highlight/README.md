# SQL highlighting samples

[Documentation](https://zfogg.github.io/sequel/) ·
[C API](https://zfogg.github.io/sequel/c/) ·
[Python API](https://zfogg.github.io/sequel/python/) ·
[Go API](https://zfogg.github.io/sequel/go/)

Each example formats [query.sql](query.sql) with the PostgreSQL dialect and then
highlights the formatted output. The SQL includes a comment, CTE, function,
type cast, bind marker, string with HTML characters, number, boolean, NULL and
quoted identifier.

- [Dark HTML](dark.html), [light HTML](light.html): open directly in a browser.
- [Dark ANSI](dark.ansi), [light ANSI](light.ansi): write to a truecolor terminal.

```sh
sequel format --dialect postgresql query.sql \
  | sequel highlight --dialect postgresql --html --light > light.html
sequel format --dialect postgresql query.sql \
  | sequel highlight --dialect postgresql --ansi --dark > dark.ansi
```

These are deterministic regression fixtures as well as examples. CTest exercises
the actual stdout/stdin pipe; Go and Python check the same files through their
library APIs. Run `uv run python scripts/highlight_samples.py build/sequel` from
the repository root to regenerate them.
