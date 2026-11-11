#include <ctype.h>
#include <string.h>

#include "inspection_text.h"
#include "util/utf8.h"

void sequel_inspection_cut_utf8(char *text, size_t limit) {
  size_t length = strlen(text);
  if (length <= limit)
    return;
  limit = sequel_utf8_prefix(text, length, limit);
  text[limit] = 0;
  while (limit && isspace((unsigned char)text[limit - 1]))
    text[--limit] = 0;
}

void sequel_inspection_normalize_text(char *text) {
  size_t read = 0, write = 0;
  bool pending_space = false, line_comment = false, quoted = false;
  char quote = 0;
  while (text[read]) {
    char c = text[read];
    if (line_comment) {
      text[write++] = c;
      ++read;
      if (c == '\n' || c == '\r') line_comment = false;
      continue;
    }
    if (quoted) {
      text[write++] = c;
      ++read;
      if (c == quote && (read < 2 || text[read - 2] != '\\')) quoted = false;
      continue;
    }
    if ((c == '-' && text[read + 1] == '-') || (c == '/' && text[read + 1] == '*')) {
      if (pending_space && write) text[write++] = ' ';
      pending_space = false;
      line_comment = c == '-';
      text[write++] = text[read++];
      text[write++] = text[read++];
      continue;
    }
    if (c == '\'' || c == '"' || c == '`') {
      if (pending_space && write) text[write++] = ' ';
      pending_space = false;
      quoted = true;
      quote = c;
      text[write++] = text[read++];
      continue;
    }
    if (isspace((unsigned char)c)) {
      pending_space = write != 0;
      ++read;
      continue;
    }
    if (pending_space && write) text[write++] = ' ';
    pending_space = false;
    text[write++] = text[read++];
  }
  while (write && isspace((unsigned char)text[write - 1])) --write;
  text[write] = 0;
}

void sequel_inspection_trim_prefix(sequel_text_result_t *result) {
  if (!result || !result->data) return;
  size_t prefix = 0;
  while (prefix < result->length && isspace((unsigned char)result->data[prefix])) ++prefix;
  if (!prefix) return;
  memmove(result->data, result->data + prefix, result->length - prefix + 1);
  result->length -= prefix;
}
