import html
import re
from dataclasses import FrozenInstanceError

import pytest
from sequel import (
    Diagnostic,
    Dialect,
    SQLDiagnosticError,
    diagnose,
    diagnose_all,
    diagnose_sql,
    format_sql,
    highlight_sql,
)


@pytest.mark.parametrize(
    ("sql", "dialect", "code", "token"),
    [
        ("SELECT '<script>&", Dialect.GENERIC, "unterminated_string", "string"),
        ("SELECT E'escape\\'", Dialect.POSTGRESQL, "unterminated_string", "string"),
        ("SELECT $tag$body $$", Dialect.POSTGRESQL, "unterminated_string", "string"),
        ("SELECT $$body", Dialect.DUCKDB, "unterminated_string", "string"),
        ("SELECT _utf8'abc", Dialect.MYSQL, "unterminated_string", "string"),
        ('SELECT "abc', Dialect.MYSQL, "unterminated_string", "string"),
        ('SELECT "abc', Dialect.POSTGRESQL, "unterminated_identifier", "identifier"),
        ("SELECT `abc", Dialect.MYSQL, "unterminated_identifier", "identifier"),
        ("SELECT [abc", Dialect.SQLITE, "unterminated_identifier", "identifier"),
        ("/* outer /* inner */", Dialect.POSTGRESQL, "unterminated_comment", "comment"),
        ("SELECT {id:UInt64", Dialect.CLICKHOUSE, "unterminated_parameter", "variable"),
        ("SELECT $name::suffix(open", Dialect.SQLITE, "unterminated_parameter", "variable"),
        ("SELECT ‘abc", Dialect.CLICKHOUSE, "unterminated_string", "string"),
        ("SELECT “abc", Dialect.CLICKHOUSE, "unterminated_identifier", "identifier"),
    ],
)
def test_incomplete(sql: str, dialect: Dialect, code: str, token: str) -> None:
    d = diagnose(sql, dialect=dialect)
    assert isinstance(d, Diagnostic)
    assert d.code == code and d.severity == "warning" and d.incomplete
    assert not d.statement_complete and d.end_offset == len(sql.encode())
    assert d.explanation and d.hint
    for render in (format_sql, highlight_sql):
        with pytest.raises(SQLDiagnosticError) as error:
            render(sql, dialect=dialect)
        assert error.value.diagnostic == d
    for mode in ("html", "ansi"):
        rendered = highlight_sql(sql, dialect=dialect, mode=mode, tolerant=True)
        if mode == "html":
            plain = html.unescape(re.sub(r"<[^>]*>", "", rendered))
            assert f"sequel-{token}" in rendered
            assert "<script>" not in rendered
        else:
            plain = re.sub(r"\x1b\[[0-9;]*m", "", rendered)
            assert rendered.endswith("\x1b[0m")
        assert plain == sql


def test_locations_and_exception() -> None:
    sql = "-- 🙂\r\nSELECT é, 'secret"
    d = diagnose(sql)
    assert (d.offset, d.line, d.column) == (20, 2, 11)
    assert (d.end_offset, d.end_line, d.end_column) == (len(sql.encode()), 2, 18)
    assert "secret" not in d.message + d.explanation + d.hint
    with pytest.raises(FrozenInstanceError):
        setattr(d, "code", "other")
    with pytest.raises(SQLDiagnosticError, match="2:11:.*unterminated_string"):
        format_sql(sql)


@pytest.mark.parametrize(
    ("sql", "code"),
    [
        ("select \x00 x", "control_character"),
        ("select '\x1b[2J'", "control_character"),
        ("select '\u009b'", "control_character"),
        ("x " * 65537 + "\x00", "control_character"),
        ("x " * 65537 + "\x1b", "control_character"),
        ("(" * 65, "nesting_limit"),
        ("/*" * 65, "nesting_limit"),
    ],
    ids=["nul", "escape", "c1", "dense-nul", "dense-control", "groups", "comments"],
)
def test_hard_errors(sql: str, code: str) -> None:
    d = diagnose(sql)
    assert d.code == code and not d.incomplete and d.severity == "error"
    for render in (format_sql, highlight_sql):
        if render is highlight_sql and sql == "(" * 65:
            continue
        with pytest.raises(SQLDiagnosticError) as error:
            render(sql)
        assert error.value.diagnostic.code == code
    if sql != "(" * 65:  # Highlighting permits unbalanced delimiter groups.
        with pytest.raises(SQLDiagnosticError) as error:
            highlight_sql(sql, tolerant=True)
        assert error.value.diagnostic.code == code


@pytest.mark.parametrize(
    ("sql", "complete"),
    [
        ("", False),
        ("-- ;", False),
        ("select ';'", False),
        ("select ';'; -- ;", True),
        ("select 1; /* ; */", True),
        ("select 1; select 2", False),
        ("select (1);", True),
    ],
)
def test_completion(sql: str, complete: bool) -> None:
    d = diagnose(sql)
    assert d.code == "none" and not d.incomplete and d.statement_complete == complete


def test_groups_and_unsupported() -> None:
    assert diagnose("select (1").code == "unclosed_group"
    assert diagnose("select (1").incomplete
    assert diagnose("select (1]", dialect=Dialect.POSTGRESQL).code == "unmatched_close"
    assert diagnose("select )").offset == 7


def test_diagnose_all_returns_safe_boundary_issues() -> None:
    diagnostics = diagnose_all("SELECT ); SELECT ]", dialect=Dialect.POSTGRESQL)
    assert [item.code for item in diagnostics] == ["unmatched_close", "unmatched_close"]
    assert all(item.offset < item.end_offset for item in diagnostics)


def test_diagnose_all_clean_sql_is_empty() -> None:
    assert diagnose_all("SELECT 1 FROM widgets", dialect=Dialect.SQLITE) == ()


def test_diagnose_all_includes_opt_in_lint_categories() -> None:
    codes = {item.code for item in diagnose_all("UPDATE widgets SET name = 'x';", dialect=Dialect.SQLITE)}
    assert {"unbounded_write"} <= codes


def test_diagnose_sql_exposes_kind_and_bounds_results() -> None:
    diagnostics = diagnose_sql("UPDATE widgets SET name = 'x';", dialect=Dialect.SQLITE, max_diagnostics=1)
    assert len(diagnostics) == 1
    assert diagnostics[0].kind == "safety"
    sql = "INSERT INTO t FORMAT CSV\nx,y"
    assert diagnose(sql, dialect=Dialect.CLICKHOUSE).code == "unsupported_payload"
    with pytest.raises(SQLDiagnosticError) as error:
        highlight_sql(sql, dialect=Dialect.CLICKHOUSE, tolerant=True)
    assert error.value.diagnostic.code == "unsupported_payload"
    assert diagnose("select {bad}", dialect=Dialect.CLICKHOUSE).code == "invalid_parameter"
    with pytest.raises(SQLDiagnosticError) as error:
        format_sql("SELECT CASE WHEN 1 THEN 2")
    assert error.value.diagnostic.code == "unsupported_layout"
