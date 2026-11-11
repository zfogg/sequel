#pragma once

#include <stddef.h>
#include <string.h>

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
