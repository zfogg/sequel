#include <sequel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
  do {                                                                                                                 \
    if (!(x)) {                                                                                                        \
      fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                                  \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

int main(void) {
  CHECK(SEQUEL_INPUT_MAX == (size_t)1024 * 1024 * 1024);
  CHECK(SEQUEL_TEXT_MAX == SEQUEL_INPUT_MAX && SEQUEL_SUMMARY_MAX == SEQUEL_INPUT_MAX);
  /* A tiny query must not allocate the configured multi-gigabyte ceilings. */
  CHECK(sizeof(sequel_metadata_t) < 256);
  sequel_metadata_t result = {0};
  sequel_metadata_free(&result);
  sequel_metadata_options_t options = {0};
  size_t length = 1024 * 1024 + 100;
  char *sql = malloc(length + 1);
  CHECK(sql);
  memcpy(sql, "SELECT * FROM ", 14);
  memset(sql + 14, 'x', length - 14);
  sql[length] = 0;
  for (size_t limit = 0; limit <= SEQUEL_TEXT_MAX; limit += SEQUEL_TEXT_MAX) {
    options.max_query_text_bytes = limit;
    CHECK(sequel_metadata(sql, &options, &result) == SEQUEL_OK);
    CHECK(!result.truncated && strlen(result.text) == length);
    CHECK(strlen(result.summary) > 1024 * 1024 && !strcmp(result.operation, "SELECT"));
    sequel_metadata_free(&result);
    sequel_metadata_free(&result);
  }
  options.max_query_text_bytes = 31;
  CHECK(sequel_metadata(sql, &options, &result) == SEQUEL_OK);
  CHECK(result.truncated && strlen(result.text) == 31);
  CHECK(strlen(result.summary) > 1024 * 1024);
  sequel_metadata_free(&result);
  options.omit_query_text = true;
  CHECK(sequel_metadata(sql, &options, &result) == SEQUEL_OK);
  CHECK(!*result.text && !result.truncated && strlen(result.summary) > 1024 * 1024);
  sequel_metadata_free(&result);
  options.max_query_text_bytes = SEQUEL_TEXT_MAX + 1;
  CHECK(sequel_metadata("SELECT 1", &options, &result) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(!*result.text && !strcmp(result.summary, "SQL"));
  sequel_metadata_free(&result);
  CHECK(sequel_metadata("SELECT 'broken", NULL, &result) != SEQUEL_OK);
  {
    const char embedded[] = {'S', 'E', 'L', 0, 'E', 'C', 'T'};
    CHECK(sequel_metadata_n(embedded, sizeof(embedded), &options, &result) == SEQUEL_ERR_INVALID_ARGUMENT);
  }
  sequel_metadata_free(&result);
  char small[16];
  bool truncated;
  CHECK(sequel_sanitize("SELECT 123", SEQUEL_GENERIC, true, 0, small, sizeof(small), &truncated) == SEQUEL_OK);
  CHECK(!strcmp(small, "SELECT ?") && !truncated);
  CHECK(sequel_sanitize("SELECT 123", SEQUEL_GENERIC, true, SEQUEL_TEXT_MAX, small, 2, &truncated) ==
        SEQUEL_ERR_OVERFLOW);
  CHECK(!*small && !truncated);
  free(sql);
  /* More than 65536 tokens in a query larger than the former 1 MiB limit. */
  const size_t columns = 100000;
  length = 7 + columns * 12 + 20;
  sql = malloc(length);
  CHECK(sql);
  strcpy(sql, "SELECT ");
  for (size_t i = 0; i < columns; ++i)
    memcpy(sql + 7 + i * 12, "column_name, ", 12);
  strcpy(sql + 7 + columns * 12, "last FROM records;");
  CHECK(sequel_metadata(sql, NULL, &result) == SEQUEL_OK);
  CHECK(!result.truncated && !strcmp(result.text, sql));
  sequel_metadata_free(&result);
  sequel_diagnostic_t diagnostic;
  CHECK(sequel_diagnose(sql, SEQUEL_GENERIC, &diagnostic) == SEQUEL_OK);
  CHECK(sequel_format(sql, SEQUEL_GENERIC, NULL, 0, &length) == SEQUEL_OK);
  CHECK(sequel_highlight(sql, NULL, NULL, 0, &length) == SEQUEL_OK);
  char *compact = malloc(strlen(sql) + 1);
  CHECK(compact);
  CHECK(sequel_compact(sql, SEQUEL_GENERIC, compact, strlen(sql) + 1) == SEQUEL_OK);
  free(compact);
  free(sql);
  return 0;
}
