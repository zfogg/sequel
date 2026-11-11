#pragma once

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sequel/analysis.h>
#include <sequel.h>
#include <sequel/binds.h>
#include <sequel/diagnostics.h>
#include <sequel/dialect.h>
#include <sequel/fingerprint.h>
#include <sequel/formatter.h>
#include <sequel/parser.h>
#include <sequel/query.h>
#include <sequel/source.h>
#include <sequel/status.h>
#include <sequel/summary.h>

#include "../runtime.h"
#include "../runtime_types.h"
#include "../resources.h"
#include "../diagnostics.h"
#include "../util/text.h"
#include "../sha256.h"
#include "../util/utf8.h"
#include "../tokens.h"

typedef struct {
  char *p;
  size_t n, capacity;
} analysis_text;

bool analysis_literal(analysis_token_t token, sequel_dialect_t dialect);
bool analysis_bind_token(analysis_token_t token, sequel_dialect_t dialect, const char **key, size_t *length,
                         size_t *number, sequel_bind_style_t *style);
void analysis_compact_list_text(analysis_text *text);
void analysis_policy_diagnostics_pass(const char *sql, size_t length, analysis_token_t *tokens, size_t count,
                                      sequel_dialect_t dialect, sequel_analysis_t *out);
void analysis_positions(const char *sql, sequel_analysis_t *out);
void analysis_operation_from_query(const sequel_query_document_t *query, sequel_analysis_t *out);
void analysis_empty(sequel_analysis_t *out, size_t n, sequel_dialect_t dialect);
sequel_status_t analysis_fail(sequel_analysis_t *out, size_t n, sequel_dialect_t dialect, sequel_status_t status,
                              sequel_diagnostic_code_t code);
void analysis_add_diagnostic(sequel_analysis_t *out, sequel_diagnostic_code_t code, size_t start, size_t end);
bool analysis_word(analysis_token_t token, const char *word);
void analysis_summary_from_query(const sequel_query_document_t *query, sequel_analysis_t *out);
void analysis_summary_default(sequel_analysis_t *out);
