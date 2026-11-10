#include <sequel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                                               \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      fprintf(stderr, "%d: %s\n", __LINE__, #condition);                                                               \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

static int clean_failure(const sequel_analysis_t *a) {
  CHECK(!a->text[0] && !a->formatted[0] && !a->fingerprint[0]);
  CHECK(!a->bind_count && !a->binds);
  CHECK(!a->original_to_sanitized.count && !a->sanitized_to_formatted.count);
  CHECK(!a->original_to_sanitized.segments && !a->sanitized_to_formatted.segments);
  CHECK(a->diagnostic_count && a->diagnostics);
  for (size_t i = 0; i < a->diagnostic_count; ++i) {
    sequel_diagnostic_t d;
    CHECK(sequel_diagnostic_canonicalize(SEQUEL_DIAGNOSTIC_VERSION, a->original_length, &a->diagnostics[i], &d) ==
          SEQUEL_OK);
    CHECK(!strstr(d.message, "sensitive") && !strstr(d.explanation, "sensitive") && !strstr(d.hint, "sensitive"));
  }
  return 0;
}

static int fingerprints(void) {
  char base[96], other[96];
  CHECK(sequel_fingerprint("SELECT 1", SEQUEL_GENERIC, base) == SEQUEL_OK);
  /* Independently reconstructed with Python hashlib: frame = kind byte +
   * big-endian uint64 payload length + payload. Parameters are B + uint64
   * identity + Y(type) frame; V/D/W/B/Y frames contain no SQL values. */
  CHECK(!strcmp(base, "sequel:v1:generic:2c01bfb45c6e98658d756ef07e1cd3a4c2d82cb4c0d841c61fd4516786a19145"));
  CHECK(sequel_fingerprint("SELECT 1;", SEQUEL_GENERIC, other) == SEQUEL_OK);
  CHECK(!strcmp(other, "sequel:v1:generic:aaa55d1efdf56f571c85bb0452639e8e3c8ab31c12f0c3394059f71273ff08ef"));
  CHECK(sequel_fingerprint("select /* sensitive comment */ 'sensitive literal'", SEQUEL_GENERIC, other) == SEQUEL_OK);
  CHECK(!strcmp(base, other));
  CHECK(sequel_fingerprint("select 0.123e9", SEQUEL_GENERIC, other) == SEQUEL_OK && !strcmp(base, other));
  CHECK(sequel_fingerprint("select .123", SEQUEL_GENERIC, other) == SEQUEL_OK && !strcmp(base, other));
  CHECK(sequel_fingerprint("select TRUE", SEQUEL_GENERIC, other) == SEQUEL_OK && !strcmp(base, other));
  CHECK(sequel_fingerprint("select NULL", SEQUEL_GENERIC, other) == SEQUEL_OK && !strcmp(base, other));
  CHECK(sequel_fingerprint("select :renamed", SEQUEL_GENERIC, other) == SEQUEL_OK && !strcmp(base, other));
  CHECK(sequel_fingerprint("select $3", SEQUEL_GENERIC, other) == SEQUEL_OK && !strcmp(base, other));
  CHECK(sequel_fingerprint("select ?", SEQUEL_GENERIC, other) == SEQUEL_OK && !strcmp(base, other));
  CHECK(sequel_fingerprint("SELECT 42,42", SEQUEL_GENERIC, base) == SEQUEL_OK);
  CHECK(!strcmp(base, "sequel:v1:generic:c13c23330aafb2655f8893bd84283f9943a5a6e18779863c1d26ff631e40b439"));
  CHECK(sequel_fingerprint("SELECT :one,:two", SEQUEL_GENERIC, other) == SEQUEL_OK && !strcmp(base, other));
  CHECK(sequel_fingerprint("SELECT :one,42", SEQUEL_GENERIC, other) == SEQUEL_OK && !strcmp(base, other));
  CHECK(sequel_fingerprint("SELECT 42,:one", SEQUEL_GENERIC, other) == SEQUEL_OK && !strcmp(base, other));
  CHECK(sequel_fingerprint("SELECT :one,:one", SEQUEL_GENERIC, other) == SEQUEL_OK && strcmp(base, other));
  CHECK(sequel_fingerprint("SELECT 42,:one,:one", SEQUEL_GENERIC, base) == SEQUEL_OK);
  CHECK(sequel_fingerprint("SELECT :a,:b,:b", SEQUEL_GENERIC, other) == SEQUEL_OK && !strcmp(base, other));
  CHECK(sequel_fingerprint("SELECT :a,:b,:a", SEQUEL_GENERIC, other) == SEQUEL_OK && strcmp(base, other));
  CHECK(sequel_fingerprint("SELECT :one,:one", SEQUEL_GENERIC, base) == SEQUEL_OK);
  CHECK(!strcmp(base, "sequel:v1:generic:8a25dc329c188d61398bd98d10b87f61b20d8ea67a2e9882b21d3934407258b6"));
  CHECK(sequel_fingerprint("select :renamed, :renamed", SEQUEL_GENERIC, other) == SEQUEL_OK && !strcmp(base, other));
  CHECK(sequel_fingerprint("select $7, $7", SEQUEL_GENERIC, other) == SEQUEL_OK && !strcmp(base, other));
  CHECK(sequel_fingerprint("select :one, :two", SEQUEL_GENERIC, other) == SEQUEL_OK && strcmp(base, other));
  CHECK(sequel_fingerprint("select $1, $01", SEQUEL_GENERIC, other) == SEQUEL_OK && !strcmp(base, other));
  const char *different[][2] = {{"SELECT ab", "SELECT a b"},      {"SELECT \"a\"", "SELECT a"},
                                {"SELECT \"A\"", "SELECT \"a\""}, {"SELECT a>=b", "SELECT a>b"},
                                {"SELECT a+-b", "SELECT a-b"},    {"SELECT a::int", "SELECT a:int"},
                                {"SELECT :a,:a", "SELECT :a,:b"}, {"SELECT 1,2", "SELECT 1"}};
  for (size_t i = 0; i < sizeof(different) / sizeof(different[0]); ++i) {
    CHECK(sequel_fingerprint(different[i][0], SEQUEL_GENERIC, base) == SEQUEL_OK);
    CHECK(sequel_fingerprint(different[i][1], SEQUEL_GENERIC, other) == SEQUEL_OK && strcmp(base, other));
  }
  sequel_dialect_t case_sensitive[] = {SEQUEL_MYSQL, SEQUEL_CLICKHOUSE};
  for (size_t i = 0; i < sizeof(case_sensitive) / sizeof(case_sensitive[0]); ++i) {
    CHECK(sequel_fingerprint("SELECT X FROM T", case_sensitive[i], base) == SEQUEL_OK);
    CHECK(sequel_fingerprint("SELECT x FROM t", case_sensitive[i], other) == SEQUEL_OK && strcmp(base, other));
  }
  CHECK(sequel_fingerprint("SELECT 1", SEQUEL_SQLITE, base) == SEQUEL_OK);
  CHECK(sequel_fingerprint("SELECT 1", SEQUEL_POSTGRESQL, other) == SEQUEL_OK && strcmp(base, other));
  CHECK(sequel_fingerprint("SELECT $tag$sensitive$tag$", SEQUEL_POSTGRESQL, base) == SEQUEL_OK);
  CHECK(sequel_fingerprint(" select 'different' ", SEQUEL_POSTGRESQL, other) == SEQUEL_OK && !strcmp(base, other));
  CHECK(sequel_fingerprint("SELECT ?1,?1", SEQUEL_SQLITE, base) == SEQUEL_OK);
  CHECK(sequel_fingerprint("SELECT ?,?1", SEQUEL_SQLITE, other) == SEQUEL_OK && !strcmp(base, other));
  CHECK(sequel_fingerprint("SELECT \"sensitive", SEQUEL_GENERIC, other) != SEQUEL_OK && !other[0]);
  CHECK(sequel_fingerprint("SELECT );", SEQUEL_GENERIC, other) != SEQUEL_OK && !other[0]);
  CHECK(sequel_fingerprint("-- sensitive only", SEQUEL_GENERIC, other) == SEQUEL_OK && !other[0]);
  CHECK(sequel_fingerprint("", SEQUEL_GENERIC, NULL) == SEQUEL_ERR_NULL);
  return 0;
}

