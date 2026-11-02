package sequel

import (
	"strings"
	"testing"
)

func TestSharedLimits(t *testing.T) {
	if MaxInputBytes != 1<<30 || DefaultMaxQueryTextBytes != 1<<30 || DefaultMaxSummaryBytes != 1<<30 {
		t.Fatal("C and Go limits must be 1 GiB")
	}
	query := "SELECT * FROM " + strings.Repeat("x", 1<<20)
	summary, operation, text, count, ok := Inspect(query, DefaultSanitizeOptions(DialectGeneric))
	if !ok || operation != "SELECT" || count != 1 || text.Truncated || text.Text != query || len(summary) <= 1<<20 {
		t.Fatal("inspection truncated a query above the former limits")
	}
	if Summary(DialectGeneric, query) != summary {
		t.Fatal("summary paths disagree")
	}
	if result, ok := Sanitize(query); !ok || result != query {
		t.Fatal("sanitization kept old input or output cap")
	}
	options := DefaultSanitizeOptions(DialectGeneric)
	options.MaxQueryTextBytes = 31
	_, _, bounded, _, ok := Inspect(query, options)
	if !ok || !bounded.Truncated || len(bounded.Text) != 31 {
		t.Fatal("explicit small cap was lost")
	}
	if d := Diagnose(DialectGeneric, query); d.Code != "none" {
		t.Fatal(d)
	}
	if result, err := FormatSQL(DialectGeneric, query); err != nil || !strings.Contains(result, strings.Repeat("x", 1<<20)) {
		t.Fatal("format kept old input cap", err)
	}
	if _, err := HighlightSQL(DialectGeneric, query, HighlightOptions{}); err != nil {
		t.Fatal("highlight kept old input cap", err)
	}
}

func TestDenseTokens(t *testing.T) {
	query := "SELECT " + strings.Repeat("column, ", 100000) + "last FROM records;"
	summary, operation, text, count, ok := Inspect(query, DefaultSanitizeOptions(DialectGeneric))
	if !ok || operation != "SELECT" || count != 1 || text.Truncated || text.Text != query || summary == "SQL" {
		t.Fatal("dense-token inspection failed")
	}
	if d := Diagnose(DialectGeneric, query); d.Code != "none" {
		t.Fatal(d)
	}
	if formatted, err := FormatSQL(DialectGeneric, query); err != nil || strings.Count(formatted, "column") != 100000 {
		t.Fatal("dense-token formatting failed", err)
	}
	for _, mode := range []HighlightMode{HighlightHTML, HighlightANSI} {
		if rendered, err := HighlightSQL(DialectGeneric, query, HighlightOptions{Mode: mode}); err != nil || strings.Count(rendered, "column") != 100000 {
			t.Fatal("dense-token highlighting failed", err)
		}
	}
	if _, ok := Sanitize(query + "'secret"); ok {
		t.Fatal("ignored malformed suffix after former cap")
	}
}
