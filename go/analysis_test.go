package sequel

import (
	"context"
	"encoding/json"
	"reflect"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
)

type cancelOnPoll struct {
	context.Context
	polls atomic.Int32
}

func (c *cancelOnPoll) Err() error {
	if c.polls.Add(1) >= 4 {
		return context.Canceled
	}
	return nil
}

func TestCancellationDuringNativeWork(t *testing.T) {
	ctx := &cancelOnPoll{Context: context.Background()}
	query := "SELECT '" + strings.Repeat("private", 50000) + "' FROM t"
	a := Analyze(query, AnalysisOptions{Context: ctx})
	if a.Status != "cancelled" || a.Text != "" || a.Fingerprint != "" || ctx.polls.Load() < 4 {
		t.Fatalf("native work did not poll cancellation: %s polls=%d", a.Status, ctx.polls.Load())
	}
	if a.WorkUnits >= uint64(len(query))*4 {
		t.Fatalf("cancelled scan continued excessive work: %d", a.WorkUnits)
	}
}

func requireAnalysis(t *testing.T, query string, options AnalysisOptions) Analysis {
	t.Helper()
	a := Analyze(query, options)
	if a.Status != "ok" {
		t.Fatalf("analysis status %q diagnostics %#v", a.Status, a.Diagnostics)
	}
	if a.SchemaVersion != AnalysisSchemaVersion || a.OriginalLength != len(query) {
		t.Fatalf("bad version/length: %#v", a)
	}
	return a
}

func TestAnalysisFingerprintCanonicalShape(t *testing.T) {
	golden, err := Fingerprint(DialectGeneric, "SELECT 1")
	if err != nil || golden != "sequel:v1:generic:2c01bfb45c6e98658d756ef07e1cd3a4c2d82cb4c0d841c61fd4516786a19145" {
		t.Fatalf("cross-language fingerprint v1 golden changed: %s, %v", golden, err)
	}
	tests := []struct {
		dialect     Dialect
		left, right string
	}{
		{DialectPostgreSQL, "SELECT a FROM t WHERE b = 12 AND c = 'secret'", " SELECT a /* confidential */ FROM t WHERE b=891 AND c='other' "},
		{DialectSQLite, "SELECT :first + :first", "SELECT :renamed+:renamed"},
		{DialectPostgreSQL, "SELECT $1 + $1", "SELECT $13+$13"},
		{DialectMySQL, "SELECT %(first)s + %(first)s", "SELECT %(renamed)s+%(renamed)s"},
		{DialectClickHouse, "SELECT {first:UInt64}+{first:UInt64}", "SELECT {renamed:UInt64} + {renamed:UInt64}"},
		{DialectSQLite, "SELECT 1", "SELECT ?"},
		{DialectSQLite, "SELECT 'private', 2", "SELECT :first, :second"},
		{DialectSQLite, "SELECT 5, :x, :x", "SELECT :literal, :renamed, :renamed"},
	}
	for _, tt := range tests {
		t.Run(string(tt.dialect)+tt.left, func(t *testing.T) {
			a, err := Fingerprint(tt.dialect, tt.left)
			if err != nil {
				t.Fatal(err)
			}
			b, err := Fingerprint(tt.dialect, tt.right)
			if err != nil {
				t.Fatal(err)
			}
			if a != b {
				t.Fatalf("shape changed: %s != %s", a, b)
			}
			if !strings.HasPrefix(a, "sequel:v1:"+string(tt.dialect)+":") || len(a) != len("sequel:v1:"+string(tt.dialect)+":")+64 {
				t.Fatalf("unversioned fingerprint %q", a)
			}
		})
	}
	distinct := [][2]string{
		{"SELECT a b", "SELECT ab"},
		{"SELECT \"Mixed\"", "SELECT \"mixed\""},
		{"SELECT \"x\"", "SELECT x"},
		{"SELECT :a+:a", "SELECT :a+:b"},
		{"SELECT 1, 1", "SELECT :a, :a"},
		{"SELECT 5, :x, :x", "SELECT :same, :same, :same"},
		{"SELECT a>=1", "SELECT a>1"},
	}
	for _, queries := range distinct {
		a, err := Fingerprint(DialectSQLite, queries[0])
		if err != nil {
			t.Fatal(err)
		}
		b, err := Fingerprint(DialectSQLite, queries[1])
		if err != nil {
			t.Fatal(err)
		}
		if a == b {
			t.Fatalf("different token structures collide: %q %q", queries[0], queries[1])
		}
	}
	a, _ := Fingerprint(DialectSQLite, "SELECT 1")
	b, _ := Fingerprint(DialectPostgreSQL, "SELECT 1")
	if a == b {
		t.Fatal("dialect namespace missing")
	}
	if fp, err := Fingerprint(DialectSQLite, "SELECT 'secret"); err == nil || fp != "" {
		t.Fatal("malformed input acquired fingerprint")
	}
}

