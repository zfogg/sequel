#include <sequel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void hex(const char *s) {
  for (; *s; ++s)
    printf("%02x", (unsigned char)*s);
}
static int nibble(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}
int main(int argc, char **argv) {
  bool sanitize_only = argc == 2 && strcmp(argv[1], "--sanitize") == 0;
  bool format_only = argc == 2 && strcmp(argv[1], "--format") == 0;
  char *line = malloc(2097200), *query = malloc(1048600), *sanitized = malloc(1048600);
  if (!line || !query || !sanitized)
    return 2;
  while (fgets(line, 2097200, stdin)) {
    int dialect = 0, omit = 0, keep = 0;
    size_t limit = 0;
    int offset = 0;
    if (sscanf(line, "%d %zu %d %d %n", &dialect, &limit, &omit, &keep, &offset) != 4)
      return 2;
    size_t n = strcspn(line + offset, "\r\n");
    if (n % 2)
      return 2;
    for (size_t i = 0; i < n; i += 2) {
      int high = nibble(line[offset + i]), low = nibble(line[offset + i + 1]);
      if (high < 0 || low < 0)
        return 2;
      query[i / 2] = (char)(high * 16 + low);
    }
    query[n / 2] = 0;
    if (format_only) {
      size_t length = 0;
      sequel_status_t status = sequel_format(query, (sequel_dialect_t)dialect, NULL, 0, &length);
      size_t capacity = limit ? limit : length + 1;
      char *formatted = calloc(capacity, 1);
      if (!formatted)
        return 2;
      if (status == SEQUEL_OK)
        status = sequel_format(query, (sequel_dialect_t)dialect, formatted, capacity, &length);
      printf("%d %zu ", (int)status, length);
      hex(formatted);
      putchar('\n');
      free(formatted);
      continue;
    }
    if (sanitize_only) {
      bool truncated = false;
      size_t capacity = limit ? limit + 1 : SEQUEL_TEXT_MAX + 1;
      if (capacity > 1048600)
        return 2;
      sequel_status_t status =
          sequel_sanitize(query, (sequel_dialect_t)dialect, !keep, limit, sanitized, capacity, &truncated);
      printf("%d %d ", (int)status, (int)truncated);
      hex(sanitized);
      putchar('\n');
      continue;
    }
    sequel_metadata_t metadata;
    sequel_options_t options = {.dialect = (sequel_dialect_t)dialect,
                                   .max_query_text_bytes = limit,
                                   .omit_query_text = omit != 0,
                                   .keep_in_lists = keep != 0};
    sequel_status_t status = sequel_inspect(query, &options, &metadata);
    printf("%d %d %zu ", (int)status, (int)metadata.truncated, metadata.statement_count);
    hex(metadata.operation);
    putchar(' ');
    hex(metadata.text);
    putchar(' ');
    hex(metadata.summary);
    putchar('\n');
  }
  free(line);
  free(query);
  free(sanitized);
  return 0;
}
