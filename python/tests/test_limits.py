"""Shared defaults, dynamic inspection storage and explicit smaller caps."""

import pytest
import sequel


def test_shared_limits():
    assert sequel.MAX_INPUT_BYTES == 1 << 30
    assert sequel.DEFAULT_MAX_QUERY_TEXT_BYTES == 1 << 30
    assert sequel.DEFAULT_MAX_SUMMARY_BYTES == 1 << 30


def test_above_former_limits():
    sql = "SELECT * FROM " + "x" * (1 << 20)
    query = sequel.inspect(sql)
    assert query.text == sql
    assert len(query.summary.encode()) > 1 << 20
    assert not query.truncated
    assert (query.operation, query.statement_count) == ("SELECT", 1)
    assert sequel.inspect(sql, max_query_text_bytes=sequel.DEFAULT_MAX_QUERY_TEXT_BYTES) == query
    bounded = sequel.inspect(sql, max_query_text_bytes=31)
    assert len(bounded.text) == 31 and bounded.truncated
    assert bounded.summary == query.summary
    assert sequel.inspect(sql, omit_query_text=True).text == ""
    assert sequel.diagnose(sql).code == "none"
    assert "x" * (1 << 20) in sequel.format_sql(sql)
    assert "x" * (1 << 20) in sequel.highlight_sql(sql)
    assert sequel.sanitize(sql) == sql


@pytest.mark.parametrize("limit", [-1, sequel.DEFAULT_MAX_QUERY_TEXT_BYTES + 1])
def test_invalid_text_limit(limit):
    with pytest.raises(sequel.SQLInspectionError):
        sequel.inspect("SELECT 1", max_query_text_bytes=limit)


def test_dense_tokens():
    sql = "SELECT " + ", ".join(["column"] * 100_000) + " FROM records;"
    query = sequel.inspect(sql)
    assert not query.truncated and query.text == sql
    assert query.operation == "SELECT" and query.statement_count == 1
    assert sequel.diagnose(sql).code == "none"
    formatted = sequel.format_sql(sql)
    assert formatted.count("column") == 100_000
    assert sequel.inspect(formatted).summary == query.summary
    for mode in (sequel.HighlightMode.HTML, sequel.HighlightMode.ANSI):
        assert sequel.highlight_sql(sql, mode=mode).count("column") == 100_000
    # A lexical error after the former cap must still fail closed.
    with pytest.raises(sequel.SQLInspectionError):
        sequel.inspect(sql + "'secret")
