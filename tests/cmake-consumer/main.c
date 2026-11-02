#include <sequel.h>
#include <string.h>
int main(void) {
  sequel_metadata_t metadata;
  sequel_options_t options = {.dialect = SEQUEL_POSTGRESQL};
  if (sequel_inspect("SELECT 'secret'", &options, &metadata) != SEQUEL_OK)
    return 1;
  if (strcmp(metadata.text, "SELECT ?"))
    return 1;
  sequel_highlight_options_t highlight;
  sequel_highlight_options_init(&highlight);
  sequel_colorscheme_t scheme;
  if (sequel_colorscheme_for_appearance("solarized-dark", SEQUEL_APPEARANCE_LIGHT, &scheme) != SEQUEL_OK ||
      sequel_highlight_set_colorscheme(&highlight, &scheme) != SEQUEL_OK ||
      sequel_highlight_set_color(&highlight, "variable", "#123000") != SEQUEL_OK)
    return 1;
  char html[1024];
  size_t length;
  if (sequel_highlight("SELECT :id", &highlight, html, sizeof(html), &length) != SEQUEL_OK)
    return 1;
  if (!strstr(html, "background-color:#fdf6e3") || !strstr(html, "color:#123000\">:id") || length != strlen(html))
    return 1;
  sequel_diagnostic_t diagnostic;
  if (sequel_diagnose("SELECT 'unfinished", SEQUEL_GENERIC, &diagnostic) != SEQUEL_ERR_INVALID_ARGUMENT ||
      strcmp(sequel_diagnostic_code_name(diagnostic.code), "unterminated_string") || !diagnostic.incomplete)
    return 1;
  highlight.tolerant = true;
  if (sequel_highlight_ex("SELECT 'unfinished", &highlight, html, sizeof(html), &length, &diagnostic) != SEQUEL_OK ||
      diagnostic.code != SEQUEL_DIAG_UNTERMINATED_STRING || !strstr(html, "sequel-string"))
    return 1;
  return sequel_format_ex("SELECT 1;", SEQUEL_GENERIC, html, sizeof(html), &length, &diagnostic) != SEQUEL_OK;
}
