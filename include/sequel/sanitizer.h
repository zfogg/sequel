#pragma once

#include <stdbool.h>

#include <sequel/formatter.h>

enum { SEQUEL_SANITIZE_REMOVE_COMMENTS = 1u << 0, SEQUEL_SANITIZE_REPLACE_LITERALS = 1u << 1 };

sequel_status_t sequel_sanitize_tokens(const sequel_lexed_document_t *lexed, unsigned flags, sequel_text_result_t *out);
