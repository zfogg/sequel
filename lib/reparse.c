#include <sequel/parser.h>

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
