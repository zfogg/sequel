#pragma once

#include <stdbool.h>
#include <stddef.h>

bool sequel_scan_part(unsigned char character);
bool sequel_scan_equal(const char *text, size_t length, const char *word);
unsigned char sequel_scan_clickhouse_quote(const char *text, size_t length, size_t offset);
size_t sequel_scan_clickhouse_quoted_end(const char *text, size_t length, size_t offset);
size_t sequel_scan_quoted(const char *text, size_t length, size_t offset, char close, bool backslash);
