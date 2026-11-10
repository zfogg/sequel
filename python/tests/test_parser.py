from sequel import Dialect, parse_document


def test_parse_document_exposes_shared_structure():
    document = parse_document("SELECT id FROM users WHERE id = $1", Dialect.POSTGRESQL)
    assert document["tokens"]
    assert document["statements"][0]["operation"] != 0
    assert document["relations"][0]["name_start"] < document["relations"][0]["name_end"]
    assert document["issues"] == []
