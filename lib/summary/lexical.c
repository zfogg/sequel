#include "internal.h"

#include <ctype.h>
#include <string.h>

int sequel_summary_word(const char *source, size_t length, size_t offset, const char *word) {
  size_t n = strlen(word);
  if (offset + n > length)
    return 0;
  for (size_t i = 0; i < n; ++i)
    if (toupper((unsigned char)source[offset + i]) != toupper((unsigned char)word[i]))
      return 0;
  return (!offset || !isalnum((unsigned char)source[offset - 1])) &&
         (offset + n == length || !isalnum((unsigned char)source[offset + n]));
}

size_t sequel_summary_skip_space(const char *source, size_t length, size_t offset) {
  while (offset < length && isspace((unsigned char)source[offset]))
    ++offset;
  return offset;
}
