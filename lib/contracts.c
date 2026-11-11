/* Public transport contracts. */
#include <string.h>
#include <sequel.h>
#include "diagnostics.h"
#include "util/utf8.h"

sequel_status_t sequel_diagnostic_catalog_get(size_t index, sequel_diagnostic_t *out) {
  if (!out)
    return SEQUEL_ERR_NULL;
  memset(out, 0, sizeof(*out));
  if (index >= sequel_diagnostic_catalog_count())
    return SEQUEL_ERR_INVALID_ARGUMENT;
  diagnostic_report(NULL, 0, out, (sequel_diagnostic_code_t)index, 0, 0);
  return SEQUEL_OK;
}

sequel_status_t sequel_diagnostic_canonicalize(unsigned version, size_t source_length, const sequel_diagnostic_t *input,
                                               sequel_diagnostic_t *out) {
  if (!out)
    return SEQUEL_ERR_NULL;
  sequel_diagnostic_t candidate = {0};
  if (input)
    candidate = *input;
  memset(out, 0, sizeof(*out));
  if (!input)
    return SEQUEL_ERR_NULL;
  sequel_diagnostic_t canonical;
  if (version != SEQUEL_DIAGNOSTIC_VERSION || candidate.code < 0 ||
      sequel_diagnostic_catalog_get((size_t)candidate.code, &canonical) != SEQUEL_OK)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (candidate.severity != canonical.severity || candidate.incomplete != canonical.incomplete ||
      (candidate.code != SEQUEL_DIAG_NONE && candidate.statement_complete) || candidate.offset > candidate.end_offset ||
      candidate.end_offset > source_length || !candidate.line || !candidate.column || !candidate.end_line ||
      !candidate.end_column || candidate.line > candidate.end_line ||
      (candidate.line == candidate.end_line && candidate.column > candidate.end_column) ||
      candidate.line - 1 > candidate.offset || candidate.column - 1 > candidate.offset ||
      candidate.end_line - 1 > candidate.end_offset || candidate.end_column - 1 > candidate.end_offset ||
      (candidate.offset == candidate.end_offset &&
       (candidate.line != candidate.end_line || candidate.column != candidate.end_column)) ||
      (candidate.code == SEQUEL_DIAG_NONE && (candidate.offset || candidate.end_offset)))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  canonical.offset = candidate.offset;
  canonical.end_offset = candidate.end_offset;
  canonical.line = candidate.line;
  canonical.column = candidate.column;
  canonical.end_line = candidate.end_line;
  canonical.end_column = candidate.end_column;
  canonical.statement_complete = candidate.statement_complete;
  *out = canonical;
  return SEQUEL_OK;
}

/* Validate complete coverage before consulting any range. This also permits a
 * zero-length side (all insertions/deletions), but never empty segments. */
static bool source_map_valid(const sequel_source_map_t *map) {
  if (!map || (!map->segments && map->count))
    return false;
  size_t from = 0, to = 0;
  for (size_t i = 0; i < map->count; ++i) {
    sequel_map_segment_t s = map->segments[i];
    if (s.from_start != from || s.to_start != to || s.from_end < from || s.to_end < to ||
        s.from_end > map->from_length || s.to_end > map->to_length)
      return false;
    size_t a = s.from_end - from, b = s.to_end - to;
    switch (s.kind) {
    case SEQUEL_MAP_COPY:
      if (!a || a != b)
        return false;
      break;
    case SEQUEL_MAP_REPLACE:
      if (!a || !b)
        return false;
      break;
    case SEQUEL_MAP_DELETE:
      if (!a || b)
        return false;
      break;
    case SEQUEL_MAP_INSERT:
      if (a || !b)
        return false;
      break;
    default:
      return false;
    }
    from = s.from_end;
    to = s.to_end;
  }
  return from == map->from_length && to == map->to_length;
}

