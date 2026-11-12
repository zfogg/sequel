#pragma once

#include <stddef.h>

#include <sequel.h>

void sequel_metadata_set_operation(const sequel_source_view_t *source,
                                     const sequel_statement_info_t *statements,
                                     size_t statement_count, char *operation, size_t operation_capacity);
