/** @file
 * @brief Versioned lexical, syntax, and query-shape fingerprints.
 */
#pragma once

#include <sequel/parser.h>
#include <sequel/query.h>
#include <sequel/status.h>

enum { SEQUEL_FINGERPRINT_LENGTH = 65 };

/** @brief Selects the structural layer included in a fingerprint. */
typedef enum { SEQUEL_FINGERPRINT_TOKENS = 0, SEQUEL_FINGERPRINT_SYNTAX, SEQUEL_FINGERPRINT_QUERY_SHAPE } sequel_fingerprint_mode_t;

/**
 * @brief Computes a stable hexadecimal fingerprint for parsed SQL.
 * @param syntax Parsed syntax document.
 * @param query Optional normalized query document for query-shape mode.
 * @param mode Structural layer to fingerprint.
 * @param[out] out Caller-owned buffer of @ref SEQUEL_FINGERPRINT_LENGTH bytes.
 * @return @ref SEQUEL_STATUS_OK on success, or a validation/status error.
 */
sequel_status_t sequel_fingerprint_document(const sequel_syntax_document_t *syntax, const sequel_query_document_t *query, sequel_fingerprint_mode_t mode, char out[SEQUEL_FINGERPRINT_LENGTH]);
