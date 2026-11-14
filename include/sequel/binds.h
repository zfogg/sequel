#pragma once

#include <stddef.h>

#include <sequel/status.h>

typedef enum { SEQUEL_BIND_POSITIONAL = 0, SEQUEL_BIND_NUMBERED, SEQUEL_BIND_NAMED, SEQUEL_BIND_TYPED, SEQUEL_BIND_FORMAT, SEQUEL_BIND_UNKNOWN } sequel_bind_style_t;

/** Value-free parameter metadata with source coordinates and driver slot. */
typedef struct {
  size_t ordinal;
  size_t slot;
  size_t statement_index;
  size_t offset;
  size_t end_offset;
  sequel_bind_style_t style;
} sequel_bind_descriptor_t;

/** Validate supplied arity for one statement without reading values. */
sequel_status_t sequel_check_binds(const sequel_bind_descriptor_t *binds, size_t count, size_t statement_index, size_t supplied_slots);
