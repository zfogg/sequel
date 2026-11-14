#pragma once

/** @file @brief Lexical-preserving and syntax-aware SQL formatting APIs. */

#include <stddef.h>
#include <stdbool.h>

#include <sequel/parser.h>
#include <sequel/status.h>

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

void sequel_format_options_init(sequel_format_options_t *options);
void sequel_text_result_free(sequel_text_result_t *result);

sequel_status_t sequel_format_tokens(const sequel_lexed_document_t *lexed, const sequel_format_options_t *options, sequel_text_result_t *out);
sequel_status_t sequel_format_syntax(const sequel_syntax_document_t *syntax, const sequel_format_options_t *options, sequel_text_result_t *out);
