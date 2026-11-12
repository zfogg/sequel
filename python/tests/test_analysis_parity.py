"""Unified Analyze must preserve legacy Inspect metadata on checked safe SQL.

These tests reuse existing recorded and synthetic corpora rather than inventing
new summary expectations. Explicit fixture flags exclude known unsafe/invalid
input; strict malformed-analysis behavior is covered by test_analysis.py. New
bind metadata, fingerprints, source maps, and sanitized formatting are additional
outputs and do not justify changing text/summary/operation/statement_count.
"""

import json
import re
from dataclasses import dataclass
from pathlib import Path

import pytest
from sequel import Dialect, analyze, diagnose, metadata

inspect = metadata

ROOT = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class ParityCase:
    label: str
    sql: str
    dialect: Dialect


def read_json(path):
    return json.loads((ROOT / path).read_text(encoding="utf-8"))


def corpus_cases():
    cases = []
    for index, row in enumerate(read_json("tests/sql_reference.json")):
        if row["safe"]:
            cases.append(ParityCase(f"reference/{index}/d{row['dialect']}", row["query"], Dialect(row["dialect"])))
    for row in read_json("tests/sql_clickhouse.json"):
        if row["safe"]:
            cases.append(ParityCase(f"clickhouse/{row['name']}", row["query"], Dialect.CLICKHOUSE))
    for row in read_json("testdata/sql_format.json")["cases"]:
        if row["valid"]:
            cases.append(ParityCase(f"format/{row['name']}", row["sql"], Dialect[row["dialect"].upper()]))
    for index, row in enumerate(read_json("tests/stonkpile_pi5.json")):
        cases.append(ParityCase(f"recorded/{index}", row["query"], Dialect.POSTGRESQL))
    for path in sorted((ROOT / "tests/stonkpile").glob("*.sql")):
        sql = path.read_text(encoding="utf-8")
        matches = list(re.finditer(r"(?m)^-- name: ([A-Za-z0-9_]+) :[^\r\n]+", sql))
        for index, match in enumerate(matches):
            end = matches[index + 1].start() if index + 1 < len(matches) else len(sql)
            cases.append(ParityCase(f"sqlc/{path.name}/{match[1]}", sql[match.start() : end], Dialect.POSTGRESQL))
        cases.append(ParityCase(f"sqlc-whole-file/{path.name}", sql, Dialect.POSTGRESQL))
    return cases


CORPUS = corpus_cases()
# Legacy Inspect does not perform delimiter validation. These exact existing
# reference rows are intentionally rejected by Analyze; no broad corpus skips.
STRICT_LABELS = {
    f"reference/{index}/d{dialect}"
    for index, dialect in [
        (115, 0),
        (116, 1),
        (117, 2),
        (118, 3),
        (119, 4),
        (135, 0),
        (136, 1),
        (510, 0),
        (511, 1),
        (515, 0),
        (516, 1),
    ]
}
# Exact legacy-accepted forms whose dialect token boundaries are ambiguous, or
# whose PostgreSQL numbered-question marker spelling is unsupported. They must
# fail closed instead of exporting a misleading dialect fingerprint.
POLICY_REJECTIONS = {
    "reference/181/d1": "ambiguous_literal",
    "reference/184/d4": "ambiguous_literal",
    "reference/196/d1": "ambiguous_literal",
    "reference/199/d4": "ambiguous_literal",
    "reference/215/d0": "ambiguous_literal",
    "reference/216/d1": "ambiguous_literal",
    "reference/217/d2": "ambiguous_literal",
    "reference/218/d3": "ambiguous_literal",
    "reference/301/d1": "ambiguous_literal",
    "reference/304/d4": "ambiguous_literal",
    "reference/192/d2": "invalid_parameter",
}
STRICT_CASES = [case for case in CORPUS if case.label in STRICT_LABELS]
PARITY_CASES = [case for case in CORPUS if case.label not in STRICT_LABELS]


def assert_parity(sql, dialect, **options):
    legacy = inspect(sql, dialect=dialect, **options)
    result = analyze(sql, dialect=dialect, omit_source_maps=True, omit_bind_descriptors=True, **options)
    assert result.status == "ok", [(d.code, d.offset, d.end_offset) for d in result.diagnostics]
    assert result.text == legacy.text
    assert result.summary == legacy.summary
    assert result.operation == legacy.operation
    assert result.statement_count == legacy.statement_count
    assert result.truncated == legacy.truncated
    assert result.original_length == len(sql.encode("utf-8"))
    return result


