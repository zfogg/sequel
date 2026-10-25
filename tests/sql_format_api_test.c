#include <sequel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr)) {                                                                                                     \
      fprintf(stderr, "failed line %d: %s\n", __LINE__, #expr);                                                        \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)
int main(void) {
  const char *sql = "SELECT a,b FROM t WHERE id=$1; -- trailing  ";
  char out[256] = "old";
  size_t length = 999;
  CHECK(sequel_format(NULL, SEQUEL_POSTGRESQL, out, sizeof(out), &length) == SEQUEL_ERR_NULL);
  CHECK(!*out);
  CHECK(sequel_format(sql, SEQUEL_POSTGRESQL, out, sizeof(out), NULL) == SEQUEL_ERR_NULL);
  CHECK(sequel_format(sql, (sequel_dialect_t)-1, out, sizeof(out), &length) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_format(sql, SEQUEL_POSTGRESQL, NULL, 1, &length) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_format(sql, SEQUEL_POSTGRESQL, out, 0, &length) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_format(sql, SEQUEL_POSTGRESQL, NULL, 0, &length) == SEQUEL_OK);
  size_t required = length;
  CHECK(required > strlen(sql));
  CHECK(sequel_format(sql, SEQUEL_POSTGRESQL, out, required, &length) == SEQUEL_ERR_OVERFLOW);
  CHECK(!*out && length == required);
  CHECK(sequel_format(sql, SEQUEL_POSTGRESQL, out, required + 1, &length) == SEQUEL_OK);
  CHECK(length == strlen(out) && length == required);
  CHECK(strstr(out, "$1") && strstr(out, "-- trailing  "));
  CHECK(sequel_format("SELECT \xff", SEQUEL_POSTGRESQL, out, sizeof(out), &length) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(!*out);
  CHECK(sequel_format("", SEQUEL_POSTGRESQL, out, 1, &length) == SEQUEL_OK && length == 0 && !*out);
  char nested[512] = "SELECT ";
  size_t used = strlen(nested);
  for (int i = 0; i < 40; ++i) {
    memcpy(nested + used, "(SELECT ", 8);
    used += 8;
  }
  nested[used++] = '1';
  for (int i = 0; i < 40; ++i)
    nested[used++] = ')';
  nested[used] = 0;
  CHECK(sequel_format(nested, SEQUEL_POSTGRESQL, NULL, 0, &length) == SEQUEL_OK);
  CHECK(length > strlen(nested) * 8 + 1024);
  required = length;
  CHECK(sequel_format(nested, SEQUEL_POSTGRESQL, out, sizeof(out), &length) == SEQUEL_ERR_OVERFLOW);
  CHECK(!*out && length == required);
  char *expanded = malloc(required + 1);
  CHECK(expanded);
  CHECK(sequel_format(nested, SEQUEL_POSTGRESQL, expanded, required + 1, &length) == SEQUEL_OK);
  CHECK(strlen(expanded) == required);
  CHECK(sequel_format(expanded, SEQUEL_POSTGRESQL, NULL, 0, &length) == SEQUEL_OK);
  CHECK(length == required);
  free(expanded);
  return 0;
}
