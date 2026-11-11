#include "analysis_internal.h"

void sequel_analysis_empty(sequel_analysis_t *out, size_t n, sequel_dialect_t dialect) {
  *out = (sequel_analysis_t){0};
  out->schema_version = SEQUEL_ANALYSIS_VERSION;
  out->dialect = dialect;
  out->original_length = n;
  out->status = "error";
  out->omission_reason = "analysis_error";
  out->text = "";
  out->summary = "SQL";
  out->formatted = "";
}

void sequel_analysis_free(sequel_analysis_t *out) {
  if (!out)
    return;
  sequel_release_allocations(out->_storage);
  sequel_analysis_empty(out, 0, SEQUEL_GENERIC);
}

sequel_status_t sequel_analysis_fail(sequel_analysis_t *out, size_t n, sequel_dialect_t dialect, sequel_status_t status,
                                     sequel_diagnostic_code_t code) {
  sequel_analysis_empty(out, n, dialect);
  out->status = status == SEQUEL_ERR_CANCELLED                                ? "cancelled"
                : status == SEQUEL_ERR_LIMIT || status == SEQUEL_ERR_OVERFLOW ? "limit"
                : status == SEQUEL_ERR_INVALID_ARGUMENT                       ? "malformed"
                                                                              : "error";
  out->omission_reason = status == SEQUEL_ERR_CANCELLED      ? "cancelled"
                         : !strcmp(out->status, "limit")     ? "analysis_limit"
                         : !strcmp(out->status, "malformed") ? "malformed"
                                                             : "analysis_error";
  diagnostic_report(NULL, 0, &out->_failure_diagnostic, code, 0, 0);
  out->diagnostics = &out->_failure_diagnostic;
  out->diagnostic_count = 1;
  return status;
}