@pytest.mark.parametrize("case", PARITY_CASES, ids=lambda case: case.label)
def test_analysis_matches_inspect_corpus(case):
    if case.label in POLICY_REJECTIONS:
        assert inspect(case.sql, dialect=case.dialect).text
        result = analyze(case.sql, dialect=case.dialect)
        assert result.status == "malformed"
        assert result.diagnostics[0].code == POLICY_REJECTIONS[case.label]
        assert not result.text and result.summary == "SQL" and not result.fingerprint
        assert not result.binds and not result.original_to_sanitized.segments
        return
    if case.label == "reference/416/d1":
        # SQLite treats $1::text as one named Tcl-style marker, not a numbered
        # marker followed by a PostgreSQL cast. Inspect's prefix split was wrong.
        legacy = inspect(case.sql, dialect=case.dialect)
        result = analyze(case.sql, dialect=case.dialect)
        assert result.status == "ok"
        assert result.text == legacy.text.replace("?::text", "?")
        assert (result.summary, result.operation, result.statement_count, result.truncated) == (
            legacy.summary,
            legacy.operation,
            legacy.statement_count,
            legacy.truncated,
        )
        assert [(b.style, b.slot) for b in result.binds] == [("named", 1), ("named", 2)]
        assert [case.sql.encode()[b.offset : b.end_offset].decode() for b in result.binds] == ["$1::text", "$2::text"]
        return
    if case.label == "reference/194/d4":
        # MySQL user variables are identifiers, not supplied bind parameters.
        # Legacy Inspect conservatively replaced @other; Analyze preserves it.
        legacy = inspect(case.sql, dialect=case.dialect)
        result = analyze(case.sql, dialect=case.dialect)
        assert result.status == "ok"
        assert result.text == "SELECT ?, ?, @other, ?, ?, ?"
        assert (result.summary, result.operation, result.statement_count, result.truncated) == (
            legacy.summary,
            legacy.operation,
            legacy.statement_count,
            legacy.truncated,
        )
        assert len(result.binds) == 5
        return
    assert_parity(case.sql, case.dialect)


@pytest.mark.parametrize("case", STRICT_CASES, ids=lambda case: case.label)
def test_analysis_intentionally_adds_strict_delimiter_validation(case):
    legacy = inspect(case.sql, dialect=case.dialect)
    assert legacy.text
    normal = diagnose(case.sql, dialect=case.dialect)
    assert normal.code in {"unclosed_group", "unmatched_close"}
    result = analyze(case.sql, dialect=case.dialect)
    assert result.status == "malformed"
    assert result.diagnostics[0].code == normal.code
    assert not result.text and not result.fingerprint and not result.binds
    assert not result.original_to_sanitized.segments


@pytest.mark.parametrize(
    "options",
    [
        {},
        {"omit_query_text": True},
        {"max_query_text_bytes": 12},
        {"keep_in_lists": True},
        {"omit_query_text": True, "max_query_text_bytes": 12},
    ],
)
def test_join_metadata_preserves_legacy_gap_and_numeric_bounds(options):
    sql = "SELECT * FROM accounts a JOIN orders o ON o.account_id=a.id AND o.token=$1 AND o.cost>123 LIMIT $2"
    expected = "SELECT … FROM accounts AS a JOIN orders AS o ON o.account_id=a.id AND o.token=? AND o.cost>? LIMIT ?"
    result = assert_parity(sql, Dialect.POSTGRESQL, **options)
    assert result.summary == expected
    assert "123" not in result.summary


@pytest.mark.parametrize("dialect", list(Dialect))
@pytest.mark.parametrize(
    "sql",
    [
        "SELECT 777 FROM a WHERE token='private' LIMIT 20 OFFSET 5",
        "SELECT * FROM (SELECT * FROM a WHERE secret='private' LIMIT 1) b JOIN c ON b.id=c.id LIMIT 99",
        "SELECT * FROM a WHERE id IN (1,2,3) LIMIT 10",
        "SELECT * FROM a LIMIT 5, 10",
        "SELECT 1; SELECT * FROM a LIMIT 2;",
        ";;; SELECT 1; ; SELECT 2; ;",
    ],
)
def test_summary_row_bounds_and_multistatement_parity(dialect, sql):
    for options in ({}, {"omit_query_text": True}, {"max_query_text_bytes": 12}, {"keep_in_lists": True}):
        assert_parity(sql, dialect, **options)


@pytest.mark.parametrize(
    "dialect,sql",
    [
        (Dialect.MYSQL, "SELECT $$secret$$"),
        (Dialect.SQLITE, "SELECT $$secret$$"),
        (Dialect.MYSQL, "SELECT $private$top secret$private$"),
        (Dialect.SQLITE, "SELECT $private$top secret$private$"),
        (Dialect.GENERIC, "SELECT 'a\\'secret\\'b'"),
        (Dialect.SQLITE, "SELECT 'a\\'secret\\'b'"),
        (Dialect.POSTGRESQL, "SELECT 'a\\'secret\\'b'"),
        (Dialect.DUCKDB, "SELECT 'a\\'secret\\'b'"),
    ],
)
def test_ambiguous_literal_boundaries_never_leak_or_claim_a_dialect_fingerprint(dialect, sql):
    result = analyze(sql, dialect=dialect)
    assert "secret" not in result.text + result.summary + result.formatted
    assert not result.fingerprint
    assert result.status == "malformed"
    assert result.diagnostics[0].code == "ambiguous_literal"
    for diagnostic in result.diagnostics:
        assert "secret" not in diagnostic.message + diagnostic.explanation + diagnostic.hint
    if result.status != "ok":
        assert not result.text and not result.formatted and not result.binds
        assert not result.original_to_sanitized.segments


@pytest.mark.parametrize("dialect", list(Dialect))
def test_join_predicate_fraction_values_are_redacted_in_summary(dialect):
    sql = "SELECT * FROM a x LEFT JOIN b y ON x.id=y.a_id AND y.f < .8765 LIMIT 5"
    result = assert_parity(sql, dialect)
    assert ".8765" not in result.summary
    assert "y.f < ?" in result.summary
    assert result.summary.endswith("LIMIT 5")
