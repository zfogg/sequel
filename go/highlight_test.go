package sequel

import (
	"html"
	"os"
	"regexp"
	"strings"
	"testing"
)

var highlightTags = regexp.MustCompile(`<[^>]*>`)
var highlightSGR = regexp.MustCompile("\x1b\\[[0-9;]*m")

func TestHighlightSQL(t *testing.T) {
	query := "SELECT sum(value::decimal), :id, '<script>&', true, null FROM 日本語 -- comment\n"
	for _, mode := range []HighlightMode{HighlightHTML, HighlightANSI} {
		for _, appearance := range []Appearance{AppearanceAuto, AppearanceLight, AppearanceDark} {
			options := HighlightOptions{Mode: mode, Appearance: appearance, Colors: map[string]string{"variable": "#123000"}}
			got, err := HighlightSQL(DialectPostgreSQL, query, options)
			if err != nil {
				t.Fatal(err)
			}
			plain := highlightSGR.ReplaceAllString(got, "")
			if mode == HighlightHTML {
				plain = html.UnescapeString(highlightTags.ReplaceAllString(got, ""))
				if !strings.Contains(got, `color:#123000">:id`) || strings.Contains(got, "<script>") {
					t.Fatalf("HTML colors or escaping: %s", got)
				}
			} else if !strings.Contains(got, "\x1b[38;2;18;48;0m:id") || !strings.HasSuffix(got, "\x1b[0m") {
				t.Fatalf("ANSI colors or reset: %q", got)
			}
			if plain != query {
				t.Fatalf("changed input: %q", plain)
			}
		}
	}
}

func TestHighlightPalettesAndCustomYAML(t *testing.T) {
	names := Colorschemes()
	if len(names) < 300 {
		t.Fatalf("only %d schemes", len(names))
	}
	for i, name := range names {
		if i > 0 && name <= names[i-1] {
			t.Fatal("registry order")
		}
		if _, err := HighlightSQL(DialectGeneric, "select :id", HighlightOptions{Colorscheme: name}); err != nil {
			t.Fatal(name, err)
		}
	}
	yaml := "scheme: Custom\n"
	for _, key := range []string{"00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "0A", "0B", "0C", "0D", "0E", "0F"} {
		yaml += "base" + key + ": '#123000'\n"
	}
	got, err := HighlightSQL(DialectGeneric, "select :id", HighlightOptions{SchemeYAML: yaml})
	if err != nil || !strings.Contains(got, `background-color:#123000`) {
		t.Fatalf("custom palette: %s, %v", got, err)
	}
}

func TestHighlightInvalidOptions(t *testing.T) {
	for _, options := range []HighlightOptions{
		{Colorscheme: "monokai", Appearance: AppearanceLight},
		{Mode: "invalid"}, {Appearance: "invalid"}, {Colorscheme: "missing"}, {SchemeYAML: "missing"},
		{Colorscheme: "default-dark", SchemeYAML: "present"}, {Colorscheme: "default-dark\x00"},
		{Colors: map[string]string{"missing": "#123000"}}, {Colors: map[string]string{"variable": "red"}},
		{Colors: map[string]string{"variable\x00": "#123000"}}, {Colors: map[string]string{"variable": "123000\x00"}},
	} {
		if got, err := HighlightSQL(DialectGeneric, "SELECT 1", options); err == nil || got != "" {
			t.Fatalf("accepted invalid options: %+v", options)
		}
	}
	for _, query := range []string{"select 'unterminated", "select \x00 1", "select '\x1b[2J'", "\xff", strings.Repeat(" ", 1024*1024+1)} {
		if got, err := HighlightSQL(DialectGeneric, query, HighlightOptions{}); err == nil || got != "" {
			t.Fatal("accepted invalid input")
		}
	}
	if _, err := HighlightSQL("unsupported", "SELECT 1", HighlightOptions{}); err == nil {
		t.Fatal("invalid dialect")
	}
}

func TestHighlightPublishedVariants(t *testing.T) {
	for _, pair := range [][2]string{{"solarized-dark", "solarized-light"}, {"gruvbox-dark-hard", "gruvbox-light-hard"},
		{"catppuccin-mocha", "catppuccin-latte"}, {"atelier-cave", "atelier-cave-light"}, {"rose-pine", "rose-pine-dawn"}} {
		for _, mode := range []HighlightMode{HighlightHTML, HighlightANSI} {
			got, err := HighlightSQL(DialectGeneric, "SELECT :id", HighlightOptions{Mode: mode, Colorscheme: pair[0], Appearance: AppearanceLight})
			want, wantErr := HighlightSQL(DialectGeneric, "SELECT :id", HighlightOptions{Mode: mode, Colorscheme: pair[1]})
			if err != nil || wantErr != nil || got != want {
				t.Fatalf("%v: %v %v", pair, err, wantErr)
			}
		}
	}
}

func TestHighlightFormatPipelineSamples(t *testing.T) {
	sql, err := os.ReadFile("../examples/highlight/query.sql")
	if err != nil {
		t.Fatal(err)
	}
	formatted, err := FormatSQL(DialectPostgreSQL, string(sql))
	if err != nil {
		t.Fatal(err)
	}
	for _, appearance := range []Appearance{AppearanceDark, AppearanceLight} {
		for _, mode := range []HighlightMode{HighlightHTML, HighlightANSI} {
			got, err := HighlightSQL(DialectPostgreSQL, formatted, HighlightOptions{Mode: mode, Appearance: appearance})
			if err != nil {
				t.Fatal(err)
			}
			want, err := os.ReadFile("../examples/highlight/" + string(appearance) + "." + string(mode))
			if err != nil || got != string(want) {
				t.Fatalf("%s/%s sample mismatch: %v", appearance, mode, err)
			}
		}
	}
}

func FuzzHighlightSQL(f *testing.F) {
	for _, query := range []string{"", "SELECT :id, 'a<&>', 123 -- comment\n", "SELECT $$hello$$", "/* nested /* comment */ */", "select '\x1b[0m'", "select 日本語", "select (", "SELECT $tag$unfinished", "SELECT 'unfinished", "/* unfinished"} {
		f.Add(query)
	}
	f.Fuzz(func(t *testing.T, query string) {
		if len(query) > 16384 {
			return
		}
		for _, mode := range []HighlightMode{HighlightHTML, HighlightANSI} {
			for _, tolerant := range []bool{false, true} {
				got, err := HighlightSQL(DialectPostgreSQL, query, HighlightOptions{Mode: mode, Tolerant: tolerant})
				if err != nil {
					if got != "" {
						t.Fatal("error returned partial output")
					}
					continue
				}
				plain := highlightSGR.ReplaceAllString(got, "")
				if mode == HighlightHTML {
					plain = html.UnescapeString(highlightTags.ReplaceAllString(got, ""))
				}
				if plain != query {
					t.Fatalf("changed source: %q -> %q", query, plain)
				}
			}
		}
	})
}
