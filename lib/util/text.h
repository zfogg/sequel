#pragma once

#include <stddef.h>
#include <string.h>
#include <ctype.h>

static inline const char *sequel_text_find(const char *source, size_t length, const char *needle) {
  if (!source || !needle)
    return NULL;
  const size_t needle_length = strlen(needle);
  if (!needle_length || needle_length > length)
    return NULL;
  for (size_t offset = 0; offset + needle_length <= length; ++offset)
    if (memcmp(source + offset, needle, needle_length) == 0)
      return source + offset;
  return NULL;
}

static inline int sequel_text_equal_ci(const char *source, size_t length, const char *expected) {
  const size_t expected_length = strlen(expected);
  if (!source || !expected || length != expected_length)
    return 0;
  for (size_t i = 0; i < length; ++i)
    if (tolower((unsigned char)source[i]) != tolower((unsigned char)expected[i]))
      return 0;
  return 1;
}
