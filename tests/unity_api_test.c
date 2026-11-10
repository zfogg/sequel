/* Exercise the cgo unity layout and an independently linked layout with the
 * same public calls. SQL's private macros must not alter subsequent sources. */
#ifdef SEQUEL_TEST_UNITY
#include "../lib/engine.c"

#if defined(malloc) || defined(calloc) || defined(realloc) || defined(free) || defined(while)
#error SQL resource macros escaped the SQL implementation
#endif
#if defined(INPUT_MAX) || defined(sequel_checkpoint)
#error SQL private configuration macros escaped the SQL implementation
#endif

#include "../lib/util/utf8.c"
#include "../third_party/utf8proc/utf8proc.c"
#else
#include <sequel.h>
#include "../third_party/utf8proc/utf8proc.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  /* utf8proc's public ABI promises that the caller can use libc free. */
  utf8proc_uint8_t *mapped = NULL;
  const utf8proc_uint8_t input[] = "Cafe\xcc\x81";
  utf8proc_ssize_t length = utf8proc_map(input, 0, &mapped, UTF8PROC_NULLTERM | UTF8PROC_COMPOSE | UTF8PROC_STABLE);
  if (length != 5 || !mapped || strcmp((const char *)mapped, "Caf\xc3\xa9")) {
    free(mapped);
    return 1;
  }
  free(mapped);

  const char sql[] = "SELECT \"e\xcc\x81\xf0\x9f\x98\x80\" FROM records WHERE value='\xf0\x9f\x98\x80';";
  sequel_analysis_options_t options = {0};
  options.dialect = SEQUEL_POSTGRESQL;
  options.omit_source_maps = true;
  options.omit_bind_descriptors = true;
  sequel_analysis_t analysis = {0};
  if (sequel_analyze_n(sql, sizeof(sql) - 1, &options, &analysis) != SEQUEL_OK) {
    sequel_analysis_free(&analysis);
    return 2;
  }
  /* The companion CTest compares this count across both layouts. */
  printf("%zu\n", analysis.work_units);
  sequel_analysis_free(&analysis);
  return 0;
}
