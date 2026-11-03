"""Cross-language native analysis contract, privacy and browser provenance tests."""

import hashlib
import json
from copy import deepcopy
from dataclasses import replace
from typing import cast

import pytest
from sequel import (
    ANALYSIS_SCHEMA_VERSION,
    DIAGNOSTIC_SCHEMA_VERSION,
    MAX_INPUT_BYTES,
    Analysis,
    Dialect,
    MapSegment,
    SourceMap,
    SQLDiagnosticError,
    analyze,
    canonicalize_diagnostic,
    check_binds,
    diagnostic_catalog,
    fingerprint,
    utf8_to_utf16,
    utf16_to_utf8,
    validate_diagnostic,
)


def test_unified_analysis_and_versioned_roundtrip():
    sql = "SELECT name FROM users WHERE secret='private-token' AND id=:account /* private-comment */;"
    result = analyze(sql, dialect=Dialect.SQLITE, format=True)
    assert result.schema_version == ANALYSIS_SCHEMA_VERSION == 1
    assert result.status == "ok"
    assert result.omission_reason == ""
    assert result.original_length == len(sql.encode())
    assert result.operation == "SELECT"
    assert result.statement_count == len(result.statements) == 1
    assert result.summary
    assert result.fingerprint.startswith("sequel:v1:sqlite:")
    assert result.text and result.formatted
    assert not result.diagnostics
    assert result.memory_peak_bytes > 0 and result.work_units > 0
    wire = json.loads(json.dumps(result.to_dict()))
    assert Analysis.from_dict(wire) == result
    serialized = json.dumps(wire)
    for secret in ("private-token", "private-comment", "account"):
        assert secret not in serialized
    assert "original_sql" not in serialized
    assert result.binds[0].style == "named"


@pytest.mark.parametrize("dialect", list(Dialect))
def test_fingerprint_literal_comment_whitespace_invariance(dialect):
    first = fingerprint("SELECT a FROM users WHERE id=17 AND secret='one'", dialect=dialect)
    second = fingerprint(" SELECT /* hidden */ a\nFROM users WHERE id=99 AND secret='two' ", dialect=dialect)
    assert first == second
    assert len(first.rsplit(":", 1)[1]) == 64


def test_fingerprint_v1_generic_golden_matches_public_frame_contract():
    expected = "2c01bfb45c6e98658d756ef07e1cd3a4c2d82cb4c0d841c61fd4516786a19145"

    def frame(kind: bytes, payload: bytes) -> bytes:
        return kind + len(payload).to_bytes(8, "big") + payload

    stream = (
        frame(b"V", b"sequel-fingerprint-v1")
        + frame(b"D", b"generic")
        + frame(b"W", b"SELECT")
        + b"B"
        + (1).to_bytes(8, "big")
        + frame(b"Y", b"")
    )
    assert hashlib.sha256(stream).hexdigest() == expected
    assert fingerprint("SELECT 1") == f"sequel:v1:generic:{expected}"
    assert fingerprint("SELECT 1;") == (
        "sequel:v1:generic:aaa55d1efdf56f571c85bb0452639e8e3c8ab31c12f0c3394059f71273ff08ef"
    )


@pytest.mark.parametrize(
    "variant",
    (
        "SELECT 'private value'",
        "SELECT .123",
        "SELECT 0.123e9",
        "SELECT TRUE",
        "SELECT NULL",
        "SELECT :renamed",
        "SELECT $3",
        "SELECT ?",
    ),
)
def test_fingerprint_literals_and_untyped_binds_share_parameter_shape(variant):
    assert fingerprint(variant) == fingerprint("SELECT 1")


def test_fingerprint_literal_equality_is_ignored_but_bind_repetition_is_retained():
    independent = fingerprint("SELECT 42,42")
    assert independent == "sequel:v1:generic:c13c23330aafb2655f8893bd84283f9943a5a6e18779863c1d26ff631e40b439"
    for query in ("SELECT :one,:two", "SELECT :one,42", "SELECT 42,:one"):
        assert fingerprint(query) == independent
    repeated = fingerprint("SELECT :one,:one")
    assert repeated == "sequel:v1:generic:8a25dc329c188d61398bd98d10b87f61b20d8ea67a2e9882b21d3934407258b6"
    assert repeated != independent
    assert repeated == fingerprint("SELECT $1,$01")
    assert fingerprint("SELECT 42,:one,:one") == fingerprint("SELECT :a,:b,:b")
    assert fingerprint("SELECT 42,:one,:one") != fingerprint("SELECT :a,:b,:a")
    assert fingerprint("SELECT 1;SELECT 2") == fingerprint("SELECT :a;SELECT :a")


