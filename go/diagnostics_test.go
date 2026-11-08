package sequel

import (
	"errors"
	"html"
	"strings"
	"testing"
)

func TestDiagnostics(t *testing.T) {
	query := "-- 🙂\r\nSELECT é, 'secret"
	d := Diagnose(DialectPostgreSQL, query)
	if d.Code != "unterminated_string" || d.Offset != 20 || d.Line != 2 || d.Column != 11 || d.EndColumn != 18 || !d.Incomplete || d.Severity != "warning" {
		t.Fatalf("locations: %+v", d)
	}
	if strings.Contains(d.Message+d.Explanation+d.Hint, "secret") {
		t.Fatal("diagnostic leaked SQL")
	}
	original, err := FormatSQL(DialectPostgreSQL, query)
	var diagnosticError *DiagnosticError
	if original != query || !errors.As(err, &diagnosticError) || diagnosticError.Diagnostic != d {
		t.Fatalf("typed format error: %q %v", original, err)
	}
	if !strings.Contains(err.Error(), "2:11:") {
		t.Fatal(err)
	}
	for _, tc := range []struct{ query, code string }{
		{"select \x00 x", "control_character"}, {"\xff", "invalid_utf8"}, {"\x1b[2J", "control_character"},
		{strings.Repeat("x ", 65537) + "'broken", "unterminated_string"}, {strings.Repeat("(", 65), "nesting_limit"},
		{"select )", "unmatched_close"}, {"select (1", "unclosed_group"},
	} {
		got := Diagnose(DialectGeneric, tc.query)
		if got.Code != tc.code {
			t.Fatalf("diagnose: %+v want %s", got, tc.code)
		}
		_, err := FormatSQL(DialectGeneric, tc.query)
		if !errors.As(err, &diagnosticError) || diagnosticError.Diagnostic.Code != tc.code {
			t.Fatalf("format: %v", err)
		}
	}
	if Diagnose("unsupported", "").Code != "invalid_argument" {
		t.Fatal("invalid dialect")
	}
	for _, query := range []string{"select ';'; -- ;", "select (1); /* ; */"} {
		if d := Diagnose(DialectGeneric, query); d.Code != "none" || !d.StatementComplete {
			t.Fatalf("completion: %+v", d)
		}
	}
	for _, query := range []string{"", "select ';'", "select 1; select 2", "-- ;"} {
		if d := Diagnose(DialectGeneric, query); d.Code != "none" || d.StatementComplete {
			t.Fatalf("completion: %+v", d)
		}
	}
}

func TestTolerantHighlight(t *testing.T) {
	for _, tc := range []struct {
		dialect Dialect
		query   string
	}{
		{DialectGeneric, "select '<script>&"}, {DialectPostgreSQL, "select $tag$body"}, {DialectPostgreSQL, "/* outer /* inner */"},
		{DialectMySQL, "select `name"}, {DialectSQLite, "select [name"}, {DialectClickHouse, "select {id:UInt64"}, {DialectSQLite, "select $name::suffix(open"},
	} {
		_, err := HighlightSQL(tc.dialect, tc.query, HighlightOptions{})
		var diagnosticError *DiagnosticError
		if !errors.As(err, &diagnosticError) || !diagnosticError.Diagnostic.Incomplete {
			t.Fatalf("strict error: %v", err)
		}
		for _, mode := range []HighlightMode{HighlightHTML, HighlightANSI} {
			got, err := HighlightSQL(tc.dialect, tc.query, HighlightOptions{Mode: mode, Tolerant: true})
			if err != nil {
				t.Fatal(err)
			}
			plain := highlightSGR.ReplaceAllString(got, "")
			if mode == HighlightHTML {
				plain = html.UnescapeString(highlightTags.ReplaceAllString(got, ""))
			}
			if plain != tc.query {
				t.Fatalf("changed incomplete SQL: %q", plain)
			}
		}
	}
	for _, query := range []string{"select \x00", "\xff", "\x1b", strings.Repeat("x ", 65537) + "\x00"} {
		if _, err := HighlightSQL(DialectGeneric, query, HighlightOptions{Tolerant: true}); err == nil {
			t.Fatal("tolerated hard error")
		}
	}
}

func TestDiagnoseAllReturnsEverySafeBoundaryIssue(t *testing.T) {
	diagnostics := DiagnoseAll("SELECT ); SELECT ]", DialectPostgreSQL)
	if len(diagnostics) != 2 {
		t.Fatalf("diagnostics = %#v", diagnostics)
	}
	if diagnostics[0].Code != "unmatched_close" || diagnostics[1].Code != "unmatched_close" {
		t.Fatalf("diagnostics = %#v", diagnostics)
	}
}

func TestDiagnoseAllCleanSQLIsEmpty(t *testing.T) {
	if diagnostics := DiagnoseAll("SELECT 1 FROM widgets", DialectSQLite); diagnostics == nil || len(diagnostics) != 0 {
		t.Fatalf("diagnostics = %#v", diagnostics)
	}
}

func TestDiagnoseAllIncludesOptInLintCategories(t *testing.T) {
	diagnostics := DiagnoseAll("UPDATE widgets SET name = 'x';", DialectSQLite)
	codes := make(map[string]bool, len(diagnostics))
	for _, diagnostic := range diagnostics {
		codes[diagnostic.Code] = true
	}
	if !codes["unbounded_write"] {
		t.Fatalf("missing unbounded_write in %#v", diagnostics)
	}
}
