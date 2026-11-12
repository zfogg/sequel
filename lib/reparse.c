#include <stdlib.h>
#include <string.h>

#include <sequel/parser.h>

sequel_status_t sequel_reparse(const sequel_syntax_document_t *previous, const sequel_text_edit_t *edit,
                               const sequel_parse_options_t *options, const sequel_cancellation_t *cancellation,
                               sequel_syntax_document_t **out) {
  if (!previous || !edit || !out)
    return SEQUEL_ERR_NULL;
  *out = NULL;
  const sequel_lexed_document_t *old_lexed = sequel_syntax_lexed(previous);
  if (!old_lexed)
    return SEQUEL_ERR_INVALID_ARGUMENT;
  sequel_source_view_t old_source = sequel_lexed_source(old_lexed);
  if (edit->start > edit->old_end || edit->old_end > old_source.length ||
      (edit->replacement_length && !edit->replacement))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  size_t removed = edit->old_end - edit->start;
  if (old_source.length - removed > SIZE_MAX - edit->replacement_length)
    return SEQUEL_ERR_OVERFLOW;
  size_t length = old_source.length - removed + edit->replacement_length;
  char *data = malloc(length ? length : 1);
  if (!data)
    return SEQUEL_ERR_NOMEM;
  memcpy(data, old_source.data, edit->start);
  memcpy(data + edit->start, edit->replacement, edit->replacement_length);
  memcpy(data + edit->start + edit->replacement_length, old_source.data + edit->old_end,
         old_source.length - edit->old_end);
  sequel_source_view_t source = {data, length};
  sequel_parse_options_t inherited;
  if (!options) {
    sequel_parse_options_init(&inherited);
    inherited.dialect = sequel_lexed_dialect(old_lexed);
    options = &inherited;
  }
  sequel_status_t status = sequel_parse_sql(&source, options, cancellation, out);
  free(data);
  return status;
}

sequel_status_t sequel_reparse_impact(size_t old_length, const sequel_text_edit_t *edit,
                                      sequel_reparse_impact_t *impact) {
  if (!edit || !impact)
    return SEQUEL_ERR_NULL;
  if (edit->start > edit->old_end || edit->old_end > old_length || (edit->replacement_length && !edit->replacement))
    return SEQUEL_ERR_INVALID_ARGUMENT;
  if (old_length - (edit->old_end - edit->start) > SIZE_MAX - edit->replacement_length)
    return SEQUEL_ERR_OVERFLOW;
  impact->old_range = (sequel_source_range_t){edit->start, edit->old_end};
  impact->new_range = (sequel_source_range_t){edit->start, edit->start + edit->replacement_length};
  impact->preserved_prefix = edit->start;
  impact->preserved_suffix = old_length - edit->old_end;
  return SEQUEL_OK;
}