@pytest.mark.parametrize("dialect", (Dialect.GENERIC, Dialect.SQLITE, Dialect.POSTGRESQL, Dialect.DUCKDB))
def test_fingerprint_case_folds_ordinary_words_in_folded_dialects(dialect):
    assert fingerprint("SELECT X FROM T", dialect=dialect) == fingerprint("select x from t", dialect=dialect)


@pytest.mark.parametrize("dialect", (Dialect.MYSQL, Dialect.CLICKHOUSE))
def test_fingerprint_retains_word_case_in_case_sensitive_dialects(dialect):
    assert fingerprint("SELECT X FROM T", dialect=dialect) != fingerprint("SELECT x FROM t", dialect=dialect)
    assert fingerprint("SELECT function_name(1)", dialect=dialect) != fingerprint(
        "SELECT FUNCTION_NAME(1)", dialect=dialect
    )


def test_fingerprint_retains_structure_quoted_identifiers_and_dialect():
    queries = [
        "SELECT ab,c FROM t",
        "SELECT a,bc FROM t",
        'SELECT "Foo" FROM t',
        'SELECT "foo" FROM t',
        "SELECT foo FROM t",
        "SELECT a + b FROM t",
        "SELECT a - b FROM t",
    ]
    assert len({fingerprint(query) for query in queries}) == len(queries)
    assert fingerprint("SELECT 1", dialect=Dialect.SQLITE) != fingerprint("SELECT 1", dialect=Dialect.POSTGRESQL)


def test_fingerprint_bind_spelling_and_repeated_identity():
    first = fingerprint("SELECT :first + :first", dialect=Dialect.SQLITE)
    assert first == fingerprint("SELECT :renamed + :renamed", dialect=Dialect.SQLITE)
    assert first != fingerprint("SELECT :first + :second", dialect=Dialect.SQLITE)
    assert fingerprint("SELECT $1 + $1", dialect=Dialect.POSTGRESQL) == fingerprint(
        "SELECT $9 + $9", dialect=Dialect.POSTGRESQL
    )
    assert fingerprint("SELECT $1 + $1", dialect=Dialect.POSTGRESQL) != fingerprint(
        "SELECT $1 + $2", dialect=Dialect.POSTGRESQL
    )


def test_malformed_input_fails_closed_and_fingerprint_raises():
    secret = "must-never-leak"
    result = analyze(f"SELECT '{secret}", format=True)
    assert result.status == "malformed"
    assert result.omission_reason == "malformed"
    assert result.diagnostics[0].code == "unterminated_string"
    assert not result.text and not result.formatted and not result.fingerprint
    assert not result.binds and not result.original_to_sanitized.segments
    assert secret not in json.dumps(result.to_dict())
    with pytest.raises(SQLDiagnosticError, match="unterminated_string"):
        fingerprint(f"SELECT '{secret}")


def test_recovery_only_at_safe_statement_boundaries():
    result = analyze("SELECT ); SELECT );")
    assert result.status == "malformed"
    assert len(result.diagnostics) >= 2
    assert result.diagnostics[0].offset < result.diagnostics[1].offset
    unsafe = analyze("SELECT 'unclosed; SELECT ); SELECT );")
    assert unsafe.status == "malformed"
    assert len(unsafe.diagnostics) == 1
    assert unsafe.diagnostics[0].code == "unterminated_string"


def test_embedded_nul_preserves_original_length_without_truncation():
    sql = "SELECT 1\0SECRET"
    result = analyze(sql)
    assert result.original_length == len(sql.encode())
    assert result.status == "malformed"
    assert result.diagnostics[0].code == "control_character"
    assert not result.fingerprint and not result.text
    assert "SECRET" not in json.dumps(result.to_dict())


