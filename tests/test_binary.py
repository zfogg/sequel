"""Black-box tests of the C executable; no Go toolchain required."""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

BINARY = str(Path(sys.argv.pop(1)).resolve())


def run(*args, sql=b""):
    return subprocess.run([BINARY, *args], input=sql, capture_output=True, timeout=10)


class BinaryTests(unittest.TestCase):
    def test_stdout_dialects(self):
        for dialect in ("generic", "postgresql", "mysql", "sqlite", "duckdb", "clickhouse"):
            with self.subTest(dialect=dialect):
                result = run("format", "--dialect", dialect, sql=b"select a,b from t where id=42;")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(b"a,", result.stdout)
                self.assertIn(b"id = 42", result.stdout)

    def test_nested_format_expansion(self):
        sql = b"select (" * 40 + b"select 1" + b")" * 40
        result = run("format", sql=sql)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count(b"select"), 41)
        self.assertEqual(result.stdout.count(b"("), 40)
        self.assertEqual(result.stdout.count(b")"), 40)

    def test_selective_sanitize(self):
        sql = "/* private */ SELECT 'secret', 42, $1 -- private\nFROM t"
        for flags, want in [
            ([], b"SELECT ?, ?, ? FROM t"),
            (["--remove-comments"], b"SELECT 'secret', 42, $1 FROM t"),
            (["--replace-literals"], b"/* private */ SELECT ?, ?, ? -- private\nFROM t"),
            (["--remove-comments", "--replace-literals"], b"SELECT ?, ?, ? FROM t"),
        ]:
            result = run("sanitize", "--dialect", "postgresql", *flags, sql)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, want)
        self.assertNotEqual(run("format", "--remove-comments", sql).returncode, 0)

    def test_sanitize(self):
        result = run("sanitize", "select * from t where name='secret' and id=42")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn(b"secret", result.stdout)
        self.assertNotIn(b"42", result.stdout)
        self.assertIn(b"?", result.stdout)

    def test_argument_and_pipe_agree(self):
        sql = "select a,b from t where x=1; -- tail\nselect 'keep  spaces';"
        for command in ("format", "sanitize"):
            a = run(command, sql)
            b = run(command, sql=sql.encode())
            self.assertEqual(a.returncode, 0, a.stderr)
            self.assertEqual(b.returncode, 0, b.stderr)
            self.assertEqual(a.stdout, b.stdout)

    def test_files(self):
        cases = [
            (
                "postgresql",
                b"-- name: First :one\nselect   a, b  from   t; -- trailing\n\n-- Second\nselect   'x;  y', $$a;\n b$$;\n",
                b"-- name: First :one\nselect a, b from t; -- trailing\n\n-- Second\nselect 'x;  y', $$a;\n b$$;\n",
            ),
            (
                "sqlite",
                b"\r\n  select   [a]  from t;\r\n\r\n  select   'b';\r\n",
                b"\r\n  select [a] from t;\r\n\r\n  select 'b';\r\n",
            ),
            (
                "postgresql",
                b"/* outer\n /* inner */  end */\nselect   1;",
                b"/* outer\n /* inner */  end */\nselect 1;",
            ),
            (
                "mysql",
                b"# comment\nselect   `a`, 'a  b'; # tail\nselect   2;",
                b"# comment\nselect `a`, 'a  b'; # tail\nselect 2;",
            ),
            ("postgresql", b"select   'a'\n  'b';\n", b"select 'a'\n  'b';\n"),
            ("generic", b"", b""),
            ("generic", b" \t\n\n", b" \t\n\n"),
            ("generic", b"select   1;", b"select 1;"),
            ("postgresql", b"select  a::text, b->>'x', - -1, x != 2;", b"select a::text, b->>'x', - -1, x != 2;"),
            ("postgresql", "select   'æ—¥æœ¬èªž  cafÃ©';\r".encode(), "select 'æ—¥æœ¬èªž  cafÃ©';\r".encode()),
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "queries with spaces.sql"
            for dialect, sql, expected in cases:
                with self.subTest(sql=sql):
                    path.write_bytes(sql)
                    path.chmod(0o640)
                    mode = path.stat().st_mode
                    for _ in range(2):
                        result = run("format", "--dialect", dialect, "--edit-file", str(path))
                        self.assertEqual(result.returncode, 0, result.stderr)
                        self.assertEqual(result.stdout, b"")
                        self.assertEqual(path.read_bytes(), expected)
                        self.assertEqual(path.stat().st_mode, mode)
                    self.assertEqual(list(Path(directory).glob("*.tmp")), [])

    def test_errors_preserve_original(self):
        bad = [
            b"select 'unterminated",
            b"/* unfinished",
            b"select $$unfinished",
            b"select \0",
            b"select \xff",
            b"x" * (1024 * 1024 + 1),
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.sql"
            for sql in bad:
                path.write_bytes(sql)
                result = run("format", "--dialect", "postgresql", "--edit-file", str(path))
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, b"")
                self.assertEqual(path.read_bytes(), sql)
                self.assertEqual(list(Path(directory).glob("*.tmp")), [])

    def test_unicode_arguments_and_paths(self):
        sql = "select   '日本語 café 😀';"
        for command in ("format", "sanitize"):
            argument = run(command, sql)
            piped = run(command, sql=sql.encode())
            self.assertEqual(argument.returncode, 0, argument.stderr)
            self.assertEqual(piped.returncode, 0, piped.stderr)
            self.assertEqual(argument.stdout, piped.stdout)
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory) / "日本語 café 😀"
            folder.mkdir()
            path = folder / "日本.sql"
            path.write_bytes(sql.encode())
            result = run("format", "--edit-file", str(path))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(path.read_bytes(), "select '日本語 café 😀';".encode())
            self.assertEqual(list(folder.glob("*.tmp")), [])

    @unittest.skipUnless(os.name == "nt", "Windows read-only replacement behavior")
    def test_readonly_failure_removes_temp(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "readonly.sql"
            path.write_bytes(b"select   1;")
            path.chmod(0o444)
            try:
                result = run("format", "--edit-file", str(path))
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(path.read_bytes(), b"select   1;")
                self.assertEqual(list(Path(directory).glob("*.tmp")), [])
            finally:
                path.chmod(0o666)

    def test_bad_arguments(self):
        for args in [
            [],
            ["unknown"],
            ["format", "--dialect", "unknown"],
            ["sanitize", "--edit-file", "x"],
            ["format", "--edit-file", "missing.sql"],
            ["format", "a", "b"],
            ["format", "--edit-file", "x", "select 1"],
            ["format", "--edit-file", ""],
        ]:
            with self.subTest(args=args):
                self.assertNotEqual(run(*args).returncode, 0)

    def test_pipe_limit_and_nul(self):
        for sql in [b"x" * (1024 * 1024 + 1), b"select \0 1"]:
            self.assertNotEqual(run("format", sql=sql).returncode, 0)

    def test_help(self):
        for args in [("--help",), ("format", "--help")]:
            result = run(*args)
            self.assertEqual(result.returncode, 0)
            self.assertIn(b"Usage:", result.stdout)

    @unittest.skipIf(os.name == "nt", "POSIX pseudo-terminal required")
    def test_interactive_stdin_does_not_wait(self):
        import pty

        master, slave = pty.openpty()
        try:
            for command in ("format", "sanitize"):
                result = subprocess.run([BINARY, command], stdin=slave, capture_output=True, timeout=3)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b"Usage:", result.stderr)
            result = subprocess.run([BINARY, "format", "select 1"], stdin=slave, capture_output=True, timeout=3)
            self.assertEqual(result.returncode, 0)
        finally:
            os.close(master)
            os.close(slave)

    @unittest.skipIf(os.name == "nt", "POSIX symlink permissions required")
    def test_symlinks_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            original = Path(directory) / "original.sql"
            original.write_bytes(b"select   1;")
            link = Path(directory) / "link.sql"
            link.symlink_to(original)
            self.assertNotEqual(run("format", "--edit-file", str(link)).returncode, 0)
            self.assertEqual(original.read_bytes(), b"select   1;")


if __name__ == "__main__":
    unittest.main()
