#include <sequel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                                                                                                                               \
  do {                                                                                                                                                                                                 \
    if (!(condition)) {                                                                                                                                                                                \
      fprintf(stderr, "%d: %s\n", __LINE__, #condition);                                                                                                                                               \
      return 1;                                                                                                                                                                                        \
    }                                                                                                                                                                                                  \
  } while (0)

static int catalog_tests(void) {
  sequel_diagnostic_t d, canonical;
  size_t count = sequel_diagnostic_catalog_count();
  CHECK(count > SEQUEL_DIAG_OUT_OF_MEMORY);
  CHECK(sequel_diagnostic_catalog_get(0, NULL) == SEQUEL_ERR_NULL);
  CHECK(sequel_diagnostic_catalog_get(count, &d) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(d.code == SEQUEL_DIAG_NONE && !d.message[0]);
  for (size_t i = 0; i < count; ++i) {
    CHECK(sequel_diagnostic_catalog_get(i, &d) == SEQUEL_OK);
    CHECK((size_t)d.code == i && sequel_diagnostic_code_name(d.code));
    CHECK(d.message[0] && d.explanation[0] && d.hint[0]);
    CHECK(d.line == 1 && d.column == 1 && d.end_line == 1 && d.end_column == 1);
    CHECK(sequel_diagnostic_canonicalize(SEQUEL_DIAGNOSTIC_VERSION, 0, &d, &canonical) == SEQUEL_OK);
    CHECK(!strcmp(d.message, canonical.message));
  }
  CHECK(sequel_diagnose("SELECT )", SEQUEL_GENERIC, &d) == SEQUEL_ERR_INVALID_ARGUMENT);
  memset(d.message, 's', sizeof(d.message));
  memset(d.explanation, 's', sizeof(d.explanation));
  memset(d.hint, 's', sizeof(d.hint));
  CHECK(sequel_diagnostic_canonicalize(1, 8, &d, &canonical) == SEQUEL_OK);
  CHECK(canonical.code == SEQUEL_DIAG_UNMATCHED_CLOSE && canonical.offset == 7 && canonical.end_offset == 8);
  CHECK(!strcmp(canonical.message, "Unexpected closing delimiter"));
  CHECK(canonical.line == 1 && canonical.column == 8 && canonical.end_column == 9);
  CHECK(sequel_diagnostic_canonicalize(1, 8, &d, &d) == SEQUEL_OK);
  CHECK(!strcmp(d.message, canonical.message));
  CHECK(sequel_diagnostic_canonicalize(2, 8, &d, &canonical) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(!canonical.message[0]);
  CHECK(sequel_diagnostic_canonicalize(1, 8, NULL, &canonical) == SEQUEL_ERR_NULL);
  CHECK(sequel_diagnostic_canonicalize(1, 8, &d, NULL) == SEQUEL_ERR_NULL);
  sequel_diagnostic_t bad = d;
  bad.code = (sequel_diagnostic_code_t)count;
  CHECK(sequel_diagnostic_canonicalize(1, 8, &bad, &canonical) == SEQUEL_ERR_INVALID_ARGUMENT);
  bad = d;
  bad.code = (sequel_diagnostic_code_t)-1;
  CHECK(sequel_diagnostic_canonicalize(1, 8, &bad, &canonical) == SEQUEL_ERR_INVALID_ARGUMENT);
  bad = d;
  bad.severity = SEQUEL_WARNING;
  CHECK(sequel_diagnostic_canonicalize(1, 8, &bad, &canonical) == SEQUEL_ERR_INVALID_ARGUMENT);
  bad = d;
  bad.incomplete = true;
  CHECK(sequel_diagnostic_canonicalize(1, 8, &bad, &canonical) == SEQUEL_ERR_INVALID_ARGUMENT);
  bad = d;
  bad.statement_complete = true;
  CHECK(sequel_diagnostic_canonicalize(1, 8, &bad, &canonical) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_diagnostic_canonicalize(1, 7, &d, &canonical) == SEQUEL_ERR_INVALID_ARGUMENT);
  bad = d;
  bad.end_offset = 6;
  CHECK(sequel_diagnostic_canonicalize(1, 8, &bad, &canonical) == SEQUEL_ERR_INVALID_ARGUMENT);
  bad = d;
  bad.line = 0;
  CHECK(sequel_diagnostic_canonicalize(1, 8, &bad, &canonical) == SEQUEL_ERR_INVALID_ARGUMENT);
  bad = d;
  bad.column = 9;
  CHECK(sequel_diagnostic_canonicalize(1, 8, &bad, &canonical) == SEQUEL_ERR_INVALID_ARGUMENT);
  bad = d;
  bad.end_line = 0;
  CHECK(sequel_diagnostic_canonicalize(1, 8, &bad, &canonical) == SEQUEL_ERR_INVALID_ARGUMENT);
  bad = d;
  bad.end_column = 0;
  CHECK(sequel_diagnostic_canonicalize(1, 8, &bad, &canonical) == SEQUEL_ERR_INVALID_ARGUMENT);
  bad = d;
  bad.line = 2;
  bad.end_line = 1;
  CHECK(sequel_diagnostic_canonicalize(1, 8, &bad, &canonical) == SEQUEL_ERR_INVALID_ARGUMENT);
  bad = d;
  bad.end_offset = bad.offset;
  CHECK(sequel_diagnostic_canonicalize(1, 8, &bad, &canonical) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_diagnose("-- \xf0\x9f\x99\x82\r\nSELECT e\xcc\x81, )", SEQUEL_POSTGRESQL, &d) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(d.line == 2 && d.column == 12);
  CHECK(sequel_diagnostic_canonicalize(1, d.end_offset, &d, &canonical) == SEQUEL_OK);
  CHECK(canonical.line == 2 && canonical.column == 12);
  return 0;
}

static int utf_tests(void) {
  const char *text = "A\xf0\x9f\x99\x82"
                     "e\xcc\x81\r\nZ";
  size_t bytes[] = {0, 1, 5, 6, 8, 9, 10, 11};
  size_t units[] = {0, 1, 3, 4, 5, 6, 7, 8};
  CHECK(strlen(text) == 11);
  for (size_t i = 0; i < sizeof(bytes) / sizeof(bytes[0]); ++i) {
    size_t result = SIZE_MAX;
    CHECK(sequel_utf8_to_utf16(text, 11, bytes[i], &result) == SEQUEL_OK && result == units[i]);
    CHECK(sequel_utf16_to_utf8(text, 11, units[i], &result) == SEQUEL_OK && result == bytes[i]);
  }
  size_t out;
  CHECK(sequel_utf8_to_utf16(text, 11, 2, &out) == SEQUEL_ERR_INVALID_ARGUMENT && out == 0);
  CHECK(sequel_utf8_to_utf16(text, 11, 3, &out) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_utf8_to_utf16(text, 11, 4, &out) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_utf8_to_utf16(text, 11, 7, &out) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_utf16_to_utf8(text, 11, 2, &out) == SEQUEL_ERR_INVALID_ARGUMENT && out == 0);
  CHECK(sequel_utf8_to_utf16(text, 11, 12, &out) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_utf16_to_utf8(text, 11, 9, &out) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_utf8_to_utf16("", 0, 0, &out) == SEQUEL_OK && out == 0);
  CHECK(sequel_utf16_to_utf8("", 0, 0, &out) == SEQUEL_OK && out == 0);
  CHECK(sequel_utf8_to_utf16(NULL, 0, 0, &out) == SEQUEL_ERR_NULL);
  CHECK(sequel_utf16_to_utf8(NULL, 0, 0, &out) == SEQUEL_ERR_NULL);
  CHECK(sequel_utf8_to_utf16("", 0, 0, NULL) == SEQUEL_ERR_NULL);
  CHECK(sequel_utf16_to_utf8("", 0, 0, NULL) == SEQUEL_ERR_NULL);
  const char *invalid[] = {"\xc0\xaf", "\xe0\x80\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xf0\x9f", "A\xff"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    CHECK(sequel_utf8_to_utf16(invalid[i], strlen(invalid[i]), 0, &out) == SEQUEL_ERR_INVALID_ARGUMENT);
    CHECK(sequel_utf16_to_utf8(invalid[i], strlen(invalid[i]), 0, &out) == SEQUEL_ERR_INVALID_ARGUMENT);
  }
  CHECK(sequel_utf8_to_utf16("", SEQUEL_INPUT_MAX + 1, 0, &out) == SEQUEL_ERR_OVERFLOW);
  CHECK(sequel_utf16_to_utf8("", SEQUEL_INPUT_MAX + 1, 0, &out) == SEQUEL_ERR_OVERFLOW);
  return 0;
}

static int map_tests(void) {
  sequel_map_segment_t segments[] = {{0, 2, 0, 2, SEQUEL_MAP_COPY}, {2, 8, 2, 3, SEQUEL_MAP_REPLACE}, {8, 13, 3, 3, SEQUEL_MAP_DELETE}, {13, 15, 3, 5, SEQUEL_MAP_COPY}};
  sequel_source_map_t map = {15, 5, segments, 4};
  sequel_mapped_range_t r;
  CHECK(sequel_source_map_range(&map, 3, 4, false, &r) == SEQUEL_OK && r.start == 2 && r.end == 3 && !r.exact);
  CHECK(sequel_source_map_range(&map, 8, 13, false, &r) == SEQUEL_OK && r.start == 3 && r.end == 3 && !r.exact);
  CHECK(sequel_source_map_range(&map, 2, 3, true, &r) == SEQUEL_OK && r.start == 2 && r.end == 8 && !r.exact);
  CHECK(sequel_source_map_range(&map, 1, 14, false, &r) == SEQUEL_OK && r.start == 1 && r.end == 4 && !r.exact);
  CHECK(sequel_source_map_range(&map, 13, 15, false, &r) == SEQUEL_OK && r.start == 3 && r.end == 5 && r.exact);
  CHECK(sequel_source_map_range(&map, 3, 3, true, &r) == SEQUEL_OK && r.start == 8 && r.end == 13 && !r.exact);
  CHECK(sequel_source_map_range(&map, 2, 2, false, &r) == SEQUEL_OK && r.start == 2 && r.end == 2 && r.exact);
  CHECK(sequel_source_map_range(&map, 4, 4, false, &r) == SEQUEL_OK && r.start == 2 && r.end == 3 && !r.exact);
  CHECK(sequel_source_map_range(&map, 15, 15, false, &r) == SEQUEL_OK && r.start == 5 && r.end == 5 && r.exact);
  CHECK(sequel_source_map_range(&map, 0, 16, false, &r) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_source_map_range(&map, 0, 6, true, &r) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_source_map_range(&map, 2, 1, false, &r) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_source_map_range(&map, 0, SIZE_MAX, false, &r) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_source_map_range(NULL, 0, 0, false, &r) == SEQUEL_ERR_NULL);
  CHECK(sequel_source_map_range(&map, 0, 0, false, NULL) == SEQUEL_ERR_NULL);
  sequel_map_segment_t layout[] = {{0, 2, 0, 2, SEQUEL_MAP_COPY}, {2, 2, 2, 5, SEQUEL_MAP_INSERT}, {2, 5, 5, 8, SEQUEL_MAP_COPY}};
  map = (sequel_source_map_t){5, 8, layout, 3};
  CHECK(sequel_source_map_range(&map, 2, 3, false, &r) == SEQUEL_OK && r.start == 5 && r.end == 6 && r.exact);
  CHECK(sequel_source_map_range(&map, 2, 2, false, &r) == SEQUEL_OK && r.start == 2 && r.end == 5 && !r.exact);
  CHECK(sequel_source_map_range(&map, 3, 4, true, &r) == SEQUEL_OK && r.start == 2 && r.end == 2 && !r.exact);
  CHECK(sequel_source_map_range(&map, 2, 6, true, &r) == SEQUEL_OK && r.start == 2 && r.end == 3 && !r.exact);
  CHECK(sequel_source_map_range(&map, 0, 5, false, &r) == SEQUEL_OK && r.start == 0 && r.end == 8 && !r.exact);
  sequel_map_segment_t copy = {0, 11, 0, 11, SEQUEL_MAP_COPY};
  map = (sequel_source_map_t){11, 11, &copy, 1};
  for (size_t i = 0; i <= 11; ++i)
    for (size_t j = i; j <= 11; ++j) {
      CHECK(sequel_source_map_range(&map, i, j, false, &r) == SEQUEL_OK && r.start == i && r.end == j && r.exact);
      CHECK(sequel_source_map_range(&map, i, j, true, &r) == SEQUEL_OK && r.start == i && r.end == j && r.exact);
    }
  sequel_map_segment_t deleted = {0, 6, 0, 0, SEQUEL_MAP_DELETE};
  map = (sequel_source_map_t){6, 0, &deleted, 1};
  CHECK(sequel_source_map_range(&map, 0, 6, false, &r) == SEQUEL_OK && r.start == 0 && r.end == 0 && !r.exact);
  CHECK(sequel_source_map_range(&map, 0, 0, true, &r) == SEQUEL_OK && r.start == 0 && r.end == 6 && !r.exact);
  map = (sequel_source_map_t){0};
  CHECK(sequel_source_map_range(&map, 0, 0, false, &r) == SEQUEL_OK && r.start == 0 && r.end == 0 && r.exact);
  CHECK(sequel_source_map_range(&map, 0, 0, true, &r) == SEQUEL_OK && r.exact);
  map.from_length = 1;
  CHECK(sequel_source_map_range(&map, 0, 0, false, &r) == SEQUEL_ERR_INVALID_ARGUMENT);
  map = (sequel_source_map_t){15, 5, NULL, 4};
  CHECK(sequel_source_map_range(&map, 0, 0, false, &r) == SEQUEL_ERR_INVALID_ARGUMENT);
  map = (sequel_source_map_t){15, 5, segments, 4};
  segments[1].from_start = 3;
  CHECK(sequel_source_map_range(&map, 0, 0, false, &r) == SEQUEL_ERR_INVALID_ARGUMENT);
  segments[1].from_start = 2;
  segments[1].kind = SEQUEL_MAP_COPY;
  CHECK(sequel_source_map_range(&map, 0, 0, false, &r) == SEQUEL_ERR_INVALID_ARGUMENT);
  segments[1].kind = (sequel_map_kind_t)99;
  CHECK(sequel_source_map_range(&map, 0, 0, false, &r) == SEQUEL_ERR_INVALID_ARGUMENT);
  segments[1].kind = SEQUEL_MAP_REPLACE;
  segments[2].to_end = 4;
  CHECK(sequel_source_map_range(&map, 0, 0, false, &r) == SEQUEL_ERR_INVALID_ARGUMENT);
  return 0;
}

int main(void) {
  CHECK(catalog_tests() == 0);
  CHECK(utf_tests() == 0);
  CHECK(map_tests() == 0);
  return 0;
}