def test_source_map_replacements_and_deleted_comment_are_explicit():
    sql = "SELECT 'secret-value' /* deleted-secret */ FROM users"
    result = analyze(sql)
    start = sql.index("'secret-value'")
    end = start + len("'secret-value'")
    mapped = result.original_to_sanitized.map_range(start, end)
    assert not mapped.exact
    assert result.text.encode()[mapped.start : mapped.end] == b"?"
    restored = result.original_to_sanitized.map_range(mapped.start, mapped.end, reverse=True)
    assert restored.start == start and restored.end == end and not restored.exact
    comment_start = sql.index("/*")
    comment_end = sql.index("*/") + 2
    deleted = result.original_to_sanitized.map_range(comment_start, comment_end)
    assert not deleted.exact
    assert deleted.end - deleted.start <= 1
    assert any(segment.kind == "delete" for segment in result.original_to_sanitized.segments)
    assert "secret" not in repr(result.original_to_sanitized)


def test_source_maps_reach_sanitized_formatted_identifiers_with_astral_text():
    sql = 'SELECT "😀é" FROM users WHERE id=123;'
    result = analyze(sql, format=True)
    start = sql.encode().index('"😀é"'.encode())
    end = start + len('"😀é"'.encode())
    safe = result.original_to_sanitized.map_range(start, end)
    assert safe.exact
    displayed = result.sanitized_to_formatted.map_range(safe.start, safe.end)
    assert displayed.exact
    assert result.formatted.encode()[displayed.start : displayed.end] == '"😀é"'.encode()
    assert utf8_to_utf16(result.formatted, displayed.end) - utf8_to_utf16(result.formatted, displayed.start) == 6


def test_omitted_maps_and_binds_keep_fingerprint():
    sql = "SELECT :id FROM users WHERE id=:id"
    full = analyze(sql)
    slim = analyze(sql, omit_source_maps=True, omit_bind_descriptors=True)
    assert full.fingerprint == slim.fingerprint
    assert full.binds and full.original_to_sanitized.segments
    assert not slim.binds
    assert slim.original_to_sanitized == SourceMap(0, 0)
    assert slim.sanitized_to_formatted == SourceMap(0, 0)
    assert Analysis.from_dict(slim.to_dict()) == slim


def test_explicit_omission_retains_analysis_metadata():
    result = analyze("SELECT 12 FROM users", omit_query_text=True)
    assert result.status == "ok"
    assert result.omission_reason == "policy"
    assert not result.text and not result.formatted
    assert result.fingerprint and result.summary and result.operation == "SELECT"


def test_empty_analysis_has_explicit_absence_reason():
    result = analyze(" /* comment */ ")
    assert result.status == "ok"
    assert result.omission_reason == "empty"
    assert not result.text and not result.diagnostics
    assert result.statement_count == 0


def test_input_limit_transport_preserves_original_length_above_default_ceiling():
    # Rejected explicit-length input can report its full length without scanning
    # it. Exercise transport with numeric metadata, never allocating that input.
    limited = analyze("SELECT 1", max_input_bytes=1)
    reported_length = MAX_INPUT_BYTES + 17
    synthetic = replace(limited, original_length=reported_length)
    restored = Analysis.from_dict(json.loads(json.dumps(synthetic.to_dict())))
    assert restored == synthetic
    assert restored.original_length > MAX_INPUT_BYTES
    assert restored.status == "limit" and restored.omission_reason == "analysis_limit"
    assert restored.diagnostics[0].code == "input_limit"
    assert not restored.text and not restored.fingerprint
    location = replace(
        restored.diagnostics[0],
        offset=MAX_INPUT_BYTES,
        end_offset=MAX_INPUT_BYTES,
        line=1,
        end_line=1,
        column=MAX_INPUT_BYTES + 1,
        end_column=MAX_INPUT_BYTES + 1,
    )
    assert canonicalize_diagnostic(location, source_length=reported_length) == location


