#pragma once

/** Status returned by public operations. */
typedef enum {
  SEQUEL_OK = 0,
  SEQUEL_ERR_NULL,
  SEQUEL_ERR_INVALID_ARGUMENT,
  SEQUEL_ERR_OVERFLOW,
  SEQUEL_ERR_NOMEM,
  SEQUEL_ERR_LIMIT,
  SEQUEL_ERR_CANCELLED
} sequel_status_t;
