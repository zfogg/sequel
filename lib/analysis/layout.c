#include "analysis_internal.h"

/* Clause boundaries used by the shared layout pass. */
bool boundary(token t) {
  const char *words[] = {"FROM",  "WHERE",     "GROUP",  "HAVING",    "ORDER", "LIMIT", "OFFSET",  "FETCH",  "FOR",
                         "UNION", "INTERSECT", "EXCEPT", "RETURNING", "SET",   "USING", "QUALIFY", "WINDOW", "EXCLUDE"};
  if (t.clickhouse && (sequel_runtime_keyword(t, "PREWHERE") || sequel_runtime_keyword(t, "SETTINGS") ||
                       sequel_runtime_keyword(t, "FORMAT") || sequel_runtime_keyword(t, "SAMPLE") ||
                       sequel_runtime_keyword(t, "VALUES")))
    return true;
  for (size_t i = 0; (sequel_work_tick(1), i < sizeof(words) / sizeof(*words)); ++i)
    if (sequel_runtime_keyword(t, words[i]))
      return true;
  return sequel_runtime_symbol(t, ';') || sequel_runtime_symbol(t, ')');
}