def _analyze_with_budget(option: str, value: int):
    sql = "SELECT 'secret-value' FROM users WHERE id=123"
    if option == "max_input_bytes":
        return analyze(sql, max_input_bytes=value)
    if option == "max_output_bytes":
        return analyze(sql, max_output_bytes=value)
    if option == "max_memory_bytes":
        return analyze(sql, max_memory_bytes=value)
    return analyze(sql, max_work_units=value)


@pytest.mark.parametrize("option", ("max_input_bytes", "max_output_bytes", "max_memory_bytes", "max_work_units"))
def test_budget_failure_drops_partial_data(option):
    result = _analyze_with_budget(option, 1)
    assert result.status == "limit"
    assert result.omission_reason == "analysis_limit"
    assert not result.text and not result.formatted and not result.fingerprint
    assert result.diagnostics
    assert not result.binds and not result.original_to_sanitized.segments
    assert "secret-value" not in json.dumps(result.to_dict())


def test_query_text_cap_is_independent_of_total_output_budget():
    result = analyze("SELECT a,b,c FROM users WHERE id=123", max_query_text_bytes=12)
    assert result.status == "ok"
    assert result.truncated
    assert len(result.text.encode()) <= 12
    assert result.fingerprint
    assert result.original_to_sanitized.to_length == len(result.text.encode())
    assert not analyze("SELECT a FROM users", max_query_text_bytes=-1).truncated


def test_optional_format_failure_preserves_valid_lexical_analysis():
    result = analyze("SELECT CASE WHEN 1 THEN 2;", format=True)
    assert result.status == "ok"
    assert result.text and result.fingerprint
    assert not result.diagnostics
    assert not result.formatted and not result.sanitized_to_formatted.segments
    assert Analysis.from_dict(result.to_dict()) == result
    truncated = analyze("SELECT a,b,c FROM users", max_query_text_bytes=5, format=True)
    assert truncated.status == "ok" and truncated.truncated
    assert not truncated.formatted and not truncated.sanitized_to_formatted.segments


def test_cancellation_during_native_work_and_exception_propagation():
    checks = 0

    def cancelled():
        nonlocal checks
        checks += 1
        return checks >= 3

    result = analyze("SELECT " + ",".join(str(number) for number in range(5000)), cancelled=cancelled)
    assert checks >= 3
    assert result.status == "cancelled"
    assert result.omission_reason == "cancelled"
    assert not result.text and not result.fingerprint

    def broken_callback():
        raise RuntimeError("callback failure")

    with pytest.raises(RuntimeError, match="callback failure"):
        analyze("SELECT 1", cancelled=broken_callback)


def test_bind_descriptors_repeated_named_and_explicit_numbered_arity():
    result = analyze("SELECT :x,:x,:y; SELECT ?;", dialect=Dialect.SQLITE)
    assert [bind.slot for bind in result.binds[:3]] == [1, 1, 2]
    assert result.binds[-1].statement_index == 1
    check_binds(result.binds, 2)
    check_binds(result.binds, 1, statement_index=1)
    with pytest.raises(ValueError):
        check_binds(result.binds, 1)
    numbered = analyze("SELECT $3", dialect=Dialect.POSTGRESQL)
    assert numbered.binds[0].slot == 3
    check_binds(numbered.binds, 3)
    with pytest.raises(ValueError):
        check_binds(numbered.binds, 1)


def test_utf16_conversions_astral_combining_marks_and_crlf():
    text = "A😀é\r\nZ"
    boundaries = [(0, 0), (1, 1), (5, 3), (6, 4), (8, 5), (9, 6), (10, 7), (11, 8)]
    for byte_offset, units in boundaries:
        assert utf8_to_utf16(text, byte_offset) == units
        assert utf16_to_utf8(text, units) == byte_offset
    for split in (2, 3, 4, 7):
        with pytest.raises(ValueError):
            utf8_to_utf16(text, split)
    for split in (2, 9):
        with pytest.raises(ValueError):
            utf16_to_utf8(text, split)


