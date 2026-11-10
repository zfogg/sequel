#include "internal.h"

static const char summary_clause_ellipsis[] = "\342\200\246";

int sequel_summary_append_redacted_clause(char **text, size_t *length, size_t *capacity) {
  return sequel_summary_append_bytes(text, length, capacity, summary_clause_ellipsis,
                                     sizeof(summary_clause_ellipsis) - 1);
}