func TestTransportInputLimitRetainsOversizedLength(t *testing.T) {
	// No giant input allocation is necessary to validate failure metadata. A
	// producer may report the supplied length before accepting/scanning SQL.
	a := requireAnalysis(t, "", AnalysisOptions{})
	a.OriginalLength = MaxInputBytes + 100000
	a.Status = "limit"
	a.OmissionReason = "analysis_limit"
	for _, d := range DiagnosticCatalog() {
		if d.Code == "input_limit" {
			a.Diagnostics = []Diagnostic{d}
			break
		}
	}
	if len(a.Diagnostics) != 1 {
		t.Fatal("input_limit missing from catalog")
	}
	encoded, err := MarshalAnalysis(a)
	if err != nil {
		t.Fatal(err)
	}
	decoded, err := UnmarshalAnalysis(encoded)
	if err != nil || decoded.OriginalLength != a.OriginalLength {
		t.Fatalf("oversized failure metadata rejected: %v", err)
	}
}

func TestJSONUnicodePreservesByteMapBoundaries(t *testing.T) {
	for _, input := range []string{`"plain"`, `"\ud83d\ude00"`, `"escaped \\ud800"`, `"quote \""`} {
		if !validJSONUnicode([]byte(input)) {
			t.Fatalf("valid Unicode rejected: %s", input)
		}
	}
	for _, input := range []string{`"\ud800"`, `"\udc00"`, `"\ud800x"`, `"\ud800\u0000"`, "\"\xff\""} {
		if validJSONUnicode([]byte(input)) {
			t.Fatalf("invalid Unicode accepted: %q", input)
		}
	}
}

func TestAnalysisSourceMapsRedactAndFormat(t *testing.T) {
	query := "SELECT \"😀é\", 'top-secret' /* hidden value */ FROM records WHERE id=:private;"
	a := requireAnalysis(t, query, AnalysisOptions{Dialect: DialectSQLite, Format: true, KeepInLists: true})
	encoded, err := MarshalAnalysis(a)
	if err != nil {
		t.Fatal(err)
	}
	for _, secret := range []string{"top-secret", "hidden value", ":private"} {
		if strings.Contains(string(encoded), secret) {
			t.Fatalf("secret in analysis: %q", secret)
		}
	}
	start := strings.Index(query, "'top-secret'")
	end := start + len("'top-secret'")
	mapped, err := a.OriginalToSanitized.MapRange(start, end, false)
	if err != nil {
		t.Fatal(err)
	}
	if mapped.Exact || a.Text[mapped.Start:mapped.End] != "?" {
		t.Fatalf("literal not replaced: %#v, %q", mapped, a.Text)
	}
	original, err := a.OriginalToSanitized.MapRange(mapped.Start, mapped.End, true)
	if err != nil {
		t.Fatal(err)
	}
	if original.Exact || original.Start != start || original.End != end {
		t.Fatalf("replacement reverse provenance lost: %#v", original)
	}
	formatted, err := a.SanitizedToFormatted.MapRange(mapped.Start, mapped.End, false)
	if err != nil {
		t.Fatal(err)
	}
	if a.Formatted[formatted.Start:formatted.End] != "?" {
		t.Fatalf("formatted source map selected wrong text: %#v %q", formatted, a.Formatted)
	}
	commentStart := strings.Index(query, "/*")
	commentEnd := strings.Index(query, "*/") + 2
	deleted, err := a.OriginalToSanitized.MapRange(commentStart, commentEnd, false)
	if err != nil {
		t.Fatal(err)
	}
	if deleted.Exact || deleted.Start != deleted.End {
		t.Fatalf("comment was not represented as deletion: %#v", deleted)
	}
	if len(a.Statements) != 1 || a.Statements[0].EndOffset != len(query) {
		t.Fatalf("statement ranges missing: %#v", a.Statements)
	}
	if len(a.Binds) != 1 || a.Binds[0].Style != "named" {
		t.Fatalf("bind metadata missing: %#v", a.Binds)
	}
}

