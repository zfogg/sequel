#pragma once

/* Transitional token view used only by the remaining legacy analysis passes.
 * New code must consume sequel_token_t directly. */
typedef struct {
  token t;
  bool gap;
  bool newline;
  int kind;
  size_t matching;
  bool multiline;
  size_t flat_width;
  size_t source_offset;
  size_t rendered_offset;
} format_token;
