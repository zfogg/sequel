#include <string.h>

#include "model.h"

int sequel_query_array_append(void **array, size_t *count, size_t size, const void *value) {
  size_t required = *count + 1;
  query_array_header *header = *array ? (query_array_header *)*array - 1 : NULL;
  size_t capacity = header ? header->capacity : 0;

  if (capacity < required) {
    capacity = capacity ? capacity : 64;
    while (capacity < required) {
      if (capacity > SIZE_MAX / 2) {
        capacity = required;
        break;
      }
      capacity *= 2;
    }
    if (capacity > SIZE_MAX / size || capacity > (SIZE_MAX - sizeof(*header)) / size)
      return 0;
    header = realloc(header, sizeof(*header) + capacity * size);
    if (!header)
      return 0;
    header->capacity = capacity;
    *array = header + 1;
  }

  memcpy((char *)*array + *count * size, value, size);
  ++*count;
  return 1;
}
