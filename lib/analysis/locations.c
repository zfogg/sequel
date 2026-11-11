#include "analysis_internal.h"

void sequel_analysis_positions(const char *sql, sequel_analysis_t *out) {
  if (!sql || !out)
    return;
  sequel_source_view_t source = {sql, out->original_length};
  for (size_t i = 0; i < out->diagnostic_count; ++i) {
    sequel_diagnostic_t *diagnostic = (sequel_diagnostic_t *)out->diagnostics + i;
    sequel_location_t location;
    if (sequel_source_view_range_position(&source, (sequel_source_range_t){diagnostic->offset, diagnostic->end_offset},
                                          &location) != SEQUEL_OK)
      continue;
    diagnostic->line = location.start.line;
    diagnostic->column = location.start.column;
    diagnostic->end_line = location.end.line;
    diagnostic->end_column = location.end.column;
  }
}
