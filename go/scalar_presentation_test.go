package sequel

import "testing"

func TestShortScalarPresentation(t *testing.T) {
	for _, dialect := range []Dialect{DialectGeneric, DialectSQLite, DialectPostgreSQL, DialectDuckDB, DialectMySQL, DialectClickHouse} {
		for _, query := range []string{"SELECT 1", "SELECT 'private-value'", "SELECT 987654"} {
			got := Analyze(query, AnalysisOptions{Dialect: dialect, Format: true})
			if got.Text != "SELECT ?" || got.Formatted != "SELECT ?" || got.Summary != "SELECT ?" {
				t.Fatalf("dialect %s query %q: text=%q formatted=%q summary=%q", dialect, query, got.Text, got.Formatted, got.Summary)
			}
		}
	}
}
