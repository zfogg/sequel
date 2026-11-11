#pragma once

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sequel.h>

#include "../runtime.h"
#include "../runtime_types.h"
#include "../resources.h"
#include "../diagnostics.h"
#include "../util/text.h"
#include "../sha256.h"
#include "../util/utf8.h"

typedef struct {
  char *p;
  size_t n, capacity;
} analysis_text;

sequel_status_t adapt_lexed_tokens(const sequel_lexed_document_t *lexed, format_token **out, size_t *count);

bool analysis_literal(format_token token, sequel_dialect_t dialect);
bool analysis_bind_token(format_token token, sequel_dialect_t dialect, const char **key, size_t *length, size_t *number,
                         sequel_bind_style_t *style);
void analysis_compact_list_text(analysis_text *text);
void analysis_policy_diagnostics_pass(const char *sql, size_t length, format_token *tokens, size_t count,
                                      sequel_dialect_t dialect, sequel_analysis_t *out);
void analysis_positions(const char *sql, sequel_analysis_t *out);
void analysis_operation_from_query(const sequel_query_document_t *query, sequel_analysis_t *out);
void analysis_empty(sequel_analysis_t *out, size_t n, sequel_dialect_t dialect);
sequel_status_t analysis_fail(sequel_analysis_t *out, size_t n, sequel_dialect_t dialect, sequel_status_t status,
                              sequel_diagnostic_code_t code);
void analysis_add_diagnostic(sequel_analysis_t *out, sequel_diagnostic_code_t code, size_t start, size_t end);
bool analysis_word(format_token token, const char *word);
void analysis_summary_from_query(const sequel_query_document_t *query, sequel_analysis_t *out);
void analysis_summary_default(sequel_analysis_t *out);
