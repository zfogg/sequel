#include "internal.h"

#include <ctype.h>
#include <string.h>

char *sequel_summary_find_text(const char *source, const char *needle) {
  if (!source || !needle || !*needle)
    return source;
  size_t needle_length = strlen(needle);
  for (const char *cursor = source; *cursor; ++cursor) {
    size_t i = 0;
    while (i < needle_length && cursor[i] && toupper((unsigned char)cursor[i]) == toupper((unsigned char)needle[i]))
      ++i;
    if (i == needle_length)
      return (char *)cursor;
  }
  return NULL;
}

static int sequel_summary_identifier_character(unsigned char character) {
  return isalnum(character) || character == '_';
}

int sequel_summary_word(const char *source, size_t length, size_t offset, const char *word) {
  size_t n = strlen(word);
  if (offset + n > length)
    return 0;
  for (size_t i = 0; i < n; ++i)
    if (toupper((unsigned char)source[offset + i]) != toupper((unsigned char)word[i]))
      return 0;
  return (!offset || !sequel_summary_identifier_character((unsigned char)source[offset - 1])) &&
         (offset + n == length || !sequel_summary_identifier_character((unsigned char)source[offset + n]));
}

const char *sequel_summary_find_word(const char *source, size_t length, size_t offset, const char *word) {
  for (size_t i = offset; i < length; ++i)
    if (sequel_summary_word(source, length, i, word))
      return source + i;
  return NULL;
}

size_t sequel_summary_skip_space(const char *source, size_t length, size_t offset) {
  while (offset < length && isspace((unsigned char)source[offset]))
    ++offset;
  return offset;
}
