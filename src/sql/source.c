#include <sequel/source.h>

int sequel_source_range_valid(sequel_source_range_t range, size_t source_length) {
  return range.start <= range.end && range.end <= source_length;
}

int sequel_source_range_contains(sequel_source_range_t outer, sequel_source_range_t inner) {
  return outer.start <= inner.start && inner.start <= inner.end && inner.end <= outer.end;
}
