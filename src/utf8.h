#pragma once

#include <stdbool.h>
#include <stddef.h>

/** Return whether the explicit byte range is valid UTF-8. */
bool sequel_utf8_valid(const char *text, size_t length);

/** Return the longest complete UTF-8 prefix within the byte limit. */
size_t sequel_utf8_prefix(const char *text, size_t length, size_t limit);

/** Return terminal display columns for a NUL-terminated UTF-8 string. */
int utf8_display_width(const char *text);

/** Return terminal display columns for at most length bytes of UTF-8 text. */
int utf8_display_width_n(const char *text, size_t length);
