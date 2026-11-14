#include <sequel/summary.h>

static _Thread_local bool sequel_summary_utf8_output = true;

void sequel_summary_set_utf8_output(bool enabled) { sequel_summary_utf8_output = enabled; }

const char *sequel_summary_ellipsis(void) { return sequel_summary_utf8_output ? "…" : "..."; }

size_t sequel_summary_ellipsis_length(void) { return sequel_summary_utf8_output ? sizeof("…") - 1 : sizeof("...") - 1; }

void sequel_summary_options_init(sequel_summary_options_t *options) {
  if (options)
    *options = (sequel_summary_options_t){.include_operation = true,
                                          .include_tables = true,
                                          .include_joins = true,
                                          .include_predicates = true,
                                          .include_bind_count = true,
                                          .redact_literals = true,
                                          .redact_relations = false,
                                          .utf8_output = true,
                                          .max_length = 0};
}
