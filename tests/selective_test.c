#include <sequel.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void) {
  const char *sql = "/* private */ SELECT 'secret', 42, $1 -- private\nFROM t";
  const char *want[] = {
    "/* private */ SELECT 'secret', 42, $1 -- private\nFROM t",
    "SELECT 'secret', 42, $1 FROM t",
    "/* private */ SELECT ?, ?, ? -- private\nFROM t",
    "SELECT ?, ?, ? FROM t"
  };
  char out[256]; bool truncated;
  for (unsigned flags = 0; flags < 4; ++flags) {
    CHECK(sequel_sanitize_with_flags(sql, SEQUEL_POSTGRESQL, flags, true, 255, out, sizeof(out), &truncated) == SEQUEL_OK);
    CHECK(!truncated && !strcmp(out, want[flags]));
    CHECK(sequel_sanitize_with_flags("SELECT 'broken", SEQUEL_POSTGRESQL, flags, true, 255, out, sizeof(out), &truncated) != SEQUEL_OK);
    CHECK(!*out);
    CHECK(sequel_sanitize_with_flags("/* nested /* private */ done */SELECT 1", SEQUEL_POSTGRESQL, flags, true, 5, out, sizeof(out), &truncated) == SEQUEL_OK);
    CHECK(truncated && strlen(out) <= 5);
  }
  CHECK(sequel_sanitize_with_flags(sql, SEQUEL_POSTGRESQL, 4, true, 255, out, sizeof(out), &truncated) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_sanitize_with_flags("SELECT 1-- comment\n+2", SEQUEL_POSTGRESQL, SEQUEL_REPLACE_LITERALS, true, 255, out, sizeof(out), &truncated) == SEQUEL_OK);
  CHECK(!strcmp(out, "SELECT ?-- comment\n+?"));
  return 0;
}
