#pragma once

#include <stddef.h>

#include <sequel.h>

sequel_status_t sequel_metadata_document(const char *sql, size_t length, const sequel_options_t *options,
                                           sequel_metadata_t *out);
