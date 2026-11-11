#pragma once

#include <sequel/parser.h>

#include "runtime_types.h"

sequel_status_t adapt_lexed_tokens(const sequel_lexed_document_t *lexed, format_token **out, size_t *count);
