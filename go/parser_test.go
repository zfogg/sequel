package sequel

import "testing"

func TestParseExposesLosslessTokensAndQueryModel(t *testing.T) {
	document, err := Parse("SELECT a FROM users WHERE id = $1", DialectPostgreSQL)
	if err != nil {
		t.Fatal(err)
	}
	defer document.Close()
	if len(document.Tokens()) < 8 {
		t.Fatalf("token count = %d", len(document.Tokens()))
	}
	query := document.Query()
	if len(query.Statements) != 1 {
		t.Fatalf("statements = %d", len(query.Statements))
	}
	if len(query.Relations) != 1 {
		t.Fatalf("relations = %d", len(query.Relations))
	}
	sanitized, err := document.Sanitize(SanitizeReplaceLiterals)
	if err != nil || sanitized == "" {
		t.Fatalf("sanitize = %q, %v", sanitized, err)
	}
	if summary, err := document.Summary(); err != nil || summary == "" {
		t.Fatalf("summary = %q, %v", summary, err)
	}
	if fingerprint, err := document.Fingerprint(FingerprintQueryShape); err != nil || len(fingerprint) != 16 {
		t.Fatalf("fingerprint = %q, %v", fingerprint, err)
	}
	if diagnostics := document.Diagnostics(); len(diagnostics) != 0 {
		t.Fatalf("diagnostics = %#v", diagnostics)
	}
}

func TestParseRejectsEmbeddedNUL(t *testing.T) {
	if _, err := Parse("SELECT\x00 1", DialectGeneric); err == nil {
		t.Fatal("Parse accepted an embedded NUL byte")
	}
}

func TestParseExposesCTERanges(t *testing.T) {
	document, err := Parse("WITH recent AS NOT MATERIALIZED (SELECT id FROM events) SELECT id FROM recent", DialectPostgreSQL)
	if err != nil {
		t.Fatal(err)
	}
	defer document.Close()
	ctes := document.Query().CTEs
	if len(ctes) != 1 || ctes[0].NameEnd-ctes[0].NameStart != len("recent") || ctes[0].BodyEnd <= ctes[0].BodyStart {
		t.Fatalf("unexpected CTE metadata: %#v", ctes)
	}
}

func TestParseReturnsLayeredDiagnostics(t *testing.T) {
	document, err := Parse("SELECT * FROM users; SELECT 1", DialectGeneric)
	if err != nil {
		t.Fatal(err)
	}
	defer document.Close()
	if len(document.Diagnostics()) == 0 {
		t.Fatal("expected query diagnostics")
	}
}

func TestParseReturnsRecoveryIssues(t *testing.T) {
	document, err := Parse("SELECT (1", DialectGeneric)
	if err != nil {
		t.Fatal(err)
	}
	defer document.Close()
	if len(document.Issues()) == 0 {
		t.Fatal("expected recovery issue")
	}
}
