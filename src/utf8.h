#ifndef SEQUEL_UTF8_H
#define SEQUEL_UTF8_H
#include <stdbool.h>
#include <stddef.h>
bool sequel_utf8_valid(const char *text, size_t length);
size_t sequel_utf8_prefix(const char *text, size_t length, size_t limit);
#endif
