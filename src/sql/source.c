#include <sequel/source.h>
#include <sequel/lexer.h>

#include <stdlib.h>
#include <string.h>

struct sequel_source {
  char *owned_data;
  sequel_source_view_t view;
  sequel_source_storage_t storage;
  sequel_dialect_t dialect;
};

sequel_status_t sequel_source_create(const sequel_source_view_t *view, const sequel_source_options_t *options,
                                     sequel_source_t **out) {
  if (!view || !out)
    return SEQUEL_ERR_NULL;
  *out = NULL;
  sequel_source_storage_t storage = options ? options->storage : SEQUEL_SOURCE_COPIED;
  sequel_dialect_t dialect = options ? options->dialect : SEQUEL_GENERIC;
  if (storage != SEQUEL_SOURCE_BORROWED && storage != SEQUEL_SOURCE_COPIED)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (!sequel_dialect_definition(dialect))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  sequel_source_t *source = calloc(1, sizeof(*source));
  if (!source)
    return SEQUEL_ERR_NOMEM;
  source->storage = storage;
  source->dialect = dialect;
  if (storage == SEQUEL_SOURCE_COPIED) {
    source->owned_data = malloc(view->length + 1);
    if (!source->owned_data) {
      free(source);
      return SEQUEL_ERR_NOMEM;
    }
    memcpy(source->owned_data, view->data, view->length);
    source->owned_data[view->length] = 0;
    source->view = (sequel_source_view_t){source->owned_data, view->length};
  } else {
    source->view = *view;
  }
  *out = source;
  return SEQUEL_OK;
}

void sequel_source_free(sequel_source_t *source) {
  if (!source)
    return;
  free(source->owned_data);
  free(source);
}

sequel_source_view_t sequel_source_view(const sequel_source_t *source) {
  return source ? source->view : (sequel_source_view_t){0};
}

sequel_dialect_t sequel_source_dialect(const sequel_source_t *source) {
  return source ? source->dialect : SEQUEL_GENERIC;
}

int sequel_source_range_valid(sequel_source_range_t range, size_t source_length) {
  return range.start <= range.end && range.end <= source_length;
}

int sequel_source_range_contains(sequel_source_range_t outer, sequel_source_range_t inner) {
  return outer.start <= inner.start && inner.start <= inner.end && inner.end <= outer.end;
}

sequel_status_t sequel_token_text(const sequel_source_t *source, const sequel_token_t *token, const char **data,
                                  size_t *length) {
  if (!source || !token || !data || !length)
    return SEQUEL_ERR_NULL;
  if (!sequel_source_range_valid(token->range, source->view.length))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  *data = source->view.data + token->range.start;
  *length = token->range.end - token->range.start;
  return SEQUEL_OK;
}

static void position_at(const char *data, size_t length, size_t offset, sequel_position_t *position) {
  position->line = 1;
  position->column = 1;
  if (offset > length)
    offset = length;
  for (size_t i = 0; i < offset; ++i) {
    unsigned char byte = (unsigned char)data[i];
    if (byte == '\r') {
      ++position->line;
      position->column = 1;
      if (i + 1 < offset && data[i + 1] == '\n')
        ++i;
    } else if (byte == '\n') {
      ++position->line;
      position->column = 1;
    } else if ((byte & 0xc0) != 0x80) {
      ++position->column;
    }
  }
}

sequel_status_t sequel_source_position(const sequel_source_view_t *source, size_t byte_offset,
                                       sequel_position_t *position) {
  if (!source || !position)
    return SEQUEL_ERR_NULL;
  if (byte_offset > source->length)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  position_at(source->data, source->length, byte_offset, position);
  return SEQUEL_OK;
}

sequel_status_t sequel_source_range_position(const sequel_source_view_t *source, sequel_source_range_t range,
                                             sequel_location_t *location) {
  if (!source || !location)
    return SEQUEL_ERR_NULL;
  if (!sequel_source_range_valid(range, source->length))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  location->bytes = range;
  position_at(source->data, source->length, range.start, &location->start);
  position_at(source->data, source->length, range.end, &location->end);
  return SEQUEL_OK;
}
