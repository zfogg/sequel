package sequel

import "testing"

func TestSelectiveSanitize(t *testing.T) {
	for _, tc := range []struct {
		comments, literals bool
		want               string
	}{
		{true, true, "SELECT ?, ? FROM t WHERE id=?"},
		{true, false, "SELECT 'secret', 42 FROM t WHERE id=$1"},
		{false, true, "/* private */ SELECT ?, ? FROM t WHERE id=? -- private\n"},
		{false, false, "/* private */ SELECT 'secret', 42 FROM t WHERE id=$1 -- private\n"},
	} {
		got, err := SanitizeSelective(DialectPostgreSQL, "/* private */ SELECT 'secret', 42 FROM t WHERE id=$1 -- private\n", tc.comments, tc.literals)
		want := tc.want
		if err != nil || got != want {
			t.Fatalf("%v/%v: %q, %v; want %q", tc.comments, tc.literals, got, err, want)
		}
	}
}
