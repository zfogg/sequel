"""Diagnostics, tolerant rendering and scripted multiline workbench sessions."""

import html
import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

BINARY = str(Path(sys.argv.pop(1)).resolve())


def run(*args, data=b""):
    return subprocess.run([BINARY, *args], input=data, capture_output=True, timeout=15)


def plain_html(value):
    return html.unescape(re.sub(r"<[^>]*>", "", value.decode()))


def plain_ansi(value):
    return re.sub(rb"\x1b\[[0-9;]*m", b"", value).decode()


class DiagnosticsTests(unittest.TestCase):
    def test_explain(self):
        result = run("explain", "--json", data="-- 🙂\r\nSELECT é, 'secret".encode())
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(result.stderr)
        d = json.loads(result.stdout)
        self.assertEqual((d["code"], d["line"], d["column"], d["offset"]), ("unterminated_string", 2, 11, 20))
        self.assertEqual((d["end_line"], d["end_column"]), (2, 18))
        self.assertTrue(d["incomplete"])
        self.assertFalse(d["statement_complete"])
        self.assertEqual(d["severity"], "warning")
        self.assertNotIn("secret", result.stdout.decode())
        for command in ("format", "highlight", "sanitize"):
            result = run(command, "SELECT 'unfinished")
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(result.stdout)
            self.assertIn(b"1:8:", result.stderr)
            self.assertIn(b"unterminated_string", result.stderr)
            self.assertIn(b"Hint:", result.stderr)
        result = run("explain", "SELECT (1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"unclosed_group", result.stdout)
        self.assertIn(b"Hint:", result.stdout)

    def test_valid_completion(self):
        for sql, complete in [
            ("", False),
            ("SELECT ';'", False),
            ("SELECT ';'; -- ;", True),
            ("SELECT (1); /* ; */", True),
            ("SELECT 1; SELECT 2", False),
        ]:
            result = run("explain", "--json", data=sql.encode())
            self.assertEqual(result.returncode, 0, result.stderr)
            d = json.loads(result.stdout)
            self.assertEqual(d["code"], "none")
            self.assertEqual(d["statement_complete"], complete)
            self.assertFalse(d["incomplete"])

    def test_errors(self):
        for data, code in [
            (b"SELECT \x00 x", "control_character"),
            (b"\xff", "invalid_utf8"),
            (b"\x1b[2J", "control_character"),
            (b"x " * 65537 + b"\x00", "control_character"),
            (b"(" * 65, "nesting_limit"),
            (b"/*" * 65, "nesting_limit"),
        ]:
            result = run("explain", "--json", data=data)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(json.loads(result.stdout)["code"], code)
            self.assertFalse(result.stderr)
            if data == b"(" * 65:
                continue  # Highlighting does not validate delimiter groups.
            result = run("highlight", "--tolerant", data=data)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(result.stdout)
            self.assertIn(code.encode(), result.stderr)
        result = run("explain", "--json", "--dialect", "clickhouse", "INSERT INTO t FORMAT CSV\nx,y")
        self.assertEqual(json.loads(result.stdout)["code"], "unsupported_payload")

    def test_paths_and_in_place_errors(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = [Path(directory) / name for name in ("first.sql", "second.sql")]
            paths[0].write_text("select 1;", encoding="utf-8")
            paths[1].write_text("select 'unfinished", encoding="utf-8")
            result = run("explain", "--json", *(str(path) for path in paths))
            diagnostics = [json.loads(line) for line in result.stdout.splitlines()]
            self.assertEqual([d["code"] for d in diagnostics], ["none", "unterminated_string"])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(str(paths[1]).encode(), result.stderr)
            result = run("format", "--edit-file", str(paths[1]))
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(paths[1].read_text(), "select 'unfinished")
            result = run("highlight", "--tolerant", "--edit-file", str(paths[1]))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(plain_html(paths[1].read_bytes()), "select 'unfinished")

    def test_tolerant_pipeline(self):
        for sql, dialect, token in [
            ("SELECT '<script>&", "generic", "string"),
            ("/* outer /* inner */", "postgresql", "comment"),
            ('SELECT "unfinished', "postgresql", "identifier"),
            ("SELECT {id:UInt64", "clickhouse", "variable"),
        ]:
            for mode in ("--html", "--ansi"):
                result = run("highlight", "--tolerant", mode, "--dialect", dialect, data=sql.encode())
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertFalse(result.stderr)
                self.assertEqual(plain_html(result.stdout) if mode == "--html" else plain_ansi(result.stdout), sql)
                if mode == "--html":
                    self.assertIn(f"sequel-{token}".encode(), result.stdout)
        formatted = run("format", data=b"select a,b from t where id=1;")
        self.assertEqual(formatted.returncode, 0, formatted.stderr)
        rendered = run("highlight", data=formatted.stdout)
        self.assertEqual(rendered.returncode, 0, rendered.stderr)
        self.assertEqual(plain_html(rendered.stdout), formatted.stdout.decode())

    def test_workbench_multiline_and_recovery(self):
        session = b"SELECT 'hello\n:explain\nworld';\n:explain\n:show\n:format\nSELECT 2;\n:explain\n:clear\n:explain\n:q\nSELECT 999;\n"
        result = run("interactive", "--json", data=session)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"\x1b[", result.stdout)
        self.assertNotIn(b"sql> ", result.stderr)
        self.assertNotIn(b"...> ", result.stderr)
        self.assertNotIn(b"999", result.stdout)
        warnings = [json.loads(line) for line in result.stderr.splitlines()]
        self.assertEqual([d["code"] for d in warnings], ["unterminated_string"])
        lines = result.stdout.splitlines()
        diagnostics = [json.loads(line) for line in lines if line.startswith(b'{"code"')]
        self.assertEqual([d["code"] for d in diagnostics], ["unterminated_string", "none", "none", "none"])
        self.assertEqual([d["statement_complete"] for d in diagnostics], [False, True, True, False])
        plain = plain_ansi(result.stdout)
        self.assertIn("SELECT 'hello\nworld';", plain)
        self.assertEqual(plain.count("world';"), 3)  # completion, :show and :format

    def test_workbench_comments_commands_and_html(self):
        session = b"/* first\n:show\n:format\n:clear\nselect a,b from t;\n:format\n:show\n:help\n:quit\n"
        result = run("highlight", "--interactive", "--html", "--colorscheme", "solarized-dark", "--light", data=session)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn(b"\x1b", result.stdout)
        self.assertIn(b"sequel-comment", result.stdout)
        self.assertIn(b"background-color:#fdf6e3", result.stdout)
        self.assertIn(b"unterminated_comment", result.stderr)
        self.assertIn(b":clear", result.stderr)
        self.assertIn("select a, b\nfrom t;", plain_html(result.stdout))

    def test_workbench_completion_and_eof(self):
        for dialect, start, end in [
            ("postgresql", "select $tag$hello;", "$tag$;"),
            ("sqlite", "select $name(suffix", ");"),
            ("clickhouse", "select {id:", "UInt64};"),
        ]:
            session = f"{start}\n{end}\n:explain\n".encode()
            result = run("interactive", "--json", "--dialect", dialect, data=session)
            self.assertEqual(result.returncode, 0, result.stderr)
            d = json.loads(result.stdout.splitlines()[-1])
            self.assertEqual(d["code"], "none")
            self.assertTrue(d["statement_complete"])
        result = run("interactive", data=b"select 1\n:id\n:explain")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("select 1\n:id\n", plain_ansi(result.stdout))
        self.assertIn(b"Statement complete: no", result.stdout)
        result = run("interactive", data=b":clear\r\nselect 1;\r\n:explain\r\n:quit\r\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"Statement complete: yes", result.stdout)
        self.assertEqual(run("interactive").returncode, 0)

    def test_workbench_limits_and_hard_error_recovery(self):
        for data, code in [
            (b"x" * (1024 * 1024 + 1) + b"\0", "control_character"),
            (b"select \x00 x", "control_character"),
            (b" " * (512 * 1024) + b"\n" + b" " * (512 * 1024) + b"\0", "control_character"),
        ]:
            result = run("interactive", "--json", data=data)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(json.loads(result.stderr)["code"], code)
            self.assertNotIn(b"\x00", result.stdout)
        result = run("interactive", "--json", data=b"select '\nabc\x00")
        diagnostics = [json.loads(line) for line in result.stderr.splitlines()]
        self.assertEqual(
            (diagnostics[-1]["code"], diagnostics[-1]["line"], diagnostics[-1]["column"]), ("control_character", 2, 4)
        )
        result = run("interactive", "--json", data=b"select '\x1b[2J'\n:clear\nselect 2;\n:quit\n")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(json.loads(result.stderr)["code"], "control_character")
        self.assertNotIn(b"\x1b[2J", result.stdout)
        self.assertIn("select 2;", plain_ansi(result.stdout))

    def test_flag_conflicts(self):
        for args in [
            ("explain", "--edit-file"),
            ("explain", "--embedded"),
            ("format", "--tolerant"),
            ("format", "--interactive"),
            ("highlight", "--json"),
            ("interactive", "select 1"),
            ("interactive", "--edit-file"),
            ("interactive", "--embedded"),
            ("interactive", "--html", "--ansi"),
            ("interactive", "--list-colorschemes"),
        ]:
            result = run(*args)
            self.assertNotEqual(result.returncode, 0, args)
            self.assertFalse(result.stdout, args)
            self.assertTrue(result.stderr, args)


if __name__ == "__main__":
    unittest.main()
