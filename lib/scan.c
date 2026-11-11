#include <ctype.h>
#include <string.h>

#include "scan.h"

bool sequel_scan_part(unsigned char character) {
  return character == '_' || isalpha(character) || isdigit(character) || character == '$' || character >= 128;
}

bool sequel_scan_equal(const char *text, size_t length, const char *word) {
  if (!text || !word || strlen(word) != length)
    return false;
  for (size_t i = 0; i < length; ++i)
    if (toupper((unsigned char)text[i]) != toupper((unsigned char)word[i]))
      return false;
  return true;
}

unsigned char sequel_scan_clickhouse_quote(const char *text, size_t length, size_t offset) {
  if (offset + 2 >= length || (unsigned char)text[offset] != 0xe2 || (unsigned char)text[offset + 1] != 0x80 ||
      ((unsigned char)text[offset + 2] != 0x98 && (unsigned char)text[offset + 2] != 0x9c))
    return 0;
  return (unsigned char)text[offset + 2];
}

size_t sequel_scan_clickhouse_quoted_end(const char *text, size_t length, size_t offset) {
  unsigned char close = sequel_scan_clickhouse_quote(text, length, offset);
  if (!close)
    return 0;
  ++close;
  for (size_t i = offset + 3; i + 2 < length; ++i)
    if ((unsigned char)text[i] == 0xe2 && (unsigned char)text[i + 1] == 0x80 && (unsigned char)text[i + 2] == close)
      return i + 3;
  return 0;
}

size_t sequel_scan_quoted(const char *text, size_t length, size_t offset, char close, bool backslash) {
  for (size_t i = offset + 1; i < length; ++i) {
    if (backslash && text[i] == '\\') {
      if (++i == length)
        return 0;
      continue;
    }
    if (text[i] == close) {
      if (i + 1 < length && text[i + 1] == close) {
        ++i;
        continue;
      }
      return i + 1;
    }
  }
  return 0;
}
