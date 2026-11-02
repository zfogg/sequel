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
  sequel_options_t options = {0};
  size_t length = 1024 * 1024 + 100;
  char *sql = malloc(length + 1);
  CHECK(sql);
  memcpy(sql, "SELECT * FROM ", 14);
  memset(sql + 14, 'x', length - 14);
  sql[length] = 0;
  for (size_t limit = 0; limit <= SEQUEL_TEXT_MAX; limit += SEQUEL_TEXT_MAX) {
    options.max_query_text_bytes = limit;
    CHECK(sequel_inspect(sql, &options, &result) == SEQUEL_OK);
    CHECK(!result.truncated && strlen(result.text) == length);
    CHECK(strlen(result.summary) > 1024 * 1024 && !strcmp(result.operation, "SELECT"));
    sequel_metadata_free(&result);
    sequel_metadata_free(&result);
  }
  options.max_query_text_bytes = 31;
  CHECK(sequel_inspect(sql, &options, &result) == SEQUEL_OK);
  CHECK(result.truncated && strlen(result.text) == 31);
  CHECK(strlen(result.summary) > 1024 * 1024);
  sequel_metadata_free(&result);
  options.omit_query_text = true;
  CHECK(sequel_inspect(sql, &options, &result) == SEQUEL_OK);
  CHECK(!*result.text && !result.truncated && strlen(result.summary) > 1024 * 1024);
  sequel_metadata_free(&result);
  options.max_query_text_bytes = SEQUEL_TEXT_MAX + 1;
  CHECK(sequel_inspect("SELECT 1", &options, &result) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(!*result.text && !strcmp(result.summary, "SQL"));
  sequel_metadata_free(&result);
  CHECK(sequel_inspect("SELECT 'broken", NULL, &result) != SEQUEL_OK);
  sequel_metadata_free(&result);
  char small[16];
  bool truncated;
  CHECK(sequel_sanitize("SELECT 123", SEQUEL_GENERIC, true, 0, small, sizeof(small), &truncated) == SEQUEL_OK);
  CHECK(!strcmp(small, "SELECT ?") && !truncated);
  CHECK(sequel_sanitize("SELECT 123", SEQUEL_GENERIC, true, SEQUEL_TEXT_MAX, small, 2, &truncated) ==
        SEQUEL_ERR_OVERFLOW);
  CHECK(!*small && !truncated);
  free(sql);
  return 0;
}