def test_diagnostic_catalog_validation_and_canonicalization():
    catalog = diagnostic_catalog()
    assert DIAGNOSTIC_SCHEMA_VERSION == 1
    assert len({item.code for item in catalog}) == len(catalog)
    assert {"unterminated_string", "unmatched_close", "output_limit"} <= {item.code for item in catalog}
    diagnostic = analyze("SELECT );").diagnostics[0]
    validate_diagnostic(diagnostic, source_length=9)
    canonical = canonicalize_diagnostic(
        replace(diagnostic, message="private", explanation="private", hint="private"), source_length=9
    )
    assert canonical == diagnostic
    assert "private" not in repr(canonical)
    for invalid in (
        replace(diagnostic, code="future_code"),
        replace(diagnostic, severity="future_severity"),
        replace(diagnostic, offset=10),
        replace(diagnostic, end_offset=100),
        replace(diagnostic, line=0),
        replace(diagnostic, incomplete=1),
    ):
        with pytest.raises(ValueError):
            validate_diagnostic(invalid, source_length=9)
    for version in (0, 2, 2**32 + 1):
        with pytest.raises(ValueError):
            validate_diagnostic(diagnostic, source_length=9, version=version)


def test_transport_unknown_versions_map_corruption_and_diagnostic_prose():
    result = analyze("SELECT 'secret' FROM users", format=True)
    wire = result.to_dict()
    corrupted = deepcopy(wire)
    corrupted["schema_version"] = 2
    with pytest.raises(ValueError):
        Analysis.from_dict(corrupted)
    corrupted = deepcopy(wire)
    corrupted["original_length"] = 0
    with pytest.raises(ValueError):
        Analysis.from_dict(corrupted)
    invalid_map = SourceMap(2, 2, (MapSegment(1, 2, 0, 2, "copy"),))
    with pytest.raises(ValueError):
        invalid_map.map_range(0, 1)
    with pytest.raises(ValueError):
        result.original_to_sanitized.map_range(-1, 0)
    failure = analyze("SELECT );").to_dict()
    cast(list[dict[str, object]], failure["diagnostics"])[0]["message"] = "injected secret"
    restored = Analysis.from_dict(failure)
    assert "injected secret" not in repr(restored)


@pytest.mark.parametrize("option", ("max_input_bytes", "max_output_bytes", "max_memory_bytes", "max_work_units"))
def test_budget_arguments_reject_bool_and_negative(option):
    for value in (-1, True):
        with pytest.raises(ValueError):
            _analyze_with_budget(option, value)


def test_pyformat_and_clickhouse_typed_bind_metadata():
    positional = analyze("SELECT %s, %s", dialect=Dialect.POSTGRESQL)
    assert [bind.style for bind in positional.binds] == ["positional", "positional"]
    assert [bind.slot for bind in positional.binds] == [1, 2]
    check_binds(positional.binds, 2)
    named = analyze("SELECT %(secret_name)s, %(secret_name)s", dialect=Dialect.POSTGRESQL)
    assert [bind.style for bind in named.binds] == ["named", "named"]
    assert [bind.slot for bind in named.binds] == [1, 1]
    assert "secret_name" not in json.dumps(named.to_dict())
    typed = analyze("SELECT {private_name:UInt64}, {private_name:UInt64}", dialect=Dialect.CLICKHOUSE)
    assert [bind.style for bind in typed.binds] == ["typed", "typed"]
    assert [bind.slot for bind in typed.binds] == [1, 1]
    assert "private_name" not in json.dumps(typed.to_dict())
    assert typed.fingerprint == fingerprint(
        "SELECT {different_name:UInt64}, {different_name:UInt64}", dialect=Dialect.CLICKHOUSE
    )
    assert typed.fingerprint != fingerprint(
        "SELECT {different_name:String}, {different_name:String}", dialect=Dialect.CLICKHOUSE
    )
    assert typed.fingerprint != fingerprint(
        "SELECT {different_name:uint64}, {different_name:uint64}", dialect=Dialect.CLICKHOUSE
    )