static int typed_arguments(void) {
  char a[96], b[96];
  CHECK(sequel_fingerprint("SELECT {p:Decimal(8,2)}", SEQUEL_CLICKHOUSE, a) == SEQUEL_OK);
  CHECK(sequel_fingerprint("SELECT {p:Decimal(9,2)}", SEQUEL_CLICKHOUSE, b) == SEQUEL_OK && strcmp(a, b));
  CHECK(sequel_fingerprint("SELECT {p:Enum('a'=1)}", SEQUEL_CLICKHOUSE, a) == SEQUEL_OK);
  CHECK(sequel_fingerprint("SELECT {p:Enum('b'=1)}", SEQUEL_CLICKHOUSE, b) == SEQUEL_OK && strcmp(a, b));
  CHECK(!strstr(a, "'a'") && !strstr(b, "'b'"));
  return 0;
}

static int maps_and_metadata(void) {
  {
    const char *ambiguous[] = {"SELECT $$sensitive$$", "SELECT 'a\\'sensitive\\'b'"};
    sequel_dialect_t dialects[] = {SEQUEL_MYSQL, SEQUEL_POSTGRESQL};
    for (size_t i = 0; i < 2; i++) {
      sequel_analysis_options_t policy = {0};
      policy.dialect = dialects[i];
      sequel_analysis_t rejected;
      CHECK(sequel_analyze(ambiguous[i], &policy, &rejected) == SEQUEL_ERR_INVALID_ARGUMENT);
      CHECK(clean_failure(&rejected) == 0);
      CHECK(rejected.diagnostics[0].code == SEQUEL_DIAG_AMBIGUOUS_LITERAL);
      sequel_analysis_free(&rejected);
    }
    sequel_analysis_options_t duck = {0};
    duck.dialect = SEQUEL_DUCKDB;
    sequel_analysis_t slice;
    CHECK(sequel_analyze("SELECT col[123:456] FROM t", &duck, &slice) == SEQUEL_OK);
    CHECK(strstr(slice.text, "[?:?]") && slice.bind_count == 0);
    sequel_analysis_free(&slice);
  }
  {
    sequel_analysis_options_t mysql = {0};
    mysql.dialect = SEQUEL_MYSQL;
    sequel_analysis_t variables;
    CHECK(sequel_analyze("SELECT @@version, @user_variable", &mysql, &variables) == SEQUEL_OK);
    CHECK(variables.bind_count == 0 && sequel_check_binds(NULL, 0, 0, 0) == SEQUEL_OK);
    CHECK(strstr(variables.text, "@@version") && strstr(variables.text, "@user_variable"));
    char fingerprint[96];
    CHECK(sequel_fingerprint("SELECT @@sql_mode, @user_variable", SEQUEL_MYSQL, fingerprint) == SEQUEL_OK);
    CHECK(strcmp(variables.fingerprint, fingerprint));
    sequel_analysis_free(&variables);
  }
  const char *sql = "  SELECT /* sensitive */ '\xf0\x9f\x99\x82sensitive', :secret_name FROM \"e\xcc\x81\";  ";
  sequel_analysis_options_t options = {0};
  options.dialect = SEQUEL_POSTGRESQL;
  options.format = true;
  sequel_analysis_t a;
  CHECK(sequel_analyze(sql, &options, &a) == SEQUEL_OK);
  CHECK(a.schema_version == SEQUEL_ANALYSIS_VERSION && a.original_length == strlen(sql));
  CHECK(!strcmp(a.status, "ok") && !a.omission_reason[0]);
  CHECK(!strcmp(a.text, "SELECT ? , ? FROM \"e\xcc\x81\";") || !strcmp(a.text, "SELECT ?, ? FROM \"e\xcc\x81\";"));
  CHECK(!strstr(a.text, "sensitive") && !strstr(a.formatted, "sensitive") && !strstr(a.summary, "sensitive"));
  CHECK(!strstr(a.text, "secret_name") && !strstr(a.fingerprint, "secret_name"));
  CHECK(!strcmp(a.operation, "SELECT") && a.statement_count == 1 && a.diagnostic_count == 0);
  CHECK(a.bind_count == 1 && a.binds[0].slot == 1 && a.binds[0].ordinal == 1 && a.binds[0].statement_index == 0);
  CHECK(a.statements[0].offset == 2 && a.statements[0].end_offset == strlen(sql) - 2);
  CHECK(a.original_to_sanitized.from_length == strlen(sql) && a.original_to_sanitized.to_length == strlen(a.text));
  CHECK(a.sanitized_to_formatted.from_length == strlen(a.text) &&
        a.sanitized_to_formatted.to_length == strlen(a.formatted));
  size_t literal = (size_t)(strchr(sql, '\'') - sql);
  sequel_mapped_range_t r, f;
  CHECK(sequel_source_map_range(&a.original_to_sanitized, literal + 1, literal + 5, false, &r) == SEQUEL_OK);
  CHECK(!r.exact && r.end == r.start + 1 && a.text[r.start] == '?');
  CHECK(sequel_source_map_range(&a.sanitized_to_formatted, r.start, r.end, false, &f) == SEQUEL_OK);
  CHECK(f.exact && f.end == f.start + 1 && a.formatted[f.start] == '?');
  size_t comment = (size_t)(strstr(sql, "/*") - sql);
  CHECK(sequel_source_map_range(&a.original_to_sanitized, comment, comment + 15, false, &r) == SEQUEL_OK && !r.exact &&
        r.start == r.end);
  CHECK(sequel_source_map_range(&a.original_to_sanitized, a.binds[0].offset, a.binds[0].end_offset, false, &r) ==
        SEQUEL_OK);
  CHECK(r.end == r.start + 1 && a.text[r.start] == '?' && !r.exact);
  CHECK(sequel_source_map_range(&a.original_to_sanitized, r.start, r.end, true, &f) == SEQUEL_OK);
  CHECK(f.start == a.binds[0].offset && f.end == a.binds[0].end_offset && !f.exact);
  for (size_t i = 0; i < a.original_to_sanitized.count; ++i) {
    const sequel_map_segment_t *s = &a.original_to_sanitized.segments[i];
    if (s->kind == SEQUEL_MAP_COPY)
      CHECK(!memcmp(sql + s->from_start, a.text + s->to_start, s->from_end - s->from_start));
  }
  char fingerprint[96];
  strcpy(fingerprint, a.fingerprint);
  sequel_analysis_free(&a);
  sequel_analysis_free(&a);
  sequel_analysis_free(NULL);
  options.omit_query_text = true;
  CHECK(sequel_analyze(sql, &options, &a) == SEQUEL_OK && !strcmp(a.omission_reason, "policy"));
  CHECK(!a.text[0] && !a.formatted[0] && !a.original_to_sanitized.count && !strcmp(fingerprint, a.fingerprint));
  CHECK(a.bind_count == 1 && a.statement_count == 1);
  sequel_analysis_free(&a);
  options.omit_query_text = false;
  options.omit_source_maps = true;
  options.omit_bind_descriptors = true;
  CHECK(sequel_analyze(sql, &options, &a) == SEQUEL_OK && !a.original_to_sanitized.count &&
        !a.sanitized_to_formatted.count && !a.bind_count);
  CHECK(a.text[0] && a.formatted[0] && !strcmp(fingerprint, a.fingerprint));
  sequel_analysis_free(&a);
  options = (sequel_analysis_options_t){0};
  options.max_query_text_bytes = 11;
  options.format = true;
  const char *unicode = "SELECT \"\xc3\xa9\xf0\x9f\x99\x82x\", 42";
  CHECK(sequel_analyze(unicode, &options, &a) == SEQUEL_OK && a.truncated);
  CHECK(!strcmp(a.text, "SELECT \"\xc3\xa9") && !a.formatted[0]);
  CHECK(a.original_to_sanitized.to_length == 10 && a.original_to_sanitized.from_length == strlen(unicode));
  CHECK(sequel_source_map_range(&a.original_to_sanitized, 10, 14, false, &r) == SEQUEL_OK && r.start == 10 &&
        r.end == 10 && !r.exact);
  CHECK(sequel_utf8_to_utf16(a.text, strlen(a.text), strlen(a.text), &literal) == SEQUEL_OK && literal == 9);
  sequel_analysis_free(&a);
  options = (sequel_analysis_options_t){0};
  CHECK(sequel_analyze("SELECT x IN (1,2,3)", &options, &a) == SEQUEL_OK);
  CHECK(strstr(a.text, "(?)") && a.original_to_sanitized.count);
  CHECK(sequel_source_map_range(&a.original_to_sanitized, 0, strlen("SELECT x IN (1,2,3)"), false, &r) == SEQUEL_OK);
  CHECK(r.start == 0 && r.end == strlen(a.text));
  sequel_analysis_free(&a);
  options.keep_in_lists = true;
  CHECK(sequel_analyze("SELECT x IN (1,2,3)", &options, &a) == SEQUEL_OK);
  CHECK(strstr(a.text, "(?,?,?)"));
  sequel_analysis_free(&a);
  return 0;
}

