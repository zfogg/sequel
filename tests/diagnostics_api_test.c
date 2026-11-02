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

int main(void) {
  sequel_diagnostic_t d;
  CHECK(sequel_diagnose("", SEQUEL_GENERIC, NULL) == SEQUEL_ERR_NULL);
  CHECK(sequel_diagnose(NULL, SEQUEL_GENERIC, &d) == SEQUEL_ERR_NULL);
  CHECK(d.code == SEQUEL_DIAG_NULL_INPUT && d.line == 1 && d.column == 1);
  CHECK(sequel_diagnose("", (sequel_dialect_t)255, &d) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(d.code == SEQUEL_DIAG_INVALID_ARGUMENT);
  CHECK(!sequel_diagnostic_code_name((sequel_diagnostic_code_t)255));
  for (int i = SEQUEL_DIAG_NONE; i <= SEQUEL_DIAG_OUT_OF_MEMORY; ++i)
    CHECK(sequel_diagnostic_code_name((sequel_diagnostic_code_t)i));
  const char raw[] = {'S', 'E', 'L', 'E', 'C', 'T', ' ', '1', ';'};
  CHECK(sequel_diagnose_n(raw, sizeof(raw), SEQUEL_GENERIC, &d) == SEQUEL_OK && d.statement_complete);
  const char nul[] = {'x', 0, 'y'};
  CHECK(sequel_diagnose_n(nul, sizeof(nul), SEQUEL_GENERIC, &d) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(d.code == SEQUEL_DIAG_CONTROL_CHARACTER && d.offset == 1 && d.end_offset == 2);
  CHECK(sequel_diagnose("\xc0\xaf", SEQUEL_GENERIC, &d) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(d.code == SEQUEL_DIAG_INVALID_UTF8 && d.offset == 0);
  CHECK(sequel_diagnose("\xf0\x9f", SEQUEL_GENERIC, &d) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(d.code == SEQUEL_DIAG_INVALID_UTF8);
  CHECK(sequel_diagnose("SELECT '\033[2J'", SEQUEL_GENERIC, &d) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(d.code == SEQUEL_DIAG_CONTROL_CHARACTER && d.offset == 8);
  /* UTF-8 bytes differ from displayed scalar columns; CRLF is one newline. */
  const char *unicode = "-- \xf0\x9f\x99\x82\r\nSELECT \xc3\xa9, 'secret";
  CHECK(sequel_diagnose(unicode, SEQUEL_POSTGRESQL, &d) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(d.code == SEQUEL_DIAG_UNTERMINATED_STRING && d.incomplete && d.severity == SEQUEL_WARNING);
  CHECK(d.offset == 20 && d.line == 2 && d.column == 11);
  CHECK(d.end_offset == strlen(unicode) && d.end_line == 2 && d.end_column == 18);
  CHECK(*d.explanation && *d.hint && !strstr(d.message, "secret") && !strstr(d.explanation, "secret"));
  CHECK(sequel_diagnose("\t\r'", SEQUEL_GENERIC, &d) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(d.line == 2 && d.column == 1);

  struct {
    const char *sql;
    sequel_dialect_t dialect;
    sequel_diagnostic_code_t code;
  } cases[] = {
      {"SELECT 'abc", SEQUEL_GENERIC, SEQUEL_DIAG_UNTERMINATED_STRING},
      {"SELECT E'abc\\'", SEQUEL_POSTGRESQL, SEQUEL_DIAG_UNTERMINATED_STRING},
      {"SELECT U&'abc", SEQUEL_POSTGRESQL, SEQUEL_DIAG_UNTERMINATED_STRING},
      {"SELECT $tag$abc", SEQUEL_POSTGRESQL, SEQUEL_DIAG_UNTERMINATED_STRING},
      {"SELECT $$abc", SEQUEL_DUCKDB, SEQUEL_DIAG_UNTERMINATED_STRING},
      {"SELECT _utf8'abc", SEQUEL_MYSQL, SEQUEL_DIAG_UNTERMINATED_STRING},
      {"SELECT \"abc", SEQUEL_MYSQL, SEQUEL_DIAG_UNTERMINATED_STRING},
      {"SELECT \"abc", SEQUEL_POSTGRESQL, SEQUEL_DIAG_UNTERMINATED_IDENTIFIER},
      {"SELECT `abc", SEQUEL_MYSQL, SEQUEL_DIAG_UNTERMINATED_IDENTIFIER},
      {"SELECT [abc", SEQUEL_SQLITE, SEQUEL_DIAG_UNTERMINATED_IDENTIFIER},
      {"/* outer /* inner */", SEQUEL_POSTGRESQL, SEQUEL_DIAG_UNTERMINATED_COMMENT},
      {"SELECT {id:UInt64", SEQUEL_CLICKHOUSE, SEQUEL_DIAG_UNTERMINATED_PARAMETER},
      {"SELECT $name::suffix(open", SEQUEL_SQLITE, SEQUEL_DIAG_UNTERMINATED_PARAMETER},
  };
  sequel_highlight_options_t options;
  sequel_highlight_options_init(&options);
  CHECK(!options.tolerant);
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    const char *sql = cases[i].sql;
    CHECK(sequel_diagnose(sql, cases[i].dialect, &d) == SEQUEL_ERR_INVALID_ARGUMENT);
    CHECK(d.code == cases[i].code && d.incomplete && !d.statement_complete);
    options.dialect = cases[i].dialect;
    for (int mode = SEQUEL_HTML; mode <= SEQUEL_ANSI; ++mode) {
      options.mode = (sequel_highlight_mode_t)mode;
      options.tolerant = false;
      size_t length;
      CHECK(sequel_highlight_ex(sql, &options, NULL, 0, &length, &d) == SEQUEL_ERR_INVALID_ARGUMENT);
      CHECK(d.code == cases[i].code && !length);
      options.tolerant = true;
      CHECK(sequel_highlight_ex(sql, &options, NULL, 0, &length, &d) == SEQUEL_OK);
      CHECK(d.code == cases[i].code && d.incomplete && length > strlen(sql));
      char *out = malloc(length + 1);
      CHECK(out);
      CHECK(sequel_highlight_ex(sql, &options, out, length + 1, &length, &d) == SEQUEL_OK);
      CHECK(strlen(out) == length && d.code == cases[i].code);
      free(out);
    }
  }
  CHECK(sequel_diagnose("SELECT {bad}", SEQUEL_CLICKHOUSE, &d) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(d.code == SEQUEL_DIAG_INVALID_PARAMETER && !d.incomplete);
  options.dialect = SEQUEL_CLICKHOUSE;
  size_t length;
  CHECK(sequel_highlight_ex("SELECT {bad}", &options, NULL, 0, &length, &d) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_diagnose("INSERT INTO t FORMAT CSV\nx,y", SEQUEL_CLICKHOUSE, &d) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(d.code == SEQUEL_DIAG_UNSUPPORTED_PAYLOAD && d.offset == 14 && d.end_offset == 20);
  CHECK(sequel_diagnose("SELECT (1]", SEQUEL_POSTGRESQL, &d) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(d.code == SEQUEL_DIAG_UNMATCHED_CLOSE && d.offset == 9 && !d.incomplete);
  CHECK(sequel_diagnose("SELECT (1", SEQUEL_POSTGRESQL, &d) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(d.code == SEQUEL_DIAG_UNCLOSED_GROUP && d.offset == 7 && d.incomplete);
  CHECK(sequel_diagnose("SELECT ';' /* ; */", SEQUEL_GENERIC, &d) == SEQUEL_OK && !d.statement_complete);
  CHECK(sequel_diagnose("SELECT ';'; -- ignored\n /* ; */", SEQUEL_GENERIC, &d) == SEQUEL_OK && d.statement_complete);
  CHECK(sequel_diagnose("SELECT ';'; SELECT 2", SEQUEL_GENERIC, &d) == SEQUEL_OK && !d.statement_complete);
  CHECK(sequel_diagnose("SELECT 1+2;", SEQUEL_GENERIC, &d) == SEQUEL_OK && d.statement_complete);
  char small[4] = "xxx";
  CHECK(sequel_format_ex("SELECT 'bad", SEQUEL_GENERIC, small, sizeof(small), &length, &d) ==
        SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(!*small && !length && d.code == SEQUEL_DIAG_UNTERMINATED_STRING);
  CHECK(sequel_format_ex("SELECT 1", SEQUEL_GENERIC, small, sizeof(small), &length, &d) == SEQUEL_ERR_OVERFLOW);
  CHECK(!*small && length > 4 && d.code == SEQUEL_DIAG_OUTPUT_LIMIT);
  CHECK(sequel_format_ex("SELECT 1", SEQUEL_GENERIC, NULL, 1, &length, &d) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(d.code == SEQUEL_DIAG_INVALID_ARGUMENT);
  CHECK(sequel_format_ex("SELECT CASE WHEN 1 THEN 2", SEQUEL_GENERIC, NULL, 0, &length, &d) ==
        SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(d.code == SEQUEL_DIAG_UNSUPPORTED_LAYOUT);
  options.dialect = SEQUEL_GENERIC;
  CHECK(sequel_highlight_ex("'bad", &options, small, sizeof(small), &length, &d) == SEQUEL_ERR_OVERFLOW);
  CHECK(!*small && d.code == SEQUEL_DIAG_OUTPUT_LIMIT);
  CHECK(sequel_highlight_ex("\033", &options, NULL, 0, &length, &d) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(d.code == SEQUEL_DIAG_CONTROL_CHARACTER);
  char *large = malloc(1024 * 1024 + 2);
  CHECK(large);
  memset(large, ' ', 1024 * 1024 + 1);
  large[1024 * 1024 + 1] = 0;
  CHECK(sequel_diagnose(large, SEQUEL_GENERIC, &d) == SEQUEL_ERR_OVERFLOW);
  CHECK(d.code == SEQUEL_DIAG_INPUT_LIMIT && d.offset == 1024 * 1024);
  for (size_t i = 0; i < 65537; ++i) {
    large[i * 2] = 'x';
    large[i * 2 + 1] = ' ';
  }
  large[65537 * 2] = 0;
  CHECK(sequel_diagnose(large, SEQUEL_GENERIC, &d) == SEQUEL_ERR_OVERFLOW);
  CHECK(d.code == SEQUEL_DIAG_TOKEN_LIMIT && d.offset == 65536 * 2);
  memset(large, '(', 65);
  large[65] = 0;
  CHECK(sequel_diagnose(large, SEQUEL_GENERIC, &d) == SEQUEL_ERR_OVERFLOW);
  CHECK(d.code == SEQUEL_DIAG_NESTING_LIMIT && d.offset == 64);
  for (size_t i = 0; i < 65; ++i) {
    large[i * 2] = '/';
    large[i * 2 + 1] = '*';
  }
  large[130] = 0;
  CHECK(sequel_diagnose(large, SEQUEL_GENERIC, &d) == SEQUEL_ERR_OVERFLOW);
  CHECK(d.code == SEQUEL_DIAG_NESTING_LIMIT && !d.incomplete);
  free(large);
  return 0;
}
