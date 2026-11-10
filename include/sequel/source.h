#pragma once

#include <stddef.h>

#include <sequel/status.h>

/** A half-open byte range in UTF-8 source text. */
typedef struct {
  size_t start;
  size_t end;
} sequel_source_range_t;

/** A one-based source position. Columns count Unicode scalar values. */
typedef struct {
  size_t line;
  size_t column;
} sequel_position_t;

/** A byte range with its derived line and column positions. */
typedef struct {
  sequel_source_range_t bytes;
  sequel_position_t start;
  sequel_position_t end;
} sequel_location_t;

/** A borrowed source view. The caller retains ownership of data. */
typedef struct {
  const char *data;
  size_t length;
} sequel_source_view_t;

/** Return whether a range is ordered and contained in a source of length. */
int sequel_source_range_valid(sequel_source_range_t range, size_t source_length);

/** Return whether inner is contained by outer. */
int sequel_source_range_contains(sequel_source_range_t outer, sequel_source_range_t inner);

sequel_status_t sequel_source_position(const sequel_source_view_t *source, size_t byte_offset,
                                       sequel_position_t *position);
sequel_status_t sequel_source_range_position(const sequel_source_view_t *source, sequel_source_range_t range,
                                             sequel_location_t *location);
