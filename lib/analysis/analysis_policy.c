/* Policy diagnostics consume the shared token view; they do not rescan SQL text. */
#include "analysis_internal.h"

void sequel_analysis_policy_diagnostics_pass(const char *sql, size_t n, analysis_token_t *ts, size_t count,
                                             sequel_dialect_t dialect, sequel_analysis_t *out) {
  (void)sql;
  if (out->statement_count > 1)
    sequel_analysis_add_diagnostic(out, SEQUEL_DIAG_MULTIPLE_STATEMENTS, out->statements[1].offset,
                                   out->statements[1].end_offset);
  size_t depth = 0, joins = 0, in_items = 0, in_start = SIZE_MAX;
  bool write = false, where = false, table = false, operation = false, star = false;
  for (size_t i = 0; i < count; ++i) {
    analysis_token_t t = ts[i];
    if (t.kind >= 3)
      continue;
    if (sequel_runtime_symbol(t.t, '(')) {
      if (++depth > 16)
        sequel_analysis_add_diagnostic(out, SEQUEL_DIAG_DEEP_QUERY_NESTING, t.source_offset, t.source_offset + 1);
      if (in_start != SIZE_MAX)
        ++in_items;
    } else if (sequel_runtime_symbol(t.t, ')')) {
      if (in_start != SIZE_MAX && depth == in_start) {
        if (in_items > 32)
          sequel_analysis_add_diagnostic(out, SEQUEL_DIAG_LARGE_IN_LIST, t.source_offset, t.source_offset + 1);
        in_start = SIZE_MAX;
        in_items = 0;
      }
      if (depth)
        --depth;
    }
    if (sequel_runtime_symbol(t.t, ';') && i + 1 < count && sequel_runtime_symbol(ts[i + 1].t, ';'))
      sequel_analysis_add_diagnostic(out, SEQUEL_DIAG_EMPTY_STATEMENT, t.source_offset, t.source_offset + 1);
    if (t.kind == 1) {
      if (!operation) {
        operation = true;
        if (sequel_analysis_word(t, "UPDATE") || sequel_analysis_word(t, "DELETE"))
          write = true;
        if (!(sequel_analysis_word(t, "SELECT") || sequel_analysis_word(t, "INSERT") ||
              sequel_analysis_word(t, "UPDATE") || sequel_analysis_word(t, "DELETE") ||
              sequel_analysis_word(t, "MERGE") || sequel_analysis_word(t, "WITH")))
          sequel_analysis_add_diagnostic(out, SEQUEL_DIAG_UNKNOWN_OPERATION, t.source_offset, t.source_offset + t.t.n);
      }
      if (sequel_analysis_word(t, "FROM") || sequel_analysis_word(t, "INTO") || sequel_analysis_word(t, "UPDATE") ||
          sequel_analysis_word(t, "USING"))
        table = true;
      if (sequel_analysis_word(t, "WHERE"))
        where = true;
      if (sequel_analysis_word(t, "JOIN"))
        ++joins;
      if (sequel_analysis_word(t, "IN"))
        in_start = depth;
      if (sequel_analysis_word(t, "ILIKE") && dialect != SEQUEL_POSTGRESQL && dialect != SEQUEL_DUCKDB)
        sequel_analysis_add_diagnostic(out, SEQUEL_DIAG_DIALECT_PORTABILITY, t.source_offset, t.source_offset + t.t.n);
    }
    if (sequel_runtime_symbol(t.t, '*') && i > 0 && sequel_analysis_word(ts[i - 1], "SELECT"))
      star = true;
  }
  if (!table)
    sequel_analysis_add_diagnostic(out, SEQUEL_DIAG_NO_REFERENCED_TABLE, 0, n);
  if (write && !where)
    sequel_analysis_add_diagnostic(out, SEQUEL_DIAG_UNBOUNDED_WRITE, 0, n);
  if (star)
    sequel_analysis_add_diagnostic(out, SEQUEL_DIAG_SELECT_STAR, 0, n);
  if (joins > 8)
    sequel_analysis_add_diagnostic(out, SEQUEL_DIAG_EXCESSIVE_JOINS, 0, n);
}
