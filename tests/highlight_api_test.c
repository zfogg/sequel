#include <sequel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                                                                                                                               \
  do {                                                                                                                                                                                                 \
    if (!(condition)) {                                                                                                                                                                                \
      fprintf(stderr, "%d: %s\n", __LINE__, #condition);                                                                                                                                               \
      return 1;                                                                                                                                                                                        \
    }                                                                                                                                                                                                  \
  } while (0)

int main(void) {
  sequel_highlight_options_t options;
  sequel_highlight_options_init(&options);
  CHECK(options.mode == SEQUEL_HTML && options.colors[SEQUEL_COLOR_BACKGROUND] == 0x181818);
  size_t length = 99;
  char small[8] = "dirty";
  CHECK(sequel_highlight(NULL, NULL, small, sizeof(small), &length) == SEQUEL_ERR_NULL && !*small);
  CHECK(sequel_highlight("SELECT 1", NULL, NULL, 0, NULL) == SEQUEL_ERR_NULL);
  CHECK(sequel_highlight("SELECT 1", NULL, NULL, 1, &length) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_highlight("SELECT 1", NULL, small, 0, &length) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_highlight("SELECT 1", NULL, NULL, 0, &length) == SEQUEL_OK && length > 8);
  size_t measured = length;
  CHECK(sequel_highlight("SELECT 1", NULL, small, sizeof(small), &length) == SEQUEL_ERR_OVERFLOW);
  CHECK(!*small && length == measured);
  char *out = malloc(length + 2);
  CHECK(out);
  out[length + 1] = 'X';
  CHECK(sequel_highlight("SELECT 1", NULL, out, length, &length) == SEQUEL_ERR_OVERFLOW && !*out);
  CHECK(out[length + 1] == 'X');
  CHECK(sequel_highlight("SELECT 1", NULL, out, length + 1, &length) == SEQUEL_OK);
  CHECK(strlen(out) == measured && strstr(out, "sequel-keyword"));
  free(out);
  CHECK(sequel_highlight("\xff", NULL, small, sizeof(small), &length) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(!*small && !length);
  CHECK(sequel_highlight("SELECT '\033[2J'", NULL, small, sizeof(small), &length) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_highlight("SELECT '\xc2\x9b'", NULL, small, sizeof(small), &length) == SEQUEL_ERR_INVALID_ARGUMENT);
  options.mode = (sequel_highlight_mode_t)2;
  CHECK(sequel_highlight("", &options, NULL, 0, &length) == SEQUEL_ERR_INVALID_ARGUMENT);
  options.mode = SEQUEL_HTML;
  options.dialect = (sequel_dialect_t)99;
  CHECK(sequel_highlight("", &options, NULL, 0, &length) == SEQUEL_ERR_INVALID_ARGUMENT);
  options.dialect = SEQUEL_GENERIC;
  options.colors[0] = 0x1000000;
  CHECK(sequel_highlight("", &options, NULL, 0, &length) == SEQUEL_ERR_INVALID_ARGUMENT);
  sequel_highlight_options_init(&options);
  CHECK(sequel_highlight_set_color(&options, "variable", "#123000") == SEQUEL_OK);
  CHECK(options.colors[SEQUEL_COLOR_VARIABLE] == 0x123000);
  CHECK(sequel_highlight_set_color(&options, "variable", "Ff00Aa") == SEQUEL_OK);
  CHECK(options.colors[SEQUEL_COLOR_VARIABLE] == 0xff00aa);
  CHECK(sequel_highlight_set_color(&options, "unknown", "#123000") == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_highlight_set_color(&options, "string", "#abc") == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_highlight_set_color(&options, "string", "#123456;display:none") == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_highlight_set_color(NULL, "string", "#123456") == SEQUEL_ERR_NULL);
  CHECK(!sequel_highlight_color_name((sequel_highlight_color_t)-1));
  CHECK(!sequel_highlight_color_name(SEQUEL_COLOR_COUNT));
  for (int i = 0; i < SEQUEL_COLOR_COUNT; ++i)
    CHECK(sequel_highlight_color_name((sequel_highlight_color_t)i));
  CHECK(sequel_colorscheme_count() >= 300);
  CHECK(!sequel_colorscheme_name(sequel_colorscheme_count()));
  for (size_t i = 0; i < sequel_colorscheme_count(); ++i) {
    sequel_colorscheme_t scheme;
    CHECK(sequel_colorscheme(sequel_colorscheme_name(i), &scheme) == SEQUEL_OK);
    CHECK(sequel_highlight_set_colorscheme(&options, &scheme) == SEQUEL_OK);
    CHECK(sequel_highlight("select count(*), true, null, '🙂' from t where id=:id", &options, NULL, 0, &length) == SEQUEL_OK);
    sequel_colorscheme_t original = scheme;
    CHECK(sequel_colorscheme_set_appearance(&scheme, SEQUEL_APPEARANCE_AUTO) == SEQUEL_OK);
    CHECK(!memcmp(&scheme, &original, sizeof(scheme)));
    for (int a = SEQUEL_APPEARANCE_DARK; a <= SEQUEL_APPEARANCE_LIGHT; ++a) {
      scheme = original;
      const char *variant = sequel_colorscheme_variant_name(sequel_colorscheme_name(i), (sequel_appearance_t)a);
      sequel_status_t status = sequel_colorscheme_for_appearance(sequel_colorscheme_name(i), (sequel_appearance_t)a, &scheme);
      if (!variant) {
        CHECK(status == SEQUEL_ERR_INVALID_ARGUMENT);
        CHECK(!memcmp(&scheme, &original, sizeof(scheme)));
        continue;
      }
      CHECK(status == SEQUEL_OK);
      sequel_colorscheme_t expected;
      CHECK(sequel_colorscheme(variant, &expected) == SEQUEL_OK);
      CHECK(!memcmp(&scheme, &expected, sizeof(scheme)));
      sequel_colorscheme_t once = scheme;
      CHECK(sequel_colorscheme_set_appearance(&scheme, (sequel_appearance_t)a) == SEQUEL_OK);
      if (memcmp(&scheme, &once, sizeof(scheme))) {
        fprintf(stderr, "appearance not idempotent: %s mode %d\n", sequel_colorscheme_name(i), a);
        return 1;
      }
    }
  }
  sequel_colorscheme_t scheme;
  CHECK(sequel_colorscheme("default-dark", &scheme) == SEQUEL_OK);
  CHECK(sequel_colorscheme_set_appearance(&scheme, SEQUEL_APPEARANCE_LIGHT) == SEQUEL_OK);
  CHECK(scheme.base[0] == 0xf8f8f8 && scheme.base[5] == 0x383838 && scheme.base[14] == 0xba8baf);
  CHECK(sequel_colorscheme_set_appearance(&scheme, SEQUEL_APPEARANCE_DARK) == SEQUEL_OK);
  CHECK(scheme.base[0] == 0x181818 && scheme.base[5] == 0xd8d8d8);
  CHECK(!strcmp(sequel_colorscheme_variant_name("catppuccin-mocha", SEQUEL_APPEARANCE_LIGHT), "catppuccin-latte"));
  CHECK(!strcmp(sequel_colorscheme_variant_name("gruvbox-dark-hard", SEQUEL_APPEARANCE_LIGHT), "gruvbox-light-hard"));
  CHECK(!sequel_colorscheme_variant_name("monokai", SEQUEL_APPEARANCE_LIGHT));
  CHECK(!sequel_colorscheme_variant_name(NULL, SEQUEL_APPEARANCE_AUTO));
  CHECK(sequel_colorscheme("monokai", &scheme) == SEQUEL_OK);
  sequel_colorscheme_t monokai = scheme;
  CHECK(sequel_colorscheme_set_appearance(&scheme, SEQUEL_APPEARANCE_LIGHT) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(!memcmp(&scheme, &monokai, sizeof(scheme)));
  CHECK(sequel_colorscheme_set_appearance(&scheme, (sequel_appearance_t)99) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_colorscheme_set_appearance(NULL, SEQUEL_APPEARANCE_AUTO) == SEQUEL_ERR_NULL);
  sequel_colorscheme_t saved = scheme;
  CHECK(sequel_colorscheme("missing", &scheme) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(sequel_colorscheme_parse("base00: '123456'", &scheme) == SEQUEL_ERR_INVALID_ARGUMENT);
  CHECK(!memcmp(&scheme, &saved, sizeof(scheme)));
  char *large = malloc(1024 * 1024 + 2);
  CHECK(large);
  memset(large, ' ', 1024 * 1024 + 1);
  large[1024 * 1024 + 1] = 0;
  CHECK(sequel_highlight(large, NULL, NULL, 0, &length) == SEQUEL_OK);
  large[1024 * 1024] = 0;
  CHECK(sequel_highlight(large, NULL, NULL, 0, &length) == SEQUEL_OK);
  for (size_t i = 0; i < 70000; ++i) {
    large[2 * i] = '?';
    large[2 * i + 1] = ' ';
  }
  large[140000] = 0;
  CHECK(sequel_highlight(large, NULL, NULL, 0, &length) == SEQUEL_OK);
  free(large);
  puts("highlight API contracts and all palettes passed");
  return 0;
}
