from sequel import Dialect, parse_document


def test_parse_document_exposes_shared_structure():
    document = parse_document("SELECT id FROM users WHERE id = $1", Dialect.POSTGRESQL)
    assert document["tokens"]
    assert document["statements"][0]["operation"] != 0
    assert document["relations"][0]["name_start"] < document["relations"][0]["name_end"]
    assert document["issues"] == []
    assert document["summary"]
    assert len(document["fingerprint"]) == 16
    assert document["diagnostics"] == []


def test_parse_document_retains_all_diagnostic_layers():
    document = parse_document("SELECT '", Dialect.GENERIC)
    assert document["diagnostics"]
    assert any(item["origin"] == 0 for item in document["diagnostics"])
