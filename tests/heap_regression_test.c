#include <sequel.h>

int main(void) {
  const struct {
    const char *sql;
    sequel_dialect_t dialect;
  } cases[] = {
      {"SELECT Y',Y/ (a:/\n-+.@\";,\nY'0\n\\?[3-+Z3\\.1Zb2:/Z[+", SEQUEL_POSTGRESQL},
      {"SELECT * FROM a SEMI JOIN b ON a.id=b.id ANTI JOIN c ON a.id=c.id", SEQUEL_DUCKDB},
      {"UPDATE users u JOIN flags f ON u.id=f.user_id SET u.active=1 WHERE f.secret='SECRET'", SEQUEL_MYSQL},
      {"SELECT true", SEQUEL_POSTGRESQL},
      {"SELECT false", SEQUEL_POSTGRESQL},
      {" (SELECT * FROM accounts WHERE token='secret' LIMIT 1) b ON a.id=b.id LIMIT $1", SEQUEL_GENERIC},
      {"SELECT * FROM a LIMIT 5, 10", SEQUEL_CLICKHOUSE},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    sequel_metadata_options_t options = {.dialect = cases[i].dialect};
    for (size_t repeat = 0; repeat < 16; ++repeat) {
      sequel_metadata_t metadata = {0};
      (void)sequel_metadata(cases[i].sql, &options, &metadata);
      sequel_metadata_free(&metadata);
    }
  }
  return 0;
}
