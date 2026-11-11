#include "internal.h"

sequel_status_t sequel_analysis_format_sanitized(const char *data, size_t length, sequel_dialect_t dialect,
                                                 sequel_text_result_t *out) {
  if (!data || !out)
    return SEQUEL_ERR_NULL;
  *out = (sequel_text_result_t){0};
  sequel_source_view_t source = {data, length};
  sequel_parse_options_t parse_options;
  sequel_parse_options_init(&parse_options);
  parse_options.dialect = dialect;
  parse_options.retain_comments = true;
  sequel_lexed_document_t *lexed = NULL;
  sequel_status_t status = sequel_lex(&source, &parse_options, NULL, &lexed);
  if (status != SEQUEL_OK)
    return status;
  sequel_format_options_t format_options;
  sequel_format_options_init(&format_options);
  format_options.mode = SEQUEL_FORMAT_SYNTAX_AWARE;
  status = sequel_format_tokens(lexed, &format_options, out);
  sequel_lexed_document_free(lexed);
  return status;
}
