package sequel

import (
	"testing"
)

// TestSummaryRowBounds checks scope, static counts and noninterpolation independently.
func TestSummaryJoinKinds(t *testing.T) {
	for _, join := range []string{"JOIN", "INNER JOIN", "LEFT JOIN", "LEFT OUTER JOIN", "RIGHT JOIN", "FULL OUTER JOIN", "CROSS JOIN", "NATURAL JOIN", "NATURAL LEFT JOIN", "SEMI JOIN", "ANTI JOIN", "ASOF LEFT JOIN", "POSITIONAL JOIN", "JOIN LATERAL", "LEFT JOIN LATERAL", "CROSS JOIN LATERAL"} {
		sql := "SELECT * FROM accounts a " + join + " (SELECT * FROM accounts WHERE token='secret' LIMIT 1) b ON a.id=b.id LIMIT $1"
		want := "SELECT … FROM accounts AS a " + join + " (SELECT … FROM accounts WHERE … LIMIT 1) AS b ON a.id=b.id LIMIT ?"
		if got := Summary(DialectDuckDB, sql); got != want {
			t.Errorf("%s: got %q; want %q", join, got, want)
		}
	}
}

// TestSummaryJoinConditions verifies predicates, aliases and nested scopes
// independently of the corpus snapshot, including private literal replacement.
func TestSummaryJoinConditions(t *testing.T) {
	cases := []struct{ sql, want string }{
		{"SELECT * FROM a x LEFT JOIN b y ON x.id = y.a_id AND y.secret = 'sentinel' AND y.n >= 987654 AND y.f < .8765 JOIN c z USING (id, tenant_id) LIMIT 5", "SELECT … FROM a AS x LEFT JOIN b AS y ON x.id = y.a_id AND y.secret = ? AND y.n >= ? AND y.f < ? JOIN c AS z USING (id, tenant_id) LIMIT 5"},
		{"SELECT * FROM a JOIN b ON CASE WHEN b.n > 123 THEN a.id ELSE b.id END = a.id LEFT JOIN c ON c.id IS NOT DISTINCT FROM b.id", "SELECT … FROM a JOIN b ON CASE WHEN b.n > ? THEN a.id ELSE b.id END = a.id LEFT JOIN c ON c.id IS NOT DISTINCT FROM b.id"},
		{"SELECT * FROM a JOIN b ON b.id IN (SELECT id FROM c JOIN d USING(id) LIMIT 1) AND b.ts >= now() - interval 'secret'", "SELECT … FROM a JOIN b ON b.id IN (SELECT … FROM c JOIN d USING (id) LIMIT 1) AND b.ts >= now() - interval ?"},
		{"SELECT * FROM a JOIN b ON coalesce(a.id, 444) = b.id, c", "SELECT … FROM a JOIN b ON coalesce(a.id, ?) = b.id, c"},
		{"SELECT * FROM a AS \"left\" JOIN b AS \"right\" ON \"left\".\"key\"=\"right\".\"key\" AND b.v=1.23e45::numeric", "SELECT … FROM a AS \"left\" JOIN b AS \"right\" ON \"left\".\"key\"=\"right\".\"key\" AND b.v=?::numeric"},
		{"SELECT * FROM a JOIN b ON (b.id=a.id AND (b.x BETWEEN -987 AND +654 OR b.v=$1))", "SELECT … FROM a JOIN b ON (b.id=a.id AND (b.x BETWEEN -? AND +? OR b.v=?))"},
		{"SELECT * FROM a JOIN LATERAL (SELECT * FROM b LIMIT $1) AS recent ON true UNION ALL SELECT * FROM c JOIN d USING(id)", "SELECT … FROM a JOIN LATERAL (SELECT … FROM b LIMIT ?) AS recent ON ? UNION ALL SELECT … FROM c JOIN d USING (id)"},
	}
	for _, dialect := range []Dialect{DialectPostgreSQL, DialectDuckDB, DialectSQLite, DialectGeneric} {
		for _, tc := range cases {
			t.Run(string(dialect)+"/"+tc.sql, func(t *testing.T) {
				if got := Summary(dialect, tc.sql); got != tc.want {
					t.Fatalf("got %q; want %q", got, tc.want)
				}
			})
		}
	}
	query := "SELECT * FROM a JOIN b ON b.secret=$$sentinel$$ AND b.token=E'secret' AND b.number=0xDEAD AND b.flag=true /* hidden */"
	if got := Summary(DialectPostgreSQL, query); got != "SELECT … FROM a JOIN b ON b.secret=? AND b.token=? AND b.number=? AND b.flag=?" {
		t.Fatal(got)
	}
	if got := Summary(DialectMySQL, `SELECT * FROM a JOIN b ON b.secret="sentinel" AND b.token=? AND b.n=987`); got != "SELECT … FROM a JOIN b ON b.secret=? AND b.token=? AND b.n=?" {
		t.Fatal(got)
	}
	for _, dialect := range []Dialect{DialectPostgreSQL, DialectDuckDB} {
		if got := Summary(dialect, `SELECT * FROM unnest($1::text[]) WITH ORDINALITY AS x(id, n) JOIN unnest($2::text[]) WITH ORDINALITY AS y(id, n) USING(n)`); got != "SELECT … FROM unnest(…) WITH ORDINALITY AS x(id, n) JOIN unnest(…) WITH ORDINALITY AS y(id, n) USING (n)" {
			t.Fatalf("%s: %s", dialect, got)
		}
		query := `SELECT * FROM a JOIN b ON b.tags = ARRAY['secret', 'private'] AND b.ids[123] = 456`
		if got := Summary(dialect, query); got != "SELECT … FROM a JOIN b ON b.tags = ARRAY[?, ?] AND b.ids[?] = ?" {
			t.Fatalf("%s: %s", dialect, got)
		}
		query = `INSERT INTO a SELECT unnest(CASE WHEN cardinality($1::text[]) = 0 THEN ARRAY[cardinality($2::text[])] ELSE $1 END) ON CONFLICT(id) DO NOTHING`
		if got := Summary(dialect, query); got != "INSERT INTO a SELECT unnest(…) ON CONFLICT DO NOTHING" {
			t.Fatalf("%s: %s", dialect, got)
		}
	}
}

// TestJoinSummaryMetadata checks exported attributes without changing span identity
// or allowing query-text truncation/omission to erase the structural summary.
