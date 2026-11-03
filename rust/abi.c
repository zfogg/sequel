#include "sequel.h"

/* Test the checked-in Rust ABI declarations using this target's C compiler. */
size_t sequel_rust_layout(unsigned index) {
  const size_t values[] = {
      sizeof(sequel_diagnostic_t),
      sizeof(sequel_analysis_options_t),
      sizeof(sequel_analysis_t),
      sizeof(sequel_source_map_t),
      sizeof(sequel_bind_descriptor_t),
      sizeof(sequel_metadata_t),
      offsetof(sequel_analysis_t, _failure_diagnostic),
      sizeof(sequel_highlight_options_t),
  };
  return index < sizeof(values) / sizeof(values[0]) ? values[index] : 0;
}
