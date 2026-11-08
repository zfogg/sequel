#include "utf8.h"
#include <utf8proc.h>
#include <string.h>

bool sequel_utf8_valid(const char *text, size_t length) {
  size_t i = 0;
  while (i < length) {
    utf8proc_int32_t codepoint;
    utf8proc_ssize_t n =
        utf8proc_iterate((const utf8proc_uint8_t *)text + i, (utf8proc_ssize_t)(length - i), &codepoint);
    if (n < 1)
      return false;
    i += (size_t)n;
  }
  return true;
}

size_t sequel_utf8_prefix(const char *text, size_t length, size_t limit) {
  size_t i = 0;
  while (i < length && i < limit) {
    utf8proc_int32_t codepoint;
    utf8proc_ssize_t n =
        utf8proc_iterate((const utf8proc_uint8_t *)text + i, (utf8proc_ssize_t)(length - i), &codepoint);
    if (n < 1 || (size_t)n > limit - i)
      break;
    i += (size_t)n;
  }
  return i;
}

int utf8_display_width(const char *text) { return utf8_display_width_n(text, text ? strlen(text) : 0); }

int utf8_display_width_n(const char *text, size_t length) {
  size_t i = 0;
  int width = 0;
  if (!text)
    return 0;
  while (i < length) {
    utf8proc_int32_t codepoint;
    utf8proc_ssize_t n =
        utf8proc_iterate((const utf8proc_uint8_t *)text + i, (utf8proc_ssize_t)(length - i), &codepoint);
    if (n < 1)
      break;
    int columns = utf8proc_charwidth(codepoint);
    if (columns > 0)
      width += columns;
    i += (size_t)n;
  }
  return width;
}
