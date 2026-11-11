#include "internal.h"

sequel_status_t sequel_summarize_query(const sequel_query_document_t *query, const sequel_summary_options_t *options,
                                       sequel_text_result_t *out) {
  return sequel_summary_render(query, options, out);
}
