#pragma once

#include <sequel/formatter.h>

void sequel_inspection_normalize_text(char *text);
void sequel_inspection_trim_prefix(sequel_text_result_t *result);
void sequel_inspection_cut_utf8(char *text, size_t limit);
