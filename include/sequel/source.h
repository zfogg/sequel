/** @file
 * @brief Owned/borrowed source storage, ranges, and UTF-8 locations.
 */
#pragma once

#include <stddef.h>

#include <sequel/status.h>
#include <sequel/dialect.h>

SEQUEL_EXTERN_C_BEGIN

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

/** @brief Opaque owned source storage object. */
typedef struct sequel_source sequel_source_t;

/** @brief Ownership policy for source bytes. */
typedef enum { SEQUEL_SOURCE_BORROWED = 0, SEQUEL_SOURCE_COPIED } sequel_source_storage_t;

typedef struct {
  sequel_source_view_t view;
  sequel_source_storage_t storage;
  sequel_dialect_t dialect;
} sequel_source_options_t;

/**
 * @brief Source create.
 * @param view Argument supplied by the caller.
 * @param options Argument supplied by the caller.
 * @param out Argument supplied by the caller.
 * @return Status describing success or failure.
 */
sequel_status_t sequel_source_create(const sequel_source_view_t *view, const sequel_source_options_t *options, sequel_source_t **out);
/**
 * @brief Source free.
 * @param source Argument supplied by the caller.
 */
void sequel_source_free(sequel_source_t *source);
/**
 * @brief Source view.
 * @param source Argument supplied by the caller.
 */
sequel_source_view_t sequel_source_view(const sequel_source_t *source);
/**
 * @brief Source dialect.
 * @param source Argument supplied by the caller.
 */
sequel_dialect_t sequel_source_dialect(const sequel_source_t *source);

/** Return whether a range is ordered and contained in a source of length. */
int sequel_source_range_valid(sequel_source_range_t range, size_t source_length);

/** Return whether inner is contained by outer. */
int sequel_source_range_contains(sequel_source_range_t outer, sequel_source_range_t inner);

/**
 * @brief Source view position.
 * @param source Argument supplied by the caller.
 * @param byte_offset Argument supplied by the caller.
 * @param position Argument supplied by the caller.
 * @return Status describing success or failure.
 */
sequel_status_t sequel_source_view_position(const sequel_source_view_t *source, size_t byte_offset, sequel_position_t *position);
/**
 * @brief Source view range position.
 * @param source Argument supplied by the caller.
 * @param range Argument supplied by the caller.
 * @param location Argument supplied by the caller.
 * @return Status describing success or failure.
 */
sequel_status_t sequel_source_view_range_position(const sequel_source_view_t *source, sequel_source_range_t range, sequel_location_t *location);

/** Convert a byte offset using an owned source object. */
sequel_status_t sequel_source_position(const sequel_source_t *source, size_t byte_offset, sequel_position_t *position);

/** Convert a byte range using an owned source object. */
sequel_status_t sequel_source_range_position(const sequel_source_t *source, sequel_source_range_t range, sequel_location_t *location);

SEQUEL_EXTERN_C_END
