package sequel

import "testing"

func TestAnalysisSummaryCompatibility(t *testing.T) {
	queries := []string{
		"SELECT * FROM accounts a JOIN orders o ON o.account_id=a.id AND o.token=$1 AND o.cost>123 LIMIT $2",
		"SELECT * FROM a x LEFT JOIN b y ON x.id=y.a_id AND y.secret='sentinel' AND y.n>=987654 AND y.f<.8765 JOIN c z USING(id,tenant_id) LIMIT 5",
		"SELECT * FROM records WHERE token='secret' LIMIT 10 OFFSET 3",
	}
	for _, query := range queries {
		for _, omit := range []bool{false, true} {
			options := DefaultSanitizeOptions(DialectPostgreSQL)
			options.OmitQueryText = omit
			options.MaxQueryTextBytes = 12
			summary, operation, text, count, ok := Inspect(query, options)
			result := Analyze(query, AnalysisOptions{Dialect: DialectPostgreSQL, OmitQueryText: omit, MaxQueryTextBytes: 12})
			if !ok || result.Status != "ok" || result.Summary != summary || result.Operation != operation || result.Text != text.Text || result.StatementCount != count || result.Truncated != text.Truncated {
				t.Fatalf("unified/legacy metadata differ with omission=%v: summary=%q/%q text=%q/%q", omit, result.Summary, summary, result.Text, text.Text)
			}
		}
	}
}

func TestAnalysisRejectsAmbiguousLiteralBoundaries(t *testing.T) {
	for _, fixture := range []struct {
		dialect Dialect
		sql     string
	}{
		{DialectMySQL, "SELECT $$private$$"},
		{DialectSQLite, "SELECT $tag$private$tag$"},
		{DialectPostgreSQL, "SELECT 'a\\'private\\'b'"},
		{DialectGeneric, "SELECT 'a\\'private\\'b'"},
	} {
		result := Analyze(fixture.sql, AnalysisOptions{Dialect: fixture.dialect, Format: true})
		if result.Status != "malformed" || result.Text != "" || result.Formatted != "" || result.Fingerprint != "" || len(result.Diagnostics) == 0 || result.Diagnostics[0].Code != "ambiguous_literal" {
			t.Fatalf("ambiguous literal not rejected: %#v", result)
		}
	}
}
