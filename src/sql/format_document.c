#include <stdlib.h>
#include <string.h>

#include <sequel/formatter.h>

void sequel_format_options_init(sequel_format_options_t *options) {
  if (options)
    *options = (sequel_format_options_t){
        .mode = SEQUEL_FORMAT_SYNTAX_AWARE, .preserve_comments = true, .preserve_literals = true};
}

void sequel_text_result_free(sequel_text_result_t *result) {
  if (!result)
    return;
  free(result->data);
  *result = (sequel_text_result_t){0};
}

static sequel_status_t copy_source(sequel_source_view_t source, sequel_format_mode_t mode, sequel_text_result_t *out) {
  if (source.length == SIZE_MAX)
    return SEQUEL_ERR_OVERFLOW;
  char *data = malloc(source.length + 1);
  if (!data)
    return SEQUEL_ERR_NOMEM;
  memcpy(data, source.data, source.length);
  data[source.length] = 0;
  sequel_text_result_free(out);
  out->data = data;
  out->length = source.length;
  out->capacity = source.length + 1;
  out->mode = mode;
  return SEQUEL_OK;
}

sequel_status_t sequel_format_tokens(const sequel_lexed_document_t *lexed, const sequel_format_options_t *options,
                                     sequel_text_result_t *out) {
  if (!lexed || !out)
    return SEQUEL_ERR_NULL;
  sequel_format_options_t defaults;
  sequel_format_options_init(&defaults);
  const sequel_format_options_t *settings = options ? options : &defaults;
  return copy_source(sequel_lexed_source(lexed),
                     settings->mode == SEQUEL_FORMAT_SYNTAX_AWARE ? SEQUEL_FORMAT_LEXICAL : settings->mode, out);
}

sequel_status_t sequel_format_syntax(const sequel_syntax_document_t *syntax, const sequel_format_options_t *options,
                                     sequel_text_result_t *out) {
  if (!syntax || !out)
    return SEQUEL_ERR_NULL;
  sequel_format_options_t defaults;
  sequel_format_options_init(&defaults);
  const sequel_format_options_t *settings = options ? options : &defaults;
  size_t issues = 0;
  (void)sequel_syntax_issues(syntax, &issues);
  return copy_source(sequel_lexed_source(sequel_syntax_lexed(syntax)),
                     issues ? SEQUEL_FORMAT_RECOVERED : settings->mode, out);
}