static int diagnostics(void) {
  const char *sql = "-- \xf0\x9f\x99\x82\r\nSELECT ); SELECT (1; SELECT ]; SELECT 'sensitive; SELECT );";
  sequel_analysis_options_t o = {0};
  o.dialect = SEQUEL_POSTGRESQL;
  sequel_analysis_t a;
  CHECK(sequel_analyze(sql, &o, &a) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(clean_failure(&a) == 0 && !strcmp(a.status, "malformed") && !strcmp(a.omission_reason, "malformed"));
  CHECK(a.diagnostic_count == 4);
  CHECK(a.diagnostics[0].code == SEQUEL_DIAG_UNMATCHED_CLOSE && a.diagnostics[0].line == 2 &&
        a.diagnostics[0].column == 8);
  CHECK(a.diagnostics[1].code == SEQUEL_DIAG_UNCLOSED_GROUP);
  CHECK(a.diagnostics[2].code == SEQUEL_DIAG_UNMATCHED_CLOSE);
  CHECK(a.diagnostics[3].code == SEQUEL_DIAG_UNTERMINATED_STRING && a.diagnostics[3].end_offset == strlen(sql));
  CHECK(a.statement_count == 4); /* The malformed final statement is counted once; quoted semicolons never split it. */
  sequel_analysis_free(&a);
  const char nul[] = {'S', 'E', 'L', 'E', 'C', 'T', ' ', 0, 's', 'e', 'n', 's', 'i', 't', 'i', 'v', 'e'};
  CHECK(sequel_analyze_n(nul, sizeof(nul), &o, &a) == SEQUEL_ERR_INVALID_ARGUMENT && clean_failure(&a) == 0);
  CHECK(a.original_length == sizeof(nul) && a.diagnostics[0].code == SEQUEL_DIAG_CONTROL_CHARACTER &&
        a.diagnostics[0].offset == 7);
  sequel_analysis_free(&a);
  CHECK(sequel_analyze("SELECT '\xf0\x9f", &o, &a) == SEQUEL_ERR_INVALID_ARGUMENT && clean_failure(&a) == 0);
  CHECK(a.diagnostics[0].code == SEQUEL_DIAG_INVALID_UTF8);
  sequel_analysis_free(&a);
  const char *malformed[] = {"SELECT 'sensitive", "SELECT 1 /* sensitive", "SELECT \"sensitive",
                             "SELECT $tag$sensitive; SELECT 1;"};
  for (size_t i = 0; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
    CHECK(sequel_analyze(malformed[i], &o, &a) == SEQUEL_ERR_INVALID_ARGUMENT && clean_failure(&a) == 0);
    CHECK(a.original_length == strlen(malformed[i]) && a.diagnostic_count == 1);
    sequel_analysis_free(&a);
  }
  CHECK(sequel_analyze(NULL, &o, &a) == SEQUEL_ERR_NULL && clean_failure(&a) == 0);
  sequel_analysis_free(&a);
  CHECK(sequel_analyze("SELECT 1", &o, NULL) == SEQUEL_ERR_NULL);
  o.dialect = (sequel_dialect_t)99;
  CHECK(sequel_analyze("SELECT 1", &o, &a) == SEQUEL_ERR_INVALID_ARGUMENT && clean_failure(&a) == 0);
  sequel_analysis_free(&a);
  return 0;
}

static int bind_tests(void) {
  sequel_analysis_options_t o = {0};
  o.dialect = SEQUEL_POSTGRESQL;
  sequel_analysis_t a;
  CHECK(sequel_analyze("SELECT 42,$1,$1,99,$2", &o, &a) == SEQUEL_OK);
  CHECK(a.bind_count == 3 && a.binds[0].slot == 1 && a.binds[1].slot == 1 && a.binds[2].slot == 2);
  CHECK(a.binds[0].ordinal == 1 && a.binds[2].ordinal == 3);
  CHECK(sequel_check_binds(a.binds, a.bind_count, 0, 2) == SEQUEL_OK);
  sequel_analysis_free(&a);
  CHECK(sequel_analyze("SELECT $3,$3; SELECT $1,$2", &o, &a) == SEQUEL_OK);
  CHECK(a.bind_count == 4 && a.statement_count == 2);
  CHECK(a.binds[0].slot == 3 && a.binds[1].slot == 3 && a.binds[2].slot == 1 && a.binds[3].slot == 2);
  CHECK(a.binds[0].ordinal == 1 && a.binds[3].ordinal == 4 && a.binds[3].statement_index == 1);
  CHECK(sequel_check_binds(a.binds, a.bind_count, 0, 3) == SEQUEL_OK);
  CHECK(sequel_check_binds(a.binds, a.bind_count, 0, 1) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_check_binds(a.binds, a.bind_count, 1, 2) == SEQUEL_OK);
  sequel_analysis_free(&a);
  CHECK(sequel_check_binds(NULL, 0, 0, 0) == SEQUEL_OK);
  CHECK(sequel_check_binds(NULL, 1, 0, 0) == SEQUEL_ERR_NULL);
  o.dialect = SEQUEL_SQLITE;
  CHECK(sequel_analyze("SELECT ?,?1,:x,:x,@x,$x", &o, &a) == SEQUEL_OK && a.bind_count == 6);
  CHECK(a.binds[0].slot == 1 && a.binds[1].slot == 1 && a.binds[2].slot == 2 && a.binds[3].slot == 2 &&
        a.binds[4].slot == 3 && a.binds[5].slot == 4);
  CHECK(sequel_check_binds(a.binds, a.bind_count, 0, 4) == SEQUEL_OK);
  sequel_analysis_free(&a);
  o.dialect = SEQUEL_MYSQL;
  CHECK(sequel_analyze("SELECT %s,%(name)s,%(name)s", &o, &a) == SEQUEL_OK && a.bind_count == 3);
  CHECK(a.binds[0].style == SEQUEL_BIND_POSITIONAL && a.binds[1].style == SEQUEL_BIND_NAMED &&
        a.binds[1].slot == a.binds[2].slot);
  sequel_analysis_free(&a);
  o.dialect = SEQUEL_POSTGRESQL;
  CHECK(sequel_analyze("SELECT a ? 'key', ARRAY[1,2]", &o, &a) == SEQUEL_OK && !a.bind_count && !strstr(a.text, "key"));
  sequel_analysis_free(&a);
  o.dialect = SEQUEL_CLICKHOUSE;
  CHECK(sequel_analyze("SELECT {sensitive:Array(Nullable(UInt64))}, {sensitive:Array(Nullable(UInt64))}", &o, &a) ==
            SEQUEL_OK &&
        a.bind_count == 2);
  CHECK(a.binds[0].style == SEQUEL_BIND_TYPED && a.binds[0].slot == a.binds[1].slot && !strstr(a.text, "sensitive"));
  char fingerprint[96];
  strcpy(fingerprint, a.fingerprint);
  sequel_analysis_free(&a);
  CHECK(sequel_analyze("SELECT {other:Array(Nullable(UInt64))}, {other:Array(Nullable(UInt64))}", &o, &a) ==
            SEQUEL_OK &&
        !strcmp(fingerprint, a.fingerprint));
  sequel_analysis_free(&a);
  CHECK(sequel_analyze("SELECT {other:Array(Nullable(Int64))}, {other:Array(Nullable(Int64))}", &o, &a) == SEQUEL_OK &&
        strcmp(fingerprint, a.fingerprint));
  sequel_analysis_free(&a);
  /* A parameter's type stream must be framed separately from the following
   * expression. The lexer deliberately does not validate database type grammar. */
  CHECK(sequel_fingerprint("SELECT {p:UInt64}", SEQUEL_CLICKHOUSE, fingerprint) == SEQUEL_OK);
  CHECK(!strcmp(fingerprint, "sequel:v1:clickhouse:097a81bd1abe0ac4c77a525a6d4e9d1972c08578864c3ba126a829da92d39ec0"));
  char lower_type[96];
  CHECK(sequel_fingerprint("SELECT {p:uint64}", SEQUEL_CLICKHOUSE, lower_type) == SEQUEL_OK &&
        strcmp(fingerprint, lower_type));
  CHECK(sequel_fingerprint("SELECT {p:Tuple(UInt64)}", SEQUEL_CLICKHOUSE, fingerprint) == SEQUEL_OK);
  char different[96];
  CHECK(sequel_fingerprint("SELECT {p:Tuple}(UInt64)", SEQUEL_CLICKHOUSE, different) == SEQUEL_OK);
  CHECK(strcmp(fingerprint, different));
  CHECK(sequel_fingerprint("SELECT {p:Tuple(UInt64)}, 1", SEQUEL_CLICKHOUSE, fingerprint) == SEQUEL_OK);
  CHECK(sequel_fingerprint("SELECT {p:Tuple}(UInt64), 1", SEQUEL_CLICKHOUSE, different) == SEQUEL_OK &&
        strcmp(fingerprint, different));
  CHECK(sequel_analyze("SELECT {sensitive:Array(UInt64)", &o, &a) == SEQUEL_ERR_INVALID_ARGUMENT &&
        clean_failure(&a) == 0);
  sequel_analysis_free(&a);
  CHECK(sequel_analyze("SELECT $0", NULL, &a) == SEQUEL_ERR_INVALID_ARGUMENT && clean_failure(&a) == 0);
  sequel_analysis_free(&a);
  return 0;
}

typedef struct {
  size_t calls, stop;
} cancellation;

static bool cancel_after(void *userdata) {
  cancellation *c = userdata;
  return ++c->calls >= c->stop;
}

static int resource_tests(void) {
  sequel_analysis_t a;
  sequel_analysis_options_t o = {0};
  const char *query = "SELECT 'sensitive', :sensitive";
  o.max_input_bytes = 4;
  CHECK(sequel_analyze_n(query, strlen(query), &o, &a) == SEQUEL_ERR_LIMIT && clean_failure(&a) == 0);
  CHECK(!strcmp(a.status, "limit") && !strcmp(a.omission_reason, "analysis_limit") &&
        a.original_length == strlen(query));
  sequel_analysis_free(&a);
  o = (sequel_analysis_options_t){0};
  o.max_output_bytes = 1;
  CHECK(sequel_analyze(query, &o, &a) == SEQUEL_ERR_LIMIT && clean_failure(&a) == 0);
  sequel_analysis_free(&a);
  o = (sequel_analysis_options_t){0};
  o.max_memory_bytes = 1;
  CHECK(sequel_analyze(query, &o, &a) == SEQUEL_ERR_LIMIT && clean_failure(&a) == 0);
  CHECK(a.memory_peak_bytes <= 1);
  sequel_analysis_free(&a);
  o = (sequel_analysis_options_t){0};
  o.max_work_units = 10;
  CHECK(sequel_analyze(query, &o, &a) == SEQUEL_ERR_LIMIT && clean_failure(&a) == 0 && a.work_units <= 10);
  sequel_analysis_free(&a);
  size_t size = 65536;
  char *long_quote = malloc(size + 10);
  CHECK(long_quote);
  memcpy(long_quote, "SELECT '", 8);
  memset(long_quote + 8, 'x', size);
  long_quote[size + 8] = '\'';
  long_quote[size + 9] = 0;
  cancellation state = {0, 2};
  o = (sequel_analysis_options_t){0};
  o.cancelled = cancel_after;
  o.cancel_userdata = &state;
  CHECK(sequel_analyze(long_quote, &o, &a) == SEQUEL_ERR_CANCELLED && clean_failure(&a) == 0);
  CHECK(state.calls == 2 && !strcmp(a.status, "cancelled") && !strcmp(a.omission_reason, "cancelled"));
  CHECK(a.work_units < size);
  sequel_analysis_free(&a);
  o.cancelled = NULL;
  o.cancel_userdata = NULL;
  o.max_work_units = 100;
  CHECK(sequel_analyze(long_quote, &o, &a) == SEQUEL_ERR_LIMIT && clean_failure(&a) == 0 && a.work_units <= 100);
  sequel_analysis_free(&a);
  free(long_quote);
  /* Cancelling one call cannot contaminate a subsequent default call. */
  CHECK(sequel_analyze(query, NULL, &a) == SEQUEL_OK && !strcmp(a.status, "ok"));
  CHECK(a.memory_peak_bytes > 0 && a.work_units > 0 && a.diagnostic_count == 0);
  sequel_analysis_free(&a);
  o = (sequel_analysis_options_t){0};
  o.max_input_bytes = SEQUEL_INPUT_MAX + 1;
  CHECK(sequel_analyze(query, &o, &a) == SEQUEL_ERR_INVALID_ARGUMENT && clean_failure(&a) == 0);
  sequel_analysis_free(&a);
  return 0;
}

static int linear_work_tests(void) {
  const char *statement = "SELECT $1;";
  size_t statement_length = strlen(statement), work[2];
  sequel_analysis_options_t o = {0};
  o.dialect = SEQUEL_POSTGRESQL;
  o.omit_source_maps = true;
  o.omit_bind_descriptors = true;
  o.max_memory_bytes = 8 * 1024 * 1024;
  for (size_t round = 0; round < 2; ++round) {
    size_t count = round ? 2000 : 1000, n = count * statement_length;
    char *query = malloc(n + 1);
    CHECK(query);
    for (size_t i = 0; i < count; ++i)
      memcpy(query + i * statement_length, statement, statement_length);
    query[n] = 0;
    sequel_analysis_t a;
    CHECK(sequel_analyze_n(query, n, &o, &a) == SEQUEL_OK);
    CHECK(a.statement_count == count && !a.bind_count && !a.original_to_sanitized.count);
    CHECK(a.memory_peak_bytes > 0 && a.memory_peak_bytes <= o.max_memory_bytes);
    work[round] = a.work_units;
    sequel_analysis_free(&a);
    free(query);
  }
  CHECK(work[1] > work[0] && work[1] < work[0] * 3);
  return 0;
}

int main(void) {
  int failed = 0;
  failed |= fingerprints();
  failed |= typed_arguments();
  {
    char a[96], b[96];
    CHECK(sequel_fingerprint("SELECT ?", SEQUEL_DUCKDB, a) == SEQUEL_OK);
    CHECK(sequel_fingerprint("SELECT 1", SEQUEL_DUCKDB, b) == SEQUEL_OK && !strcmp(a, b));
  }
  failed |= maps_and_metadata();
  failed |= diagnostics();
  failed |= bind_tests();
  failed |= resource_tests();
  failed |= linear_work_tests();
  return failed;
}
