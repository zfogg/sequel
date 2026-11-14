# sequel binary

[Documentation](https://zfogg.github.io/sequel/) ·
[C API](https://zfogg.github.io/sequel/c/) ·
[Python API](https://zfogg.github.io/sequel/python/) ·
[Go API](https://zfogg.github.io/sequel/go/)

Build the standalone C executable from a complete repository checkout using
CMake 3.24+ and a C11 compiler. No Go toolchain or network download is required.
The binary embeds the sequel library and the bundled utf8proc sources.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Python 3 is used only for black-box tests. For a build without tests, configure
with `-DSEQUEL_BUILD_TESTS=OFF`. Install with `cmake --install build --config
Release --prefix /your/prefix`. The executable is `build/sequel` (`build/sequel.exe` on Windows), including
with multi-configuration generators such as Visual Studio.

```sh
echo 'select a,b from users where id=42' | build/sequel format --dialect postgresql
build/sequel sanitize "select * from users where id=42"
build/sequel format --dialect postgresql --edit-file queries.sql
```

`format` and `sanitize` accept one SQL argument or piped/redirected stdin and write
to stdout. Without an argument, interactive terminal input prints usage and exits
with an error immediately. Use `--` before an SQL argument starting with `-`.
Dialects: `generic` (default), `postgresql`, `sqlite`, `mysql`, `duckdb`, `clickhouse`.
Input reads are bounded to 1 GiB plus one byte to detect overflow. Invalid quoting,
invalid UTF-8, embedded NULs and lexer limits fail before edited files are written.
Formatting is lexical, not SQL validation.

`--edit-file PATH` conservatively collapses existing horizontal gaps between
tokens to one space. Newlines, blank lines, indentation, comments, literals and
trailing whitespace are preserved byte for byte, including CRLF and missing final
newlines. Multiple statements are lexed together, without splitting semicolons
inside quoted text or comments. This mode does not reflow clauses like stdout
formatting. The complete result is written and flushed to an exclusive temporary
sibling before replacing the original, retaining permission bits. Symlinks and
nonregular files are rejected. Replacement changes the inode; other hard links
retain their original contents.

## SQL embedded in source files

Use `--embedded` to format SQL inside strings and comments while returning the
complete source file. A positional argument is a filename in this mode, not SQL.
Without `--edit-file`, the input file is unchanged and the result goes to stdout.

```sh
build/sequel format --embedded --dialect postgresql queries.go
build/sequel format --embedded --dialect sqlite --edit-file database.c
cat queries.txt | build/sequel format --embedded --language TypeScript
```

`--language` accepts `go`, `c`, `c++`, `cpp`, `python`, `javascript`, `typescript`,
`rust`, `haskell`, and `clojure`, in any capitalization (`Go`, `Python`,
`TypeScript`, `C++`, etc.). Otherwise the file extension selects the language.
Piped or redirected source needs `--language`; interactive stdin is rejected.
Unknown languages and extensions are errors. Scheme is not supported.

The lexical scanner recognizes ordinary quoted strings, Go raw strings, Python
triple/raw strings, static JavaScript templates, C++ raw strings, Rust raw strings
with hash delimiters, Haskell quasiquotes, and each language's line/block comments. It supports nested
Rust/Haskell block comments and preserves those nested blocks verbatim. Clojure
semicolon comments, regex strings, and character literals are distinguished from
ordinary strings. Adjacent C/Python literals and literal concatenations in Go,
JavaScript/TypeScript and Haskell can be combined into one formatted literal.
Escaped strings retain valid host-language escaping; multiline strings can retain
physical SQL line breaks. SQL line comments retain the host comment prefix.

Detection requires a complete string or comment body that starts with recognizable
SQL, such as `SELECT ... FROM`, `INSERT ... INTO`, `UPDATE ... SET`, or a CTE.
It does not search arbitrary prose or evaluate code. Non-SQL contents, identifiers,
SQL literals, parameters, and source outside the selected literal groups and
comments are preserved. Formatting may change only SQL whitespace in decoded query values.

This is a conservative lexical transformation, not a complete host-language parser.
Python f/byte strings, regex literals, printf-style directives, unsupported escape
sequences, GORM `{{...}}` directives, dynamic concatenations, recognized format
strings, and interpolated JavaScript templates (`${...}`) are left unchanged.
Unterminated recognized strings/comments/templates, invalid UTF-8, and limit
violations reject the whole transformation before any file is written. The input limit remains
1 GiB; embedded output is bounded to 64 MiB. Existing file replacement, permission
preservation and symlink rejection apply to `--edit-file` in embedded mode too.

## Input and output modes for both commands

`format` and `sanitize` read stdin when piped or redirected, accept one SQL string,
or read one or more paths in argument order. Existing paths are files; multiple
arguments are paths. A single `.sql` path without whitespace is treated as a file even when missing.
Use `--` before input starting with `-`. Results go to stdout by default, with
multiple file results concatenated in argument order. Only `--edit-file` enables
in-place writes; it accepts one or more paths for either command. Each file is
replaced atomically after successful processing. Earlier successful files remain
processed if a later file fails.

```sh
sequel format one.sql two.sql
sequel sanitize --edit-file one.sql two.sql
sequel sanitize --remove-comments "SELECT 'keep', $1 /* remove */"
sequel sanitize --replace-literals "SELECT 'remove', $1 /* keep */"
sequel sanitize --embedded --edit-file queries.go queries.py
cat queries.go | sequel sanitize --embedded --language go
sequel sanitize --embedded --language go 'const q = "SELECT * FROM t WHERE x=42"'
```

`--embedded` supports both commands and the same host languages. It transforms
recognized SQL in strings and comments while preserving source outside those
fragments. Explicit `--language` is required for embedded stdin or source strings;
file extensions otherwise select it. Conservative detection and unsupported
interpolated/template forms are retained, so embedded sanitization is not a
whole-file secrets scrubber. Malformed recognized SQL rejects sanitization before
writing a file. Selection flags also apply inside embedded SQL.

## Diagnostics and the SQL workbench

`explain` reports the first lexical/delimiter diagnostic and completion state;
`diagnose` is an alias. Their default `--json` output remains one diagnostic
object per input. Add `--all` to collect multiple issues at safe statement
boundaries without exporting SQL text or bind values:

```sh
sequel diagnose --all --json --dialect postgresql queries.sql
printf 'SELECT ); SELECT ];' | sequel explain --all
```

`--all --json` emits one versioned envelope per input:

```json
{"schema_version":1,"original_length":9,"status":"malformed","diagnostics":[{"code":"unmatched_close"}]}
```

The example abbreviates each diagnostic; actual entries also contain severity,
original UTF-8 byte ranges, Unicode scalar line/columns, incomplete/completion
flags, and canonical catalog messages, explanations, and hints. `original_length`
is the input byte count, including embedded NULs. `status` distinguishes `ok`,
`malformed`, `limit`, `cancelled`, and `error`; valid input has an empty diagnostics
array. Text mode prints every issue. Malformed input returns a nonzero exit code;
multiple paths are processed in order, each producing its own result.

Recovery never splits semicolons inside quotes, comments, or opaque parameter
syntax. An unfinished quoted tail prevents recovery into apparent statements
inside that tail. Diagnostic output contains canonical explanations and numeric
locations, not original SQL snippets. These are lexical and delimiter checks,
not database grammar validation. `--all` does not edit files or process embedded
host-language source.

The `interactive` workbench accepts `:explain-all` for the same multi-issue view;
`--json` selects the versioned envelope for that command. Existing `:explain`
keeps its first-issue/completion output. Workbench `:show`, previews, formatting,
and highlighting preserve SQL content; diagnostic redaction does not change
those explicit display commands.