sequel_status_t sequel_source_map_range(const sequel_source_map_t *map, size_t start, size_t end, bool reverse,
                                        sequel_mapped_range_t *out) {
  if (!out)
    return SEQUEL_ERR_NULL;
  *out = (sequel_mapped_range_t){0};
  if (!map)
    return SEQUEL_ERR_NULL;
  if (!source_map_valid(map) || start > end || end > (reverse ? map->to_length : map->from_length))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (!map->count) {
    out->exact = true;
    return SEQUEL_OK;
  }
  bool found = false, exact = true;
  size_t minimum = SIZE_MAX, maximum = 0;
  for (size_t i = 0; i < map->count; ++i) {
    sequel_map_segment_t s = map->segments[i];
    size_t a = reverse ? s.to_start : s.from_start, b = reverse ? s.to_end : s.from_end;
    size_t c = reverse ? s.from_start : s.to_start, d = reverse ? s.from_end : s.to_end;
    size_t left, right;
    bool segment_exact = s.kind == SEQUEL_MAP_COPY;
    if (a == b) {
      if (!((start == end && start == a) || (start < a && a < end)))
        continue;
      left = c;
      right = d;
    } else if (start == end) {
      if (start < a || start > b)
        continue;
      if (start == a)
        left = right = c;
      else if (start == b)
        left = right = d;
      else if (segment_exact)
        left = right = c + start - a;
      else {
        left = c;
        right = d;
      }
      /* Endpoints remain exact even adjacent to transformed bytes. */
      if (start == a || start == b)
        segment_exact = true;
    } else {
      if (end <= a || start >= b)
        continue;
      if (segment_exact) {
        left = c + (start > a ? start - a : 0);
        right = c + (end < b ? end - a : b - a);
      } else {
        left = c;
        right = d;
      }
    }
    if (left < minimum)
      minimum = left;
    if (right > maximum)
      maximum = right;
    exact &= segment_exact;
    found = true;
  }
  if (!found)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  *out = (sequel_mapped_range_t){minimum, maximum, exact};
  return SEQUEL_OK;
}

static size_t contract_scalar_width(const char *text, size_t length, size_t offset) {
  unsigned char c = (unsigned char)text[offset];
  size_t width = c < 0x80                 ? 1
                 : c >= 0xc2 && c <= 0xdf ? 2
                 : c >= 0xe0 && c <= 0xef ? 3
                 : c >= 0xf0 && c <= 0xf4 ? 4
                                          : 0;
  return width && width <= length - offset && sequel_utf8_valid(text + offset, width) ? width : 0;
}

sequel_status_t sequel_utf8_to_utf16(const char *text, size_t length, size_t offset, size_t *units) {
  if (!units)
    return SEQUEL_ERR_NULL;
  *units = 0;
  if (!text)
    return SEQUEL_ERR_NULL;
  if (length > SEQUEL_INPUT_MAX)
    return SEQUEL_ERR_OVERFLOW;
  if (offset > length)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  size_t total = 0, result = 0;
  bool boundary = offset == length;
  for (size_t i = 0; i < length;) {
    if (i == offset) {
      result = total;
      boundary = true;
    }
    size_t width = contract_scalar_width(text, length, i);
    if (!width)
      return SEQUEL_ERR_INVALID_ARGUMENT;
    total += width == 4 ? 2 : 1;
    i += width;
  }
  if (!boundary)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  *units = offset == length ? total : result;
  return SEQUEL_OK;
}

sequel_status_t sequel_utf16_to_utf8(const char *text, size_t length, size_t units, size_t *offset) {
  if (!offset)
    return SEQUEL_ERR_NULL;
  *offset = 0;
  if (!text)
    return SEQUEL_ERR_NULL;
  if (length > SEQUEL_INPUT_MAX)
    return SEQUEL_ERR_OVERFLOW;
  size_t total = 0, result = 0;
  bool boundary = false;
  for (size_t i = 0; i < length;) {
    if (total == units) {
      result = i;
      boundary = true;
    }
    size_t width = contract_scalar_width(text, length, i);
    if (!width)
      return SEQUEL_ERR_INVALID_ARGUMENT;
    total += width == 4 ? 2 : 1;
    i += width;
  }
  if (total == units) {
    result = length;
    boundary = true;
  }
  if (!boundary)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  *offset = result;
  return SEQUEL_OK;
}
