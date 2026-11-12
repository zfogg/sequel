#pragma once

#include <stdint.h>

#include <sequel/query.h>
#include <sequel/diagnostics.h>

typedef struct {
  unsigned major;
  unsigned minor;
} sequel_schema_version_t;

typedef struct {
  uint8_t *data;
  size_t length;
  size_t capacity;
} sequel_buffer_t;

typedef struct {
  const uint8_t *data;
  size_t length;
} sequel_buffer_view_t;

typedef struct {
  sequel_schema_version_t schema;
} sequel_serialization_options_t;

typedef struct sequel_document_bundle sequel_document_bundle_t;

/** A deserialized bundle owns its source, syntax, query, and diagnostics. */

void sequel_serialization_options_init(sequel_serialization_options_t *options);
void sequel_buffer_free(sequel_buffer_t *buffer);
sequel_status_t sequel_document_serialize(const sequel_syntax_document_t *syntax, const sequel_query_document_t *query,
                                          const sequel_serialization_options_t *options, sequel_buffer_t *out);
sequel_status_t sequel_document_deserialize(const sequel_source_view_t *source, const sequel_buffer_view_t *serialized,
                                            const sequel_serialization_options_t *options,
                                            sequel_document_bundle_t **out);
void sequel_document_bundle_free(sequel_document_bundle_t *bundle);
const sequel_syntax_document_t *sequel_document_bundle_syntax(const sequel_document_bundle_t *bundle);
const sequel_query_document_t *sequel_document_bundle_query(const sequel_document_bundle_t *bundle);
const sequel_diagnostic_list_t *sequel_document_bundle_diagnostics(const sequel_document_bundle_t *bundle);
