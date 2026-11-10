#pragma once

#include <sequel/parser.h>
#include <sequel/query.h>
#include <sequel/status.h>

#define SEQUEL_FINGERPRINT_LENGTH 65

typedef enum {
  SEQUEL_FINGERPRINT_TOKENS = 0,
  SEQUEL_FINGERPRINT_SYNTAX,
  SEQUEL_FINGERPRINT_QUERY_SHAPE
} sequel_fingerprint_mode_t;

sequel_status_t sequel_fingerprint_document(const sequel_syntax_document_t *syntax,
                                            const sequel_query_document_t *query, sequel_fingerprint_mode_t mode,
                                            char out[SEQUEL_FINGERPRINT_LENGTH]);
