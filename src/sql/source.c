#include <sequel/source.h>

int sequel_source_range_valid(sequel_source_range_t range, size_t source_length) {
  return range.start <= range.end && range.end <= source_length;
}

int sequel_source_range_contains(sequel_source_range_t outer, sequel_source_range_t inner) {
  return outer.start <= inner.start && inner.start <= inner.end && inner.end <= outer.end;
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
