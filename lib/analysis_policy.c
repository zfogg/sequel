/* Policy diagnostics consume the shared token view; they do not rescan SQL text. */
static void analysis_policy_diagnostics_pass(const char *sql, size_t n, format_token *ts, size_t count,
                                             sequel_dialect_t dialect, sequel_analysis_t *out) {
  (void)sql;
  if (out->statement_count > 1)
    analysis_add_diagnostic(out, SEQUEL_DIAG_MULTIPLE_STATEMENTS, out->statements[1].offset,
                            out->statements[1].end_offset);
  size_t depth = 0, joins = 0, in_items = 0, in_start = SIZE_MAX;
  bool write = false, where = false, table = false, operation = false, star = false;
  for (size_t i = 0; i < count; ++i) {
    format_token t = ts[i];
    if (t.kind >= 3)
      continue;
    if (sequel_runtime_symbol(t.t, '(')) {
      if (++depth > 16)
        analysis_add_diagnostic(out, SEQUEL_DIAG_DEEP_QUERY_NESTING, t.source_offset, t.source_offset + 1);
      if (in_start != SIZE_MAX)
        ++in_items;
    } else if (sequel_runtime_symbol(t.t, ')')) {
      if (in_start != SIZE_MAX && depth == in_start) {
        if (in_items > 32)
          analysis_add_diagnostic(out, SEQUEL_DIAG_LARGE_IN_LIST, t.source_offset, t.source_offset + 1);
        in_start = SIZE_MAX;
        in_items = 0;
      }
      if (depth)
        --depth;
    }
    if (sequel_runtime_symbol(t.t, ';') && i + 1 < count && sequel_runtime_symbol(ts[i + 1].t, ';'))
      analysis_add_diagnostic(out, SEQUEL_DIAG_EMPTY_STATEMENT, t.source_offset, t.source_offset + 1);
    if (t.kind == 1) {
      if (!operation) {
        operation = true;
        if (analysis_word(t, "UPDATE") || analysis_word(t, "DELETE"))
          write = true;
        if (!(analysis_word(t, "SELECT") || analysis_word(t, "INSERT") || analysis_word(t, "UPDATE") ||
              analysis_word(t, "DELETE") || analysis_word(t, "MERGE") || analysis_word(t, "WITH")))
          analysis_add_diagnostic(out, SEQUEL_DIAG_UNKNOWN_OPERATION, t.source_offset, t.source_offset + t.t.n);
      }
      if (analysis_word(t, "FROM") || analysis_word(t, "INTO") || analysis_word(t, "UPDATE") ||
          analysis_word(t, "USING"))
        table = true;
      if (analysis_word(t, "WHERE"))
        where = true;
      if (analysis_word(t, "JOIN"))
        ++joins;
      if (analysis_word(t, "IN"))
        in_start = depth;
      if (analysis_word(t, "ILIKE") && dialect != SEQUEL_POSTGRESQL && dialect != SEQUEL_DUCKDB)
        analysis_add_diagnostic(out, SEQUEL_DIAG_DIALECT_PORTABILITY, t.source_offset, t.source_offset + t.t.n);
    }
    if (sequel_runtime_symbol(t.t, '*') && i > 0 && analysis_word(ts[i - 1], "SELECT"))
      star = true;
  }
  if (!table)
    analysis_add_diagnostic(out, SEQUEL_DIAG_NO_REFERENCED_TABLE, 0, n);
  if (write && !where)
    analysis_add_diagnostic(out, SEQUEL_DIAG_UNBOUNDED_WRITE, 0, n);
  if (star)
    analysis_add_diagnostic(out, SEQUEL_DIAG_SELECT_STAR, 0, n);
  if (joins > 8)
    analysis_add_diagnostic(out, SEQUEL_DIAG_EXCESSIVE_JOINS, 0, n);
}
