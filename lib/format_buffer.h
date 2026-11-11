#pragma once

#include <stddef.h>

int sequel_format_append_text(char **data, size_t *length, size_t *capacity, const char *text, size_t amount);
int sequel_format_append_space(char **data, size_t *length, size_t *capacity);
int sequel_format_append_line_start(char **data, size_t *length, size_t *capacity, size_t indent);
