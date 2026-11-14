#include <sequel/parser.h>

#include "model.h"

sequel_status_t sequel_parse_sql(const sequel_source_view_t *source, const sequel_parse_options_t *options, const sequel_cancellation_t *cancellation, sequel_syntax_document_t **out) {
  sequel_lexed_document_t *lexed = NULL;
  sequel_status_t status = sequel_lex(source, options, cancellation, &lexed);
  if (status != SEQUEL_OK)
    return status;
  status = sequel_parse(lexed, options, cancellation, out);
  if (status != SEQUEL_OK) {
    sequel_lexed_document_free(lexed);
    return status;
  }
  (*out)->owns_lexed = true;
  return SEQUEL_OK;
}