func TestBrowserOffsetsAstralCombiningCRLF(t *testing.T) {
	text := "a😀é\r\nz"
	cases := [][2]int{{0, 0}, {1, 1}, {5, 3}, {6, 4}, {8, 5}, {9, 6}, {10, 7}, {11, 8}}
	for _, p := range cases {
		units, err := UTF8ToUTF16(text, p[0])
		if err != nil || units != p[1] {
			t.Fatalf("UTF8 %d -> %d, %v", p[0], units, err)
		}
		bytes, err := UTF16ToUTF8(text, p[1])
		if err != nil || bytes != p[0] {
			t.Fatalf("UTF16 %d -> %d, %v", p[1], bytes, err)
		}
	}
	for _, offset := range []int{-1, 2, 3, 4, 7, 12} {
		if _, err := UTF8ToUTF16(text, offset); err == nil {
			t.Fatalf("accepted split UTF8 offset %d", offset)
		}
	}
	for _, offset := range []int{-1, 2, 9} {
		if _, err := UTF16ToUTF8(text, offset); err == nil {
			t.Fatalf("accepted split UTF16 offset %d", offset)
		}
	}
	if _, err := UTF8ToUTF16("a\xff", 1); err == nil {
		t.Fatal("accepted invalid UTF8 tail")
	}
}

func TestAnalysisMultipleDiagnosticsFailClosed(t *testing.T) {
	query := "SELECT ); SELECT ]; SELECT 'never publish"
	a := Analyze(query, AnalysisOptions{Dialect: DialectPostgreSQL, Format: true})
	if a.Status != "malformed" || a.OmissionReason != "malformed" || len(a.Diagnostics) < 3 {
		t.Fatalf("diagnostic recovery failed: %#v", a)
	}
	if a.Text != "" || a.Formatted != "" || a.Fingerprint != "" || len(a.Binds) != 0 || len(a.OriginalToSanitized.Segments) != 0 {
		t.Fatalf("failed analysis leaked transformed state: %#v", a)
	}
	for _, d := range a.Diagnostics {
		if err := ValidateDiagnostic(DiagnosticSchemaVersion, len(query), d); err != nil {
			t.Fatalf("invalid produced diagnostic: %#v %v", d, err)
		}
	}
	encoded, err := MarshalAnalysis(a)
	if err != nil {
		t.Fatal(err)
	}
	if strings.Contains(string(encoded), "never publish") {
		t.Fatal("diagnostic leaked malformed source")
	}
	tail := Analyze("SELECT 'unterminated; SELECT );", AnalysisOptions{Dialect: DialectSQLite})
	if len(tail.Diagnostics) != 1 || tail.Diagnostics[0].Code != "unterminated_string" {
		t.Fatalf("unsafe recovery through a quote: %#v", tail.Diagnostics)
	}
}

