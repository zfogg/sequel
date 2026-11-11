#pragma once

#include <sequel/summary.h>

sequel_status_t sequel_summary_render_document(const sequel_query_document_t *query,
                                               const sequel_summary_options_t *options,
                                               sequel_text_result_t *out);