def test_analysis_transport_rejects_unknown_fields_and_inconsistent_status():
    wire = analyze("SELECT 1").to_dict()
    unknown = dict(wire, raw_sql="private SQL must not be retained")
    with pytest.raises(ValueError, match="Unsupported analysis object fields"):
        Analysis.from_dict(unknown)
    with pytest.raises(ValueError):
        Analysis.from_dict(dict(wire, omission_reason="malformed"))
    with pytest.raises(ValueError):
        Analysis.from_dict(dict(wire, statement_count=True))
    with pytest.raises(ValueError):
        Analysis.from_dict(dict(wire, text="\ud800"))
    malformed = analyze("SELECT );").to_dict()
    with pytest.raises(ValueError):
        Analysis.from_dict(dict(malformed, omission_reason="analysis_limit"))
    with pytest.raises(ValueError):
        Analysis.from_dict(dict(malformed, truncated=True))
    missing = dict(wire)
    del missing["original_length"]
    with pytest.raises(ValueError, match="Missing analysis field"):
        Analysis.from_dict(missing)


def test_analysis_serialization_canonicalizes_manual_diagnostic_prose():
    result = analyze("SELECT );")
    tampered = replace(result, diagnostics=(replace(result.diagnostics[0], message="private injected SQL"),))
    assert tampered.to_dict() == result.to_dict()
    assert "private injected SQL" not in json.dumps(tampered.to_dict())


def test_transport_rejects_bind_ranges_outside_statements_and_out_of_order():
    result = analyze("SELECT :a; SELECT :b;", dialect=Dialect.SQLITE)
    first, second = result.binds
    invalid = (
        replace(result, binds=(replace(first, ordinal=2), second)),
        replace(result, binds=(first, replace(second, offset=first.offset))),
        replace(result, binds=(first, replace(second, statement_index=0))),
        replace(result, binds=(first, replace(second, end_offset=result.original_length + 1))),
    )
    for item in invalid:
        with pytest.raises(ValueError):
            item.to_dict()


def test_source_map_empty_anchors_and_inverse_insertions():
    mapping = SourceMap(
        8,
        5,
        (
            MapSegment(0, 1, 0, 1, "copy"),
            MapSegment(1, 5, 1, 2, "replace"),
            MapSegment(5, 7, 2, 2, "delete"),
            MapSegment(7, 7, 2, 4, "insert"),
            MapSegment(7, 8, 4, 5, "copy"),
        ),
    )
    replaced = mapping.map_range(2, 3)
    assert (replaced.start, replaced.end, replaced.exact) == (1, 2, False)
    deleted = mapping.map_range(5, 7)
    assert (deleted.start, deleted.end, deleted.exact) == (2, 2, False)
    insertion = mapping.map_range(2, 4, reverse=True)
    assert (insertion.start, insertion.end, insertion.exact) == (7, 7, False)
    deletion_anchor = mapping.map_range(2, 2, reverse=True)
    assert (deletion_anchor.start, deletion_anchor.end, deletion_anchor.exact) == (5, 7, False)
    for start, end in ((2, 1), (0, 9)):
        with pytest.raises(ValueError):
            mapping.map_range(start, end)


@pytest.mark.parametrize(
    "sql",
    (
        "SELECT '" + "x" * 20_000 + "'",
        "/*" + "x" * 20_000 + "*/ SELECT 1",
        'SELECT "' + "x" * 20_000 + '"',
        " " * 20_000 + "SELECT 1",
    ),
)
def test_cancellation_polls_inside_long_lexical_regions(sql):
    checks = 0

    def cancelled():
        nonlocal checks
        checks += 1
        return checks >= 4

    result = analyze(sql, cancelled=cancelled)
    assert result.status == "cancelled"
    assert checks >= 4
    assert not result.text and not result.fingerprint
    assert result.work_units < len(sql)


def test_duckdb_anonymous_binding() -> None:
    result = analyze("SELECT ?", dialect=Dialect.DUCKDB)
    assert result.status == "ok"
    assert len(result.binds) == 1 and result.binds[0].slot == 1
    assert result.fingerprint == fingerprint("SELECT 1", dialect=Dialect.DUCKDB)


@pytest.mark.parametrize("dialect", [Dialect.MYSQL, Dialect.CLICKHOUSE])
def test_keyword_case_and_function_identity(dialect: Dialect) -> None:
    assert fingerprint("SELECT fn(x) FROM t", dialect=dialect) == fingerprint("select fn(x) from t", dialect=dialect)
    assert fingerprint("SELECT fn(x) FROM t", dialect=dialect) != fingerprint("SELECT Fn(x) FROM t", dialect=dialect)