func TestAnalysisBudgetsCancellationAndPolicy(t *testing.T) {
	query := "SELECT 'private' FROM records WHERE id=123"
	for _, options := range []AnalysisOptions{
		{MaxInputBytes: 8}, {MaxOutputBytes: 8}, {MaxMemoryBytes: 8}, {MaxWorkUnits: 8},
	} {
		a := Analyze(query, options)
		if a.Status != "limit" || a.OmissionReason != "analysis_limit" || a.Text != "" || a.Fingerprint != "" {
			t.Fatalf("budget failed open: %#v", a)
		}
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	a := Analyze(query, AnalysisOptions{Context: ctx})
	if a.Status != "cancelled" || a.OmissionReason != "cancelled" || a.Text != "" {
		t.Fatalf("cancelled result: %#v", a)
	}
	a = requireAnalysis(t, query, AnalysisOptions{OmitQueryText: true})
	if a.Text != "" || a.OmissionReason != "policy" || a.Fingerprint == "" || a.Summary == "" {
		t.Fatalf("omission lost analysis metadata: %#v", a)
	}
	a = requireAnalysis(t, query, AnalysisOptions{OmitSourceMaps: true, OmitBindDescriptors: true})
	if len(a.OriginalToSanitized.Segments) != 0 || len(a.Binds) != 0 {
		t.Fatal("metadata opt-out failed")
	}
	if _, err := MarshalAnalysis(a); err != nil {
		t.Fatal(err)
	}
	a = requireAnalysis(t, query, AnalysisOptions{MaxQueryTextBytes: 9})
	if !a.Truncated || len(a.Text) > 9 || a.Fingerprint == "" {
		t.Fatalf("text truncation failed: %#v", a)
	}
	a = requireAnalysis(t, "", AnalysisOptions{})
	if a.Text != "" || a.OmissionReason != "empty" {
		t.Fatalf("empty reason: %#v", a)
	}
}

func TestBindArityWithoutValues(t *testing.T) {
	a := requireAnalysis(t, "SELECT :name+:name+:other; SELECT ?;", AnalysisOptions{Dialect: DialectSQLite})
	if len(a.Binds) != 4 || a.Binds[0].Slot != a.Binds[1].Slot || a.Binds[1].Slot == a.Binds[2].Slot || a.Binds[3].StatementIndex != 1 {
		t.Fatalf("bind identities: %#v", a.Binds)
	}
	if err := CheckBinds(a.Binds, 0, 2); err != nil {
		t.Fatal(err)
	}
	if err := CheckBinds(a.Binds, 0, 1); err == nil {
		t.Fatal("missing bind accepted")
	}
	if err := CheckBinds(a.Binds, 1, 1); err != nil {
		t.Fatal(err)
	}
	numbered := requireAnalysis(t, "SELECT $3+$3", AnalysisOptions{Dialect: DialectPostgreSQL})
	if numbered.Binds[0].Slot != 3 {
		t.Fatalf("numbered identity lost: %#v", numbered.Binds)
	}
	if err := CheckBinds(numbered.Binds, 0, 3); err != nil {
		t.Fatal(err)
	}
	if err := CheckBinds(numbered.Binds, 0, 1); err == nil {
		t.Fatal("numbered arity undercounted")
	}
	withEmptyStatements := requireAnalysis(t, "; ; SELECT ?; ; SELECT ?;", AnalysisOptions{Dialect: DialectSQLite})
	if withEmptyStatements.StatementCount != 2 || len(withEmptyStatements.Binds) != 2 ||
		withEmptyStatements.Binds[0].StatementIndex != 0 || withEmptyStatements.Binds[1].StatementIndex != 1 {
		t.Fatalf("empty statements shifted bind indexes: %#v", withEmptyStatements)
	}
	if _, err := MarshalAnalysis(withEmptyStatements); err != nil {
		t.Fatal(err)
	}
}

func TestDiagnosticCatalogAndVersionedTransport(t *testing.T) {
	catalog := DiagnosticCatalog()
	if len(catalog) < 19 {
		t.Fatal("catalog missing legacy codes")
	}
	seen := map[string]bool{}
	for _, d := range catalog {
		if seen[d.Code] || d.Message == "" || d.Explanation == "" || d.Hint == "" {
			t.Fatalf("invalid catalog entry: %#v", d)
		}
		seen[d.Code] = true
	}
	a := Analyze("SELECT )", AnalysisOptions{})
	d := a.Diagnostics[0]
	original := d.Message
	d.Message = "private incoming SQL"
	d.Explanation = "untrusted"
	d.Hint = "untrusted"
	clean, err := CanonicalizeDiagnostic(DiagnosticSchemaVersion, a.OriginalLength, d)
	if err != nil || clean.Message != original || strings.Contains(clean.Message, "private") {
		t.Fatalf("canonicalization: %#v %v", clean, err)
	}
	for _, mutate := range []func(*Diagnostic){
		func(d *Diagnostic) { d.Code = "future_code" }, func(d *Diagnostic) { d.Severity = "warning" },
		func(d *Diagnostic) { d.EndOffset = 999 }, func(d *Diagnostic) { d.Offset = -1 }, func(d *Diagnostic) { d.Line = 0 },
	} {
		bad := d
		mutate(&bad)
		if err := ValidateDiagnostic(DiagnosticSchemaVersion, a.OriginalLength, bad); err == nil {
			t.Fatalf("invalid diagnostic accepted: %#v", bad)
		}
	}
	if err := ValidateDiagnostic(2, a.OriginalLength, d); err == nil {
		t.Fatal("unknown catalog version accepted")
	}
	a.Diagnostics[0] = d
	encoded, err := MarshalAnalysis(a)
	if err != nil {
		t.Fatal(err)
	}
	if strings.Contains(string(encoded), "private incoming SQL") {
		t.Fatal("uncanonical prose serialized")
	}
	decoded, err := UnmarshalAnalysis(encoded)
	if err != nil {
		t.Fatal(err)
	}
	if decoded.Diagnostics[0].Message != original {
		t.Fatal("prose not canonical after decode")
	}
	var raw map[string]any
	if err := json.Unmarshal(encoded, &raw); err != nil {
		t.Fatal(err)
	}
	raw["schema_version"] = 2
	invalid, _ := json.Marshal(raw)
	if _, err := UnmarshalAnalysis(invalid); err == nil {
		t.Fatal("unknown analysis schema accepted")
	}
	raw["schema_version"] = 1
	raw["unexpected"] = true
	invalid, _ = json.Marshal(raw)
	if _, err := UnmarshalAnalysis(invalid); err == nil {
		t.Fatal("unknown transport field accepted")
	}
}

func TestSourceMapRejectsUntrustedStructure(t *testing.T) {
	for _, m := range []SourceMap{
		{FromLength: 2, ToLength: 2, Segments: []MapSegment{{0, 2, 0, 1, "copy"}}},
		{FromLength: 2, ToLength: 2, Segments: []MapSegment{{0, 1, 0, 1, "copy"}}},
		{FromLength: 2, ToLength: 2, Segments: []MapSegment{{0, 2, 0, 2, "future"}}},
	} {
		if _, err := m.MapRange(0, 1, false); err == nil {
			t.Fatalf("invalid map accepted: %#v", m)
		}
	}
}

func TestConcurrentAnalysisBudgetsAreIsolated(t *testing.T) {
	reference := requireAnalysis(t, "SELECT 42 FROM t", AnalysisOptions{}).Fingerprint
	var workers sync.WaitGroup
	for i := 0; i < 16; i++ {
		workers.Add(1)
		go func(i int) {
			defer workers.Done()
			for j := 0; j < 20; j++ {
				options := AnalysisOptions{}
				if i%2 == 0 {
					options.MaxWorkUnits = 1
				}
				result := Analyze("SELECT 43 FROM t", options)
				if i%2 == 0 {
					if result.Status != "limit" {
						t.Errorf("budget escaped goroutine: %s", result.Status)
					}
				} else if result.Status != "ok" || result.Fingerprint != reference {
					t.Errorf("isolated result changed: %s", result.Status)
				}
			}
		}(i)
	}
	workers.Wait()
	a := requireAnalysis(t, "SELECT 'a'", AnalysisOptions{})
	encoded, err := MarshalAnalysis(a)
	if err != nil {
		t.Fatal(err)
	}
	decoded, err := UnmarshalAnalysis(encoded)
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(a, decoded) {
		t.Fatalf("transport round trip changed result: %#v %#v", a, decoded)
	}
}

func TestAnalysisTransportRejectsMissingNullAndWrongTypes(t *testing.T) {
	valid := requireAnalysis(t, "SELECT :id FROM records WHERE value='private';", AnalysisOptions{Dialect: DialectSQLite, Format: true})
	failed := Analyze("SELECT )", AnalysisOptions{})
	tests := []struct {
		name   string
		failed bool
		mutate func(map[string]any)
	}{
		{"missing top field", false, func(w map[string]any) { delete(w, "original_length") }},
		{"null status", false, func(w map[string]any) { w["status"] = nil }},
		{"null length", false, func(w map[string]any) { w["original_length"] = nil }},
		{"fractional length", false, func(w map[string]any) { w["original_length"] = 1.5 }},
		{"string length", false, func(w map[string]any) { w["original_length"] = "45" }},
		{"boolean length", false, func(w map[string]any) { w["original_length"] = true }},
		{"null boolean", false, func(w map[string]any) { w["truncated"] = nil }},
		{"string boolean", false, func(w map[string]any) { w["truncated"] = "false" }},
		{"null diagnostics", false, func(w map[string]any) { w["diagnostics"] = nil }},
		{"null binds", false, func(w map[string]any) { w["binds"] = nil }},
		{"null statements", false, func(w map[string]any) { w["statements"] = nil }},
		{"null map", false, func(w map[string]any) { w["original_to_sanitized"] = nil }},
		{"null segments", false, func(w map[string]any) { w["original_to_sanitized"].(map[string]any)["segments"] = nil }},
		{"missing map field", false, func(w map[string]any) { delete(w["original_to_sanitized"].(map[string]any), "to_length") }},
		{"null segment offset", false, func(w map[string]any) {
			w["original_to_sanitized"].(map[string]any)["segments"].([]any)[0].(map[string]any)["from_start"] = nil
		}},
		{"missing segment field", false, func(w map[string]any) {
			delete(w["original_to_sanitized"].(map[string]any)["segments"].([]any)[0].(map[string]any), "from_end")
		}},
		{"unknown segment field", false, func(w map[string]any) {
			w["original_to_sanitized"].(map[string]any)["segments"].([]any)[0].(map[string]any)["extra"] = true
		}},
		{"null bind slot", false, func(w map[string]any) { w["binds"].([]any)[0].(map[string]any)["slot"] = nil }},
		{"wrong bind style", false, func(w map[string]any) { w["binds"].([]any)[0].(map[string]any)["style"] = false }},
		{"missing bind ordinal", false, func(w map[string]any) { delete(w["binds"].([]any)[0].(map[string]any), "ordinal") }},
		{"null statement offset", false, func(w map[string]any) { w["statements"].([]any)[0].(map[string]any)["offset"] = nil }},
		{"missing statement end", false, func(w map[string]any) { delete(w["statements"].([]any)[0].(map[string]any), "end_offset") }},
		{"null diagnostic severity", true, func(w map[string]any) { w["diagnostics"].([]any)[0].(map[string]any)["severity"] = nil }},
		{"null diagnostic boolean", true, func(w map[string]any) { w["diagnostics"].([]any)[0].(map[string]any)["incomplete"] = nil }},
		{"wrong diagnostic offset", true, func(w map[string]any) { w["diagnostics"].([]any)[0].(map[string]any)["offset"] = false }},
		{"missing diagnostic hint", true, func(w map[string]any) { delete(w["diagnostics"].([]any)[0].(map[string]any), "hint") }},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			original := valid
			if tt.failed {
				original = failed
			}
			encoded, err := MarshalAnalysis(original)
			if err != nil {
				t.Fatal(err)
			}
			var wire map[string]any
			if err := json.Unmarshal(encoded, &wire); err != nil {
				t.Fatal(err)
			}
			tt.mutate(wire)
			tampered, err := json.Marshal(wire)
			if err != nil {
				t.Fatal(err)
			}
			if _, err := UnmarshalAnalysis(tampered); err == nil {
				t.Fatal("invalid wire shape accepted")
			}
		})
	}
	encoded, err := MarshalAnalysis(valid)
	if err != nil {
		t.Fatal(err)
	}
	for _, field := range []string{`"schema_version":1`, `"from_start":0`} {
		duplicate := strings.Replace(string(encoded), field, field+","+field, 1)
		if duplicate == string(encoded) {
			t.Fatalf("duplicate fixture missing %s", field)
		}
		if _, err := UnmarshalAnalysis([]byte(duplicate)); err == nil {
			t.Fatalf("duplicate field accepted: %s", field)
		}
	}
}

func TestAnalysisDialectKeywordCase(t *testing.T) {
	for _, d := range []Dialect{DialectMySQL, DialectClickHouse} {
		a, e := Fingerprint(d, "SELECT fn(x) FROM t")
		if e != nil {
			t.Fatal(e)
		}
		b, e := Fingerprint(d, "select fn(x) from t")
		if e != nil || a != b {
			t.Fatal("keyword case", d, e)
		}
		b, e = Fingerprint(d, "SELECT Fn(x) FROM t")
		if e != nil || a == b {
			t.Fatal("function identity", d, e)
		}
	}
}
