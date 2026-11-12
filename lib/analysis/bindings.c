#include "internal.h"

static uint64_t sequel_analysis_key_hash(const char *data, size_t length) {
  sequel_sha256 sha;
  sha_init(&sha);
  sha_update(&sha, data, length);
  char hex[65];
  sha_final(&sha, hex);
  uint64_t value = 0;
  for (size_t i = 0; i < 16; i++) {
    sequel_work_tick(1);
    value = (value << 4) | (uint64_t)(hex[i] <= '9' ? hex[i] - '0' : hex[i] - 'a' + 10);
  }
  return value;
}

analysis_binding *sequel_analysis_binding_slot(analysis_bindings *bindings, const char *key, size_t length,
                                               size_t number) {
  if (!bindings->capacity || bindings->count >= bindings->capacity / 2) {
    size_t next = bindings->capacity ? bindings->capacity * 2 : 16;
    if (next < bindings->capacity || next > SIZE_MAX / sizeof(*bindings->p))
      sequel_abort(SEQUEL_ERR_LIMIT);
    analysis_binding *grown = calloc(next, sizeof(*grown));
    for (size_t i = 0; i < bindings->capacity; i++) {
      sequel_work_tick(1);
      if (bindings->p[i].occupied) {
        size_t at = (size_t)bindings->p[i].hash & (next - 1);
        while (grown[at].occupied)
          at = (at + 1) & (next - 1);
        grown[at] = bindings->p[i];
      }
    }
    free(bindings->p);
    bindings->p = grown;
    bindings->capacity = next;
  }
  uint64_t hash = length ? sequel_analysis_key_hash(key, length) : (uint64_t)(bindings->identities + 1);
  size_t at = (size_t)hash & (bindings->capacity - 1);
  while (bindings->p[at].occupied) {
    analysis_binding *value = &bindings->p[at];
    if (length && value->hash == hash && value->length == length && value->numbered == (number != 0)) {
      sequel_work_tick(length);
      if (!memcmp(value->key, key, length))
        return value;
    }
    at = (at + 1) & (bindings->capacity - 1);
  }
  if (bindings->maximum == SIZE_MAX || bindings->identities == SIZE_MAX)
    sequel_abort(SEQUEL_ERR_LIMIT);
  size_t slot = number ? number : bindings->maximum + 1;
  if (slot > bindings->maximum)
    bindings->maximum = slot;
  bindings->p[at] = (analysis_binding){key, length, slot, ++bindings->identities, hash, true, false, number != 0};
  ++bindings->count;
  return &bindings->p[at];
}

analysis_binding *sequel_analysis_numeric_identity(analysis_bindings *bindings, size_t slot) {
  char encoded[32];
  snprintf(encoded, sizeof(encoded), "%zu", slot);
  size_t length = strlen(encoded);
  analysis_binding *value = sequel_analysis_binding_slot(bindings, encoded, length, slot);
  if (value->key == encoded) {
    char *owned = malloc(length);
    memcpy(owned, encoded, length);
    value->key = owned;
    value->owned = true;
  }
  return value;
}

void sequel_analysis_dictionary_clear(analysis_bindings *bindings) {
  for (size_t i = 0; i < bindings->capacity; i++) {
    sequel_work_tick(1);
    if (bindings->p[i].occupied && bindings->p[i].owned)
      free((void *)bindings->p[i].key);
  }
  free(bindings->p);
  *bindings = (analysis_bindings){0};
}

void sequel_analysis_append_bind_descriptor(sequel_analysis_t *analysis, size_t *bind_number, size_t slot,
                                            size_t statement, size_t start, size_t end, sequel_bind_style_t style) {
  sequel_output_tick(sizeof(sequel_bind_descriptor_t));
  sequel_bind_descriptor_t *binds =
      sequel_analysis_array((void *)analysis->binds, analysis->bind_count + 1, sizeof(*binds));
  binds[analysis->bind_count] =
      (sequel_bind_descriptor_t){++*bind_number, slot, statement, start, end, style};
  analysis->binds = binds;
  analysis->bind_count++;
}
