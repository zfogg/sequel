#include <ctype.h>

#include <sequel/query.h>

sequel_bind_style_t sequel_query_bind_style(const char *text, size_t length, size_t *number) {
  *number = 0;
  if (!length)
    return SEQUEL_BIND_UNKNOWN;
  if (text[0] == '?') {
    if (length == 1)
      return SEQUEL_BIND_POSITIONAL;
    for (size_t i = 1; i < length; ++i) {
      if (!isdigit((unsigned char)text[i]))
        return SEQUEL_BIND_UNKNOWN;
      *number = *number * 10 + (size_t)(text[i] - '0');
    }
    return *number ? SEQUEL_BIND_NUMBERED : SEQUEL_BIND_UNKNOWN;
  }
  if (text[0] == '$') {
    if (length == 1)
      return SEQUEL_BIND_UNKNOWN;
    bool numeric = true;
    for (size_t i = 1; i < length; ++i) {
      if (!isdigit((unsigned char)text[i]))
        numeric = false;
      else
        *number = *number * 10 + (size_t)(text[i] - '0');
    }
    return numeric && *number ? SEQUEL_BIND_NUMBERED : SEQUEL_BIND_NAMED;
  }
  if (text[0] == ':' || text[0] == '@')
    return length > 1 ? SEQUEL_BIND_NAMED : SEQUEL_BIND_UNKNOWN;
  if (text[0] == '%')
    return length >= 2 && (text[1] == 's' || text[1] == '(') ? SEQUEL_BIND_FORMAT : SEQUEL_BIND_UNKNOWN;
  return SEQUEL_BIND_UNKNOWN;
}
