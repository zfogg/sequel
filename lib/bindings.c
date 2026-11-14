#include <ctype.h>
#include <string.h>

#include <sequel/query.h>
#include <sequel/binds.h>

#include "model.h"
#include "runtime.h"

sequel_status_t sequel_check_binds(const sequel_bind_descriptor_t *binds, size_t count, size_t statement, size_t supplied) {
  if (!binds && count)
    return SEQUEL_ERR_NULL;
  size_t needed = 0;
  for (size_t i = 0; i < count; ++i) {
    sequel_work_tick(1);
    if (!binds[i].slot || !binds[i].ordinal || binds[i].offset >= binds[i].end_offset || binds[i].style < SEQUEL_BIND_POSITIONAL || binds[i].style > SEQUEL_BIND_TYPED)
      return SEQUEL_ERR_INVALID_ARGUMENT;
    if (binds[i].statement_index == statement && binds[i].slot > needed)
      needed = binds[i].slot;
  }
  return supplied == needed ? SEQUEL_OK : SEQUEL_ERR_INVALID_ARGUMENT;
}

sequel_status_t sequel_query_build_parameters(sequel_query_document_t *document, const sequel_token_t *tokens, size_t token_count, const sequel_source_view_t *source) {
  size_t ordinal = 0;
  for (size_t i = 0; i < token_count; ++i) {
    if (tokens[i].kind != SEQUEL_TOKEN_PARAMETER)
      continue;
    const char *text = source->data + tokens[i].range.start;
    size_t length = tokens[i].range.end - tokens[i].range.start;
    size_t explicit_number = 0;
    sequel_bind_style_t style = sequel_query_bind_style(text, length, &explicit_number);
    if (style == SEQUEL_BIND_UNKNOWN)
      continue;
    ++ordinal;
    size_t name_start = tokens[i].range.start + (style == SEQUEL_BIND_POSITIONAL ? 0 : 1);
    size_t name_end = tokens[i].range.end;
    if (style == SEQUEL_BIND_FORMAT && text[1] == '(') {
      name_start = tokens[i].range.start + 2;
      name_end = tokens[i].range.end >= 2 && source->data[tokens[i].range.end - 2] == ')' ? tokens[i].range.end - 2 : tokens[i].range.end;
    }
    sequel_source_range_t name_range = {name_start, name_end};
    uint64_t identity = explicit_number ? explicit_number : ordinal;
    if (!explicit_number && (style == SEQUEL_BIND_NAMED || style == SEQUEL_BIND_TYPED || style == SEQUEL_BIND_FORMAT)) {
      for (size_t previous = 0; previous < document->parameter_count; ++previous) {
        const sequel_parameter_info_t *candidate = &document->parameters[previous];
        size_t candidate_length = candidate->name_range.end - candidate->name_range.start;
        size_t name_length = name_range.end - name_range.start;
        if (candidate->style == style && candidate_length == name_length && !memcmp(source->data + candidate->name_range.start, source->data + name_range.start, name_length)) {
          identity = candidate->identity;
          break;
        }
      }
    }
    sequel_source_range_t type_range = {tokens[i].range.end, tokens[i].range.end};
    if (style == SEQUEL_BIND_TYPED) {
      size_t type_start = tokens[i].range.start;
      while (type_start + 1 < tokens[i].range.end && source->data[type_start] != ':' && source->data[type_start + 1] != ':')
        ++type_start;
      if (type_start + 1 < tokens[i].range.end) {
        type_start += 2;
        type_range = (sequel_source_range_t){type_start, tokens[i].range.end};
      }
    }
    sequel_parameter_info_t parameter = {.range = tokens[i].range,
                                         .style = style,
                                         .statement_index = sequel_query_statement_for_range(document, tokens[i].range),
                                         .ordinal = ordinal,
                                         .explicit_number = explicit_number,
                                         .name_range = name_range,
                                         .type_range = type_range,
                                         .identity = identity};
    if (!sequel_query_array_append((void **)&document->parameters, &document->parameter_count, sizeof(parameter), &parameter))
      return SEQUEL_ERR_NOMEM;
  }
  return SEQUEL_OK;
}

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
