"""CLI, rendering, dialect, palette, safety and real format|highlight pipelines."""

import json
import random
import re
import subprocess
import sys
import tempfile
import unittest
from html.parser import HTMLParser
from pathlib import Path

BINARY = str(Path(sys.argv.pop(1)).resolve())
ROOT = Path(__file__).resolve().parents[1]
SGR = re.compile(rb"\x1b\[[0-9;]*m")


def run(*args, data=b""):
    return subprocess.run([BINARY, *args], input=data, capture_output=True, timeout=15)


class Fragment(HTMLParser):
    def __init__(self, html):
        super().__init__(convert_charrefs=True)
        self.source = ""
        self.tokens = []
        self.kind = None
        self.style = None
        self.tags = []
        self.feed(html)

    def handle_starttag(self, tag, attrs):
        assert tag in ("pre", "code", "span"), tag
        self.tags.append(tag)
        if tag == "span":
            attrs = dict(attrs)
            self.kind = attrs["class"].removeprefix("sequel-")
            self.style = attrs["style"].removeprefix("color:")

    def handle_endtag(self, tag):
        if tag == "span":
            self.kind = None

    def handle_data(self, data):
        self.source += data
        if self.kind:
            self.tokens.append((self.kind, data, self.style))


class HighlightTests(unittest.TestCase):
    def html(self, sql, *args):
        result = run("highlight", *args, data=sql.encode())
        self.assertEqual(result.returncode, 0, result.stderr)
        fragment = Fragment(result.stdout.decode())
        self.assertEqual(fragment.source, sql)
        self.assertNotIn(b"\x1b", result.stdout)
        return fragment, result.stdout

    def test_categories_and_overrides(self):
        sql = "select sum(price::decimal), :id, 'hello', 1.25e-2, true, NULL /* note */ from users;"
        colors = [
            "foreground",
            "background",
            "keyword",
            "type",
            "function",
            "variable",
            "string",
            "number",
            "boolean",
            "null",
            "comment",
            "operator",
            "punctuation",
            "identifier",
        ]
        args = [arg for i, kind in enumerate(colors) for arg in (f"--color-{kind}", f"#{i + 1:06x}")]
        fragment, rendered = self.html(sql, *args)
        found = {kind for kind, _, _ in fragment.tokens}
        self.assertEqual(found, set(colors) - {"foreground", "background"})
        for kind, _, color in fragment.tokens:
            self.assertEqual(color, f"#{colors.index(kind) + 1:06x}")
        self.assertIn(b"color:#000001;background-color:#000002", rendered)
        result = run("highlight", "--ansi", *args, data=sql.encode())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(SGR.sub(b"", result.stdout), sql.encode())
        self.assertIn(b"\x1b[38;2;0;0;6m:id", result.stdout)
        self.assertTrue(result.stdout.endswith(b"\x1b[0m"))

    def test_dialects_opaque_literals_comments_and_bind_markers(self):
        cases = [
            (
                "postgresql",
                "SELECT $tag$<b>&'quoted'$tag$, E'it\\'s', U&'hello', $1, :user, %(name)s, %s;",
                [
                    ("string", "$tag$<b>&'quoted'$tag$"),
                    ("variable", "$1"),
                    ("variable", "%(name)s"),
                    ("variable", "%s"),
                ],
            ),
            (
                "postgresql",
                "SELECT document ? 'key', document ?| array['a'], id::int FROM t",
                [("operator", "?"), ("operator", "?|"), ("operator", "::"), ("type", "int")],
            ),
            (
                "sqlite",
                'SELECT [strange]]name], "quoted", `backtick`, ?12, $ns::name(key), @name, :name;',
                [("identifier", "[strange]]name]"), ("variable", "$ns::name(key)"), ("variable", "?12")],
            ),
            (
                "mysql",
                "SELECT _utf8'héllo', \"string\", `column`, @@version, @user, ? # line\r\nFROM t;",
                [("string", "_utf8'héllo'"), ("string", '"string"'), ("variable", "@@version"), ("comment", "# line")],
            ),
            (
                "clickhouse",
                "SELECT ‘string’, “identifier”, {ids:Array(UInt64)}, 0x1.fp+2 // note\nFROM t FINAL",
                [
                    ("string", "‘string’"),
                    ("identifier", "“identifier”"),
                    ("variable", "{ids:Array(UInt64)}"),
                    ("number", "0x1.fp+2"),
                ],
            ),
            (
                "duckdb",
                "SELECT list_transform([1,2], x -> x + .5), $$hello$$ FROM read_parquet('file') QUALIFY n>0;",
                [("function", "list_transform"), ("number", ".5"), ("string", "$$hello$$"), ("keyword", "QUALIFY")],
            ),
            (
                "generic",
                "/* outer /* nested */ end */\nselect \"SELECT\", '-- not a comment', λ, 日本語, 😀 from t -- tail\r\n",
                [
                    ("comment", "/* outer /* nested */ end */"),
                    ("identifier", '"SELECT"'),
                    ("string", "'-- not a comment'"),
                ],
            ),
        ]
        for dialect, sql, expected in cases:
            with self.subTest(dialect=dialect):
                fragment, _ = self.html(sql, "--dialect", dialect)
                tokens = [(kind, text) for kind, text, _ in fragment.tokens]
                for token in expected:
                    self.assertIn(token, tokens)
                ansi = run("highlight", "--ansi", "--dialect", dialect, data=sql.encode())
                self.assertEqual(ansi.returncode, 0, ansi.stderr)
                self.assertEqual(SGR.sub(b"", ansi.stdout), sql.encode())

    def test_html_escaping_and_no_terminal_sequences(self):
        sql = "SELECT '<script>alert(\"x\")</script>&' AS \"<img src=x onerror=alert(1)>\"; -- <b> & 'x'"
        _, html = self.html(sql)
        self.assertNotIn(b"<script>", html)
        self.assertNotIn(b"<img ", html)
        for entity in (b"&lt;", b"&gt;", b"&amp;", b"&quot;", b"&#39;"):
            self.assertIn(entity, html)
        ansi = run("highlight", "--ansi", data=sql.encode())
        self.assertEqual(SGR.sub(b"", ansi.stdout), sql.encode())
        self.assertIn(b"<script>", ansi.stdout)

    def test_stdin_string_redirected_and_multiple_paths(self):
        sql = "\tSELECT\r\n  '🙂 & < >', :id;  \n"
        expected = run("highlight", data=sql.encode())
        self.assertEqual(expected.returncode, 0, expected.stderr)
        self.assertEqual(run("highlight", sql).stdout, expected.stdout)
        with tempfile.TemporaryDirectory() as directory:
            paths = [Path(directory) / f"日本語 {i}.sql" for i in range(2)]
            for path in paths:
                path.write_bytes(sql.encode())
            with paths[0].open("rb") as stdin:
                result = subprocess.run([BINARY, "highlight"], stdin=stdin, capture_output=True, timeout=10)
            self.assertEqual(result.stdout, expected.stdout)
            result = run("highlight", *map(str, paths))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, expected.stdout * 2)
            self.assertEqual(paths[0].read_bytes(), sql.encode())
            result = run("highlight", "--edit-file", *map(str, paths))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, b"")
            self.assertTrue(all(p.read_bytes() == expected.stdout for p in paths))

    def test_path_errors_do_not_replace_sql(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "invalid.sql"
            source = b"SELECT 'unterminated"
            path.write_bytes(source)
            result = run("highlight", "--edit-file", str(path))
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, b"")
            self.assertEqual(path.read_bytes(), source)
            self.assertEqual(list(Path(directory).glob("*.tmp")), [])
            result = run("highlight", str(Path(directory) / "missing.sql"))
            self.assertNotEqual(result.returncode, 0)
        sql = "-- comment\nSELECT 1"
        result = run("highlight", "--", sql)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(Fragment(result.stdout.decode()).source, sql)

    def test_production_query_corpus_roundtrips(self):
        for path in sorted((ROOT / "tests/stonkpile").glob("*.sql")):
            sql = path.read_bytes()
            for mode in ("--html", "--ansi"):
                with self.subTest(path=path.name, mode=mode):
                    result = run("highlight", "--dialect=postgresql", mode, data=sql)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    plain = (
                        Fragment(result.stdout.decode()).source.encode()
                        if mode == "--html"
                        else SGR.sub(b"", result.stdout)
                    )
                    self.assertEqual(plain, sql)

    def test_appearance_and_override_order(self):
        sql = "SELECT :id"
        for mode in ("--html", "--ansi"):
            for name, natural in (("default-dark", "--dark"), ("default-light", "--light")):
                original = run("highlight", mode, "--colorscheme", name, data=sql.encode())
                explicit = run("highlight", mode, natural, "--colorscheme", name, data=sql.encode())
                self.assertEqual(original.stdout, explicit.stdout)
                self.assertEqual(explicit.returncode, 0, explicit.stderr)
            light = run("highlight", mode, "--light", data=sql.encode())
            light_scheme = run("highlight", mode, "--colorscheme=default-light", data=sql.encode())
            self.assertEqual(light.stdout, light_scheme.stdout)
        first = run(
            "highlight", "--color-variable=#123000", "--light", "--colorscheme=solarized-dark", data=sql.encode()
        )
        last = run(
            "highlight", "--colorscheme", "solarized-dark", "--light", "--color-variable", "#123000", data=sql.encode()
        )
        self.assertEqual(first.stdout, last.stdout)
        self.assertIn(b'color:#123000">:id', first.stdout)

    def test_actual_published_variants(self):
        pairs = [
            ("default-dark", "default-light"),
            ("solarized-dark", "solarized-light"),
            ("gruvbox-dark-hard", "gruvbox-light-hard"),
            ("atelier-cave", "atelier-cave-light"),
            ("catppuccin-mocha", "catppuccin-latte"),
            ("rose-pine", "rose-pine-dawn"),
            ("tokyo-night-dark", "tokyo-night-light"),
        ]
        for dark, light in pairs:
            for mode in ("--html", "--ansi"):
                dark_output = run("highlight", mode, "--colorscheme", dark, data=b"SELECT :id, 'x'")
                light_output = run("highlight", mode, "--colorscheme", light, data=b"SELECT :id, 'x'")
                selected_light = run("highlight", mode, "--colorscheme", dark, "--light", data=b"SELECT :id, 'x'")
                selected_dark = run("highlight", mode, "--colorscheme", light, "--dark", data=b"SELECT :id, 'x'")
                self.assertEqual(selected_light.returncode, 0, selected_light.stderr)
                self.assertEqual(selected_dark.returncode, 0, selected_dark.stderr)
                self.assertEqual(selected_light.stdout, light_output.stdout, dark)
                self.assertEqual(selected_dark.stdout, dark_output.stdout, light)
        for name, flag in (("monokai", "--light"), ("qualia", "--light"), ("github", "--dark")):
            # github does have a dark counterpart; select it rather than invert.
            result = run("highlight", "--colorscheme", name, flag, data=b"SELECT 1")
            if name == "github":
                self.assertEqual(
                    result.stdout, run("highlight", "--colorscheme", "github-dark", data=b"SELECT 1").stdout
                )
            else:
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b"no published variant", result.stderr)
                self.assertEqual(result.stdout, b"")

    def test_every_bundled_palette_and_yaml_files(self):
        data = json.loads((ROOT / "third_party/base16.json").read_text(encoding="utf-8"))
        listed = run("highlight", "--list-colorschemes")
        self.assertEqual(listed.returncode, 0, listed.stderr)
        self.assertEqual(listed.stdout.decode().splitlines(), [p["name"] for p in data["schemes"]])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "custom.yaml"
            for palette in data["schemes"]:
                path.write_text(palette["yaml"], encoding="utf-8")
                for mode in ("--html", "--ansi"):
                    named = run("highlight", mode, "--colorscheme", palette["name"], data=b"SELECT :id, 'x', 1 -- note")
                    from_file = run("highlight", mode, "--colorscheme", str(path), data=b"SELECT :id, 'x', 1 -- note")
                    self.assertEqual(named.returncode, 0, (palette["name"], named.stderr))
                    self.assertEqual(from_file.returncode, 0, (palette["name"], from_file.stderr))
                    self.assertEqual(named.stdout, from_file.stdout, palette["name"])
                    if mode == "--html":
                        self.assertIn(f"background-color:#{palette['colors'][0]}".encode(), named.stdout)
                        self.assertIn(f'color:#{palette["colors"][8]}">:id'.encode(), named.stdout)

    def test_legacy_custom_yaml_and_invalid_palettes(self):
        yaml = "scheme: Custom\nauthor: Me\n" + "".join(f"base{i:02X}: '{i + 1:06x}' # color\r\n" for i in range(16))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "legacy.yaml"
            path.write_text(yaml)
            result = run("highlight", "--colorscheme", str(path), data=b"SELECT :id")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(b"background-color:#000001", result.stdout)
            for invalid in (
                "",
                "base00: '123456'",
                yaml + "base00: 'ffffff'",
                yaml.replace("'000001'", "'red'"),
                yaml.replace("'000001'", "'123456;display:none'"),
                yaml + "base10: '123456'",
            ):
                path.write_text(invalid)
                result = run("highlight", "--colorscheme", str(path), data=b"SELECT 1")
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, b"")

    def test_mutually_exclusive_modes_and_bad_arguments(self):
        for args in [
            ("--html", "--ansi"),
            ("--ansi", "--html"),
            ("--light", "--dark"),
            ("--dark", "--light"),
            ("--colorscheme",),
            ("--colorscheme=",),
            ("--colorscheme", "not-a-scheme"),
            ("--color-variable",),
            ("--color-variable", "#12345"),
            ("--color-variable", "#1230000"),
            ("--color-variable", "red"),
            ("--color-variable", "#GG0000"),
            ("--color-unknown", "#123000"),
            ("--color-string", "#123456;display:none"),
            ("--dialect", "bad"),
            ("--embedded",),
        ]:
            with self.subTest(args=args):
                result = run("highlight", *args, data=b"SELECT 1")
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, b"")
        for command in ("format", "sanitize"):
            for flag in (
                "--ansi",
                "--html",
                "--light",
                "--dark",
                "--list-colorschemes",
                "--colorscheme=default-dark",
                "--color-string=123456",
            ):
                self.assertNotEqual(run(command, flag, data=b"SELECT 1").returncode, 0)

    def test_empty_whitespace_incomplete_sql_and_comments(self):
        for sql in (
            "",
            " ",
            "\t\r\n  ",
            "SELECT",
            "SELECT (",
            "SELECT )",
            "-- only a comment",
            "/* comment */",
            "select 'a''b'",
        ):
            self.html(sql)
            ansi = run("highlight", "--ansi", data=sql.encode())
            self.assertEqual(ansi.returncode, 0, ansi.stderr)
            self.assertEqual(SGR.sub(b"", ansi.stdout), sql.encode())
        for sql in (
            b"select 'unfinished",
            b"/* unfinished",
            b'SELECT "unfinished',
            b"SELECT $tag$unfinished",
            b"SELECT '\x1b[2J'",
            b"SELECT '\x07'",
            b"SELECT '\xc2\x9b'",
            b"SELECT \xff",
            b"a\0b",
        ):
            for mode in ("--html", "--ansi"):
                result = run("highlight", mode, data=sql)
                self.assertNotEqual(result.returncode, 0, sql)
                self.assertEqual(result.stdout, b"")

    def test_real_format_to_highlight_pipeline_and_checked_in_samples(self):
        directory = ROOT / "examples/highlight"
        sql = (directory / "query.sql").read_bytes()
        formatted = run("format", "--dialect=postgresql", data=sql)
        self.assertEqual(formatted.returncode, 0, formatted.stderr)
        self.assertIn(b"\n", formatted.stdout)
        for appearance in ("dark", "light"):
            for mode, extension in (("--html", "html"), ("--ansi", "ansi")):
                producer = subprocess.Popen(
                    [BINARY, "format", "--dialect=postgresql"],
                    stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )
                assert producer.stdin is not None and producer.stdout is not None
                producer.stdin.write(sql)
                producer.stdin.close()
                result = subprocess.run(
                    [BINARY, "highlight", "--dialect=postgresql", mode, f"--{appearance}"],
                    stdin=producer.stdout,
                    capture_output=True,
                    timeout=15,
                )
                producer.stdout.close()
                producer.wait(timeout=15)
                assert producer.stderr is not None
                producer_error = producer.stderr.read()
                producer.stderr.close()
                self.assertEqual(producer.returncode, 0, producer_error)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, (directory / f"{appearance}.{extension}").read_bytes())
                if extension == "html":
                    self.assertEqual(Fragment(result.stdout.decode()).source, formatted.stdout.decode())
                else:
                    self.assertEqual(SGR.sub(b"", result.stdout), formatted.stdout)

    def test_deterministic_random_lexical_roundtrips(self):
        randomizer = random.Random(4201)
        tokens = [
            "select",
            "users",
            ":name",
            "$1",
            "?",
            "1.23e-4",
            "0xff",
            "'héllo <&>'",
            '"quoted"',
            "TRUE",
            "NULL",
            "/* comment */",
            "+",
            "=",
            ";",
            "(",
            ")",
            "count",
            ",",
            "日本語",
        ]
        for i in range(100):
            sql = "".join(randomizer.choice(tokens) + randomizer.choice([" ", "\t", "\n", "\r\n"]) for _ in range(30))
            with self.subTest(i=i):
                self.html(sql)
                ansi = run("highlight", "--ansi", data=sql.encode())
                self.assertEqual(ansi.returncode, 0, ansi.stderr)
                self.assertEqual(SGR.sub(b"", ansi.stdout), sql.encode())


if __name__ == "__main__":
    unittest.main()
