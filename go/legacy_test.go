package sequel

import (
	"strings"
	"testing"
)

func TestSanitizeDialects(t *testing.T) {
	input := "/* token */ SELECT 'secret', E'private', $$hidden$$, 42, $1, ? FROM `items` WHERE id = 7 -- no"
	got, ok := Sanitize(input)
	if !ok {
		t.Fatal("valid SQL rejected")
	}
	if want := "SELECT ?, ?, ?, ?, ?, ? FROM `items` WHERE id = ?"; got != want {
		t.Fatalf("Sanitize = %q, want %q", got, want)
	}
	for _, secret := range []string{"token", "secret", "private", "hidden", "42"} {
		if strings.Contains(got, secret) {
			t.Fatalf("sanitized query leaked %q: %q", secret, got)
		}
	}
}

func TestSanitizeRejectsUnterminatedContent(t *testing.T) {
	for _, query := range []string{"SELECT 'secret", "SELECT $$secret", "SELECT 1 /* secret"} {
		if _, ok := Sanitize(query); ok {
			t.Fatalf("accepted %q", query)
		}
	}
}

func TestDuckDBSummary(t *testing.T) {
	got := Summary(DialectDuckDB, "SELECT id FROM spans WHERE trace_id = 'secret' ORDER BY start_ns DESC")
	if want := "SELECT … FROM spans WHERE … ORDER BY …"; got != want {
		t.Fatalf("Summary = %q, want %q", got, want)
	}
}
