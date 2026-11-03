"""All-issues diagnostics through the existing native executable and workbench."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

BINARY = str(Path(sys.argv.pop(1)).resolve())


def run(*args, data=b""):
    return subprocess.run([BINARY, *args], input=data, capture_output=True, timeout=30)


class AllDiagnosticsTests(unittest.TestCase):
    def envelope(self, result, length, status):
        self.assertFalse(result.stderr, result.stderr)
        payload = json.loads(result.stdout)
        self.assertEqual(set(payload), {"schema_version", "original_length", "status", "diagnostics"})
        self.assertEqual(payload["schema_version"], 1)
        self.assertEqual(payload["original_length"], length)
        self.assertEqual(payload["status"], status)
        return payload

    def test_existing_default_schema_and_diagnose_alias(self):
        data = b"SELECT ); SELECT ];"
        old = run("explain", "--json", data=data)
        alias = run("diagnose", "--json", data=data)
        self.assertEqual(old.returncode, alias.returncode)
        self.assertEqual(json.loads(old.stdout), json.loads(alias.stdout))
        diagnostic = json.loads(old.stdout)
        self.assertEqual(diagnostic["code"], "unmatched_close")
        self.assertNotIn("schema_version", diagnostic)
        self.assertNotIn("diagnostics", diagnostic)

    def test_safe_boundaries_and_utf8_original_locations(self):
        data = "-- 🙂\r\nSELECT ); SELECT (1; SELECT ]; SELECT 'sensitive; SELECT );".encode()
        result = run("diagnose", "--all", "--json", "--dialect", "postgresql", data=data)
        self.assertNotEqual(result.returncode, 0)
        payload = self.envelope(result, len(data), "malformed")
        diagnostics = payload["diagnostics"]
        self.assertEqual(
            [d["code"] for d in diagnostics],
            ["unmatched_close", "unclosed_group", "unmatched_close", "unterminated_string"],
        )
        self.assertEqual((diagnostics[0]["line"], diagnostics[0]["column"]), (2, 8))
        self.assertEqual(diagnostics[0]["offset"], data.index(b")"))
        self.assertEqual(diagnostics[-1]["end_offset"], len(data))
        self.assertTrue(diagnostics[-1]["incomplete"])
        for diagnostic in diagnostics:
            self.assertLessEqual(diagnostic["offset"], diagnostic["end_offset"])
            self.assertLessEqual(diagnostic["end_offset"], len(data))
            self.assertTrue(diagnostic["message"])
            self.assertTrue(diagnostic["explanation"])
            self.assertTrue(diagnostic["hint"])
        self.assertNotIn(b"sensitive", result.stdout)
        self.assertNotIn(b"SELECT", result.stdout)

    def test_quoted_and_comment_semicolons_do_not_recover(self):
        cases = [
            (b"SELECT 'sensitive; SELECT );", ["unterminated_string"]),
            (b"SELECT 1 /* sensitive; SELECT );", ["unterminated_comment"]),
            (b"SELECT 'sensitive; SELECT )'; SELECT ];", ["unmatched_close"]),
            (b"SELECT 1 /* sensitive; SELECT ) */; SELECT ];", ["unmatched_close"]),
            (b"SELECT $tag$sensitive; SELECT );", ["unterminated_string"]),
        ]
        for data, codes in cases:
            with self.subTest(data=data):
                result = run("explain", "--all", "--json", "--dialect", "postgresql", data=data)
                self.assertNotEqual(result.returncode, 0)
                payload = self.envelope(result, len(data), "malformed")
                self.assertEqual([d["code"] for d in payload["diagnostics"]], codes)
                self.assertNotIn(b"sensitive", result.stdout)
                self.assertNotIn(b"SELECT", result.stdout)

    def test_empty_and_valid_input_have_empty_diagnostic_arrays(self):
        for data in (b"", b"-- sensitive", b"SELECT 'sensitive;'; /* ; */", "SELECT '🙂';".encode()):
            with self.subTest(data=data):
                result = run("diagnose", "--json", "--all", data=data)
                self.assertEqual(result.returncode, 0)
                payload = self.envelope(result, len(data), "ok")
                self.assertEqual(payload["diagnostics"], [])
                self.assertNotIn(b"sensitive", result.stdout)
                self.assertNotIn(b"text", result.stdout)

    def test_controls_invalid_utf8_and_nonterminated_input_length(self):
        for data, code, offset in [
            (b"SELECT \0sensitive", "control_character", 7),
            (b"SELECT \xff sensitive", "invalid_utf8", 7),
            (b"SELECT \x1b sensitive", "control_character", 7),
        ]:
            with self.subTest(code=code):
                result = run("diagnose", "--all", "--json", data=data)
                self.assertNotEqual(result.returncode, 0)
                payload = self.envelope(result, len(data), "malformed")
                self.assertEqual(len(payload["diagnostics"]), 1)
                self.assertEqual(payload["diagnostics"][0]["code"], code)
                self.assertEqual(payload["diagnostics"][0]["offset"], offset)
                self.assertNotIn(b"sensitive", result.stdout)
        data = b"-- " + b"s" * 65537 + b"\r\nSELECT ); SELECT ];"
        result = run("diagnose", "--all", "--json", data=data)
        payload = self.envelope(result, len(data), "malformed")
        self.assertEqual(len(payload["diagnostics"]), 2)
        self.assertEqual(payload["diagnostics"][0]["line"], 2)

    def test_text_prints_each_issue_without_sql(self):
        result = run("diagnose", "--all", data=b"SELECT 'sensitive'; SELECT ); SELECT ];")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(result.stderr)
        self.assertEqual(result.stdout.count(b"[unmatched_close]"), 2)
        self.assertEqual(result.stdout.count(b"Hint:"), 2)
        self.assertNotIn(b"sensitive", result.stdout)
        self.assertNotIn(b"SELECT", result.stdout)
        result = run("diagnose", "--all", "SELECT 1;")
        self.assertEqual(result.returncode, 0)
        self.assertIn(b"no lexical or delimiter issues", result.stdout)

    def test_paths_produce_one_envelope_per_input_and_never_edit(self):
        with tempfile.TemporaryDirectory() as directory:
            first, second = (Path(directory) / name for name in ("first.sql", "second.sql"))
            first.write_bytes(b"SELECT 'sensitive';")
            second.write_bytes(b"SELECT ); SELECT ];")
            result = run("diagnose", "--all", "--json", str(first), str(second))
            self.assertNotEqual(result.returncode, 0)
            payloads = [json.loads(line) for line in result.stdout.splitlines()]
            self.assertEqual([p["status"] for p in payloads], ["ok", "malformed"])
            self.assertEqual([p["original_length"] for p in payloads], [first.stat().st_size, second.stat().st_size])
            self.assertEqual(len(payloads[1]["diagnostics"]), 2)
            self.assertNotIn(b"sensitive", result.stdout)
            result = run("diagnose", "--all", "--edit-file", str(first))
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(result.stdout)
            self.assertEqual(first.read_bytes(), b"SELECT 'sensitive';")

    def test_interactive_explain_all_preserves_default_command(self):
        data = b"SELECT ); SELECT ];\n:explain-all\n:explain\n:q\n"
        result = run("interactive", "--json", data=data)
        self.assertEqual(result.returncode, 0, result.stderr)
        envelopes = [json.loads(line) for line in result.stdout.splitlines() if line.startswith(b'{"schema_version"')]
        legacy = [json.loads(line) for line in result.stdout.splitlines() if line.startswith(b'{"code"')]
        self.assertEqual(len(envelopes), 1)
        self.assertEqual(envelopes[0]["status"], "malformed")
        self.assertEqual([d["code"] for d in envelopes[0]["diagnostics"]], ["unmatched_close", "unmatched_close"])
        self.assertEqual(envelopes[0]["original_length"], len(b"SELECT ); SELECT ];\n"))
        self.assertEqual(len(legacy), 1)
        self.assertEqual(legacy[0]["code"], "unmatched_close")

    def test_invalid_path_arguments_do_not_echo_possible_sql(self):
        result = run("diagnose", "--all", "--json", "SELECT 'sensitive'", "SELECT 'other_sensitive'")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn(b"sensitive", result.stdout + result.stderr)
        self.assertNotIn(b"SELECT", result.stdout + result.stderr)

    def test_all_is_exclusive_to_diagnostic_subcommands(self):
        for command in ("format", "sanitize", "highlight", "interactive"):
            result = run(command, "--all")
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(result.stdout)
            self.assertIn(b"--all requires", result.stderr)
        result = run("--help")
        self.assertIn(b"--all", result.stdout)
        self.assertIn(b"diagnose", result.stdout)


if __name__ == "__main__":
    unittest.main()
