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
  return !strstr(html, "background-color:#fdf6e3") || !strstr(html, "color:#123000\">:id") || length != strlen(html);
}
