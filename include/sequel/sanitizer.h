/** @file
 * @brief Token-based comment and literal sanitization.
 */
#pragma once

#include <stdbool.h>

#include <sequel/formatter.h>

/** @brief Flags controlling comment removal and literal replacement. */
enum { SEQUEL_SANITIZE_REMOVE_COMMENTS = 1u << 0, SEQUEL_SANITIZE_REPLACE_LITERALS = 1u << 1 };

/**
 * @brief Produces sanitized SQL while preserving source layout where possible.
 * @param lexed Lexical document to sanitize.
 * @param flags Combination of @c SEQUEL_SANITIZE_* flags.
 * @param[out] out Caller-owned result receiving allocated text.
 * @return @ref SEQUEL_STATUS_OK on success.
 */
sequel_status_t sequel_sanitize_tokens(const sequel_lexed_document_t *lexed, unsigned flags, sequel_text_result_t *out);
