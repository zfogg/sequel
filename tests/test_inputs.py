"""Both commands share stdin, SQL strings, multiple paths and editing modes."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

BINARY = str(Path(sys.argv.pop(1)).resolve())


def run(*args, data=b""):
    return subprocess.run([BINARY, *args], input=data, capture_output=True, timeout=10)


class InputTests(unittest.TestCase):
    def test_multiple_sql_paths_and_editing(self):
        original = b"/* private */ select   a,b from t where id=42 and token='secret';"
        with tempfile.TemporaryDirectory() as directory:
            paths = [Path(directory) / f"query {i}.sql" for i in range(2)]
            for command in ("format", "sanitize"):
                for path in paths:
                    path.write_bytes(original)
                expected = run(command, data=original)
                self.assertEqual(expected.returncode, 0, expected.stderr)
                from_string = run(command, original.decode())
                self.assertEqual(from_string.stdout, expected.stdout)
                with paths[0].open("rb") as redirected:
                    result = subprocess.run([BINARY, command], stdin=redirected, capture_output=True, timeout=10)
                self.assertEqual(result.stdout, expected.stdout)
                result = run(command, *map(str, paths))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, expected.stdout * 2)
                self.assertTrue(all(p.read_bytes() == original for p in paths))
                result = run(command, "--edit-file", *map(str, paths))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, b"")
                for path in paths:
                    self.assertNotEqual(path.read_bytes(), original)
                    if command == "sanitize":
                        self.assertEqual(path.read_bytes(), expected.stdout)
                self.assertEqual(list(Path(directory).glob("*.tmp")), [])

    def test_embedded_sanitize_strings_stdin_paths_and_editing(self):
        sql = "SELECT id,name FROM users WHERE token='secret' AND id=$1 /* private */"
        source = "const query = " + json.dumps(sql) + ';\nconst untouched = "hello";\n'
        with tempfile.TemporaryDirectory() as directory:
            paths = [Path(directory) / f"source {i}.go" for i in range(2)]
            for path in paths:
                path.write_bytes(source.encode())
            for flags, keep_secret, keep_private in [
                ([], False, False),
                (["--remove-comments"], True, False),
                (["--replace-literals"], False, True),
                (["--remove-comments", "--replace-literals"], False, False),
            ]:
                args = ["sanitize", "--embedded", "--dialect", "postgresql", *flags]
                result = run(*args, "--language", "go", data=source.encode())
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(b"secret" in result.stdout, keep_secret)
                self.assertEqual(b"private" in result.stdout, keep_private)
                self.assertIn(b'const untouched = "hello";', result.stdout)
                self.assertEqual(run(*args, "--language", "go", source).stdout, result.stdout)
                files = run(*args, *map(str, paths))
                self.assertEqual(files.returncode, 0, files.stderr)
                self.assertEqual(files.stdout, result.stdout * 2)
                self.assertTrue(all(p.read_text(encoding="utf-8") == source for p in paths))
            edited = run("sanitize", "--embedded", "--edit-file", *map(str, paths))
            self.assertEqual(edited.returncode, 0, edited.stderr)
            self.assertEqual(edited.stdout, b"")
            self.assertTrue(all("secret" not in p.read_text(encoding="utf-8") for p in paths))

    def test_embedded_sanitize_host_languages_and_failure(self):
        source = json.dumps("SELECT id FROM users WHERE token='secret' /* private */")
        for language in ("go", "c", "python", "javascript", "typescript", "rust", "haskell", "clojure"):
            result = run("sanitize", "--embedded", "--language", language, source)
            self.assertEqual(result.returncode, 0, (language, result.stderr))
            self.assertNotIn(b"secret", result.stdout)
            self.assertNotIn(b"private", result.stdout)
        malformed = json.dumps("SELECT id FROM users WHERE token='secret")
        result = run("sanitize", "--embedded", "--language", "go", malformed)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"")


if __name__ == "__main__":
    unittest.main()
