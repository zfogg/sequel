#pragma once

#include <stddef.h>

int sequel_summary_append_text(char **text, size_t *length, size_t *capacity, const char *value);
int sequel_summary_append_bytes(char **text, size_t *length, size_t *capacity, const char *value, size_t amount);
