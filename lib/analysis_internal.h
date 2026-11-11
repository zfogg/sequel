#pragma once

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sequel.h>

#include "runtime.h"
#include "runtime_types.h"
#include "resources.h"
#include "diagnostics.h"
#include "util/text.h"
#include "sha256.h"
#include "util/utf8.h"

typedef struct {
  char *p;
  size_t n, capacity;
} analysis_text;

bool analysis_literal(format_token token, sequel_dialect_t dialect);
bool analysis_bind_token(format_token token, sequel_dialect_t dialect, const char **key, size_t *length, size_t *number,
                         sequel_bind_style_t *style);
void analysis_compact_list_text(analysis_text *text);
void analysis_policy_diagnostics_pass(const char *sql, size_t length, format_token *tokens, size_t count,
                                      sequel_dialect_t dialect, sequel_analysis_t *out);
void analysis_add_diagnostic(sequel_analysis_t *out, sequel_diagnostic_code_t code, size_t start, size_t end);
bool analysis_word(format_token token, const char *word);
