#include "internal.h"

void sequel_analysis_add_diagnostic(sequel_analysis_t *out, sequel_diagnostic_code_t code, size_t start,
                                    size_t end) {
  size_t count = out->diagnostic_count;
  sequel_output_tick(sizeof(sequel_diagnostic_t));
  sequel_diagnostic_t *diagnostics = sequel_analysis_array((void *)out->diagnostics, count + 1, sizeof(*diagnostics));
  sequel_diagnostics_report(NULL, 0, &diagnostics[count], code, 0, 0);
  diagnostics[count].offset = start;
  diagnostics[count].end_offset = end;
  out->diagnostics = diagnostics;
  out->diagnostic_count++;
}

bool sequel_analysis_has_diagnostic(const sequel_analysis_t *out, sequel_diagnostic_code_t code, size_t start,
                                    size_t end) {
  for (size_t i = 0; i < out->diagnostic_count; ++i)
    if (out->diagnostics[i].code == code && out->diagnostics[i].offset == start &&
        out->diagnostics[i].end_offset == end)
      return true;
  return false;
}

void sequel_analysis_sort_diagnostics(sequel_analysis_t *out) {
  sequel_diagnostic_t *diagnostics = (sequel_diagnostic_t *)out->diagnostics;
  for (size_t i = 1; i < out->diagnostic_count; ++i) {
    sequel_diagnostic_t value = diagnostics[i];
    size_t j = i;
    while (j && diagnostics[j - 1].offset > value.offset) {
      diagnostics[j] = diagnostics[j - 1];
      --j;
    }
    diagnostics[j] = value;
  }
}
