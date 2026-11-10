#pragma once

#include <stddef.h>

int sequel_summary_append_text(char **text, size_t *length, size_t *capacity, const char *value);
int sequel_summary_append_bytes(char **text, size_t *length, size_t *capacity, const char *value, size_t amount);

int sequel_summary_word(const char *source, size_t length, size_t offset, const char *word);
size_t sequel_summary_skip_space(const char *source, size_t length, size_t offset);
