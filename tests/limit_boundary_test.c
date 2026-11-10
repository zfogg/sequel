#include <sequel.h>
#include "../src/input.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
  do {                                                                                                                 \
    if (!(x)) {                                                                                                        \
      fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                                  \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)
int main(int argc, char **argv) {
  CHECK(argc == 2);
  char sql[SEQUEL_INPUT_MAX + 2];
  memset(sql, ' ', SEQUEL_INPUT_MAX + 1);
  sql[SEQUEL_INPUT_MAX] = 0;
  sequel_diagnostic_t diagnostic;
  CHECK(sequel_diagnose(sql, SEQUEL_GENERIC, &diagnostic) == SEQUEL_OK);
  sql[SEQUEL_INPUT_MAX] = ' ';
  sql[SEQUEL_INPUT_MAX + 1] = 0;
  CHECK(sequel_diagnose_n(sql, SEQUEL_INPUT_MAX + 1, SEQUEL_GENERIC, &diagnostic) == SEQUEL_ERR_OVERFLOW);
  CHECK(diagnostic.code == SEQUEL_DIAG_INPUT_LIMIT && diagnostic.offset == SEQUEL_INPUT_MAX);
  size_t length;
  CHECK(sequel_format_ex(sql, SEQUEL_GENERIC, NULL, 0, &length, &diagnostic) == SEQUEL_ERR_OVERFLOW);
  CHECK(diagnostic.code == SEQUEL_DIAG_INPUT_LIMIT);
  CHECK(sequel_highlight_ex(sql, NULL, NULL, 0, &length, &diagnostic) == SEQUEL_ERR_OVERFLOW);
  CHECK(diagnostic.code == SEQUEL_DIAG_INPUT_LIMIT);
  sequel_metadata_t result;
  CHECK(sequel_inspect(sql, NULL, &result) == SEQUEL_ERR_OVERFLOW);
  sequel_metadata_free(&result);
  char out[256];
  bool truncated;
  CHECK(sequel_sanitize(sql, SEQUEL_GENERIC, true, 0, out, sizeof(out), &truncated) == SEQUEL_ERR_OVERFLOW);
  CHECK(!*out && !truncated);
  FILE *stream = fopen(argv[1], "w+b");
  CHECK(stream);
  CHECK(fwrite(sql, 1, SEQUEL_INPUT_MAX + 1, stream) == SEQUEL_INPUT_MAX + 1);
  rewind(stream);
  char *read = input_read(stream, &length);
  CHECK(read && length == SEQUEL_INPUT_MAX + 1 && read[length] == 0);
  free(read);
  fclose(stream);
  CHECK(remove(argv[1]) == 0);
  memcpy(sql, "SELECT * FROM ", 14);
  memset(sql + 14, 'x', 80);
  sql[94] = 0;
  CHECK(sequel_inspect(sql, NULL, &result) == SEQUEL_OK);
  CHECK(result.truncated && strlen(result.text) <= SEQUEL_TEXT_MAX);
  CHECK(strlen(result.summary) <= SEQUEL_SUMMARY_MAX);
  CHECK(!strcmp(result.summary + strlen(result.summary) - 3, "…"));
  sequel_metadata_free(&result);
  return 0;
}
