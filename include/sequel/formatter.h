/** @file
 * @brief Lexical-preserving and syntax-aware SQL formatting APIs.
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>

#include <sequel/parser.h>
#include <sequel/status.h>

/** @brief Selects the formatter's preservation and recovery strategy. */
typedef enum { SEQUEL_FORMAT_LEXICAL = 0, SEQUEL_FORMAT_SYNTAX_AWARE, SEQUEL_FORMAT_RECOVERED } sequel_format_mode_t;

typedef struct {
  sequel_format_mode_t mode;
  bool preserve_comments;
  bool preserve_literals;
} sequel_format_options_t;

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
  sequel_format_mode_t mode;
} sequel_text_result_t;

/**
 * @brief Initializes formatting options with library defaults.
 * @param[out] options Options to initialize.
 */
void sequel_format_options_init(sequel_format_options_t *options);

/** @brief Releases text owned by a formatting result. */
void sequel_text_result_free(sequel_text_result_t *result);

/** @brief Formats a lossless lexical document. */
sequel_status_t sequel_format_tokens(const sequel_lexed_document_t *lexed, const sequel_format_options_t *options, sequel_text_result_t *out);

/** @brief Formats a recovered syntax document. */
sequel_status_t sequel_format_syntax(const sequel_syntax_document_t *syntax, const sequel_format_options_t *options, sequel_text_result_t *out);
