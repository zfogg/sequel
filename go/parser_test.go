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
