#include <sequel/source.h>

int sequel_range_valid(sequel_range_t range, size_t source_length) {
  return range.start <= range.end && range.end <= source_length;
}

int sequel_range_contains(sequel_range_t outer, sequel_range_t inner) {
  return outer.start <= inner.start && inner.start <= inner.end && inner.end <= outer.end;
}
