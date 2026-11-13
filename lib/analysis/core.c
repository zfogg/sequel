/* Unified analysis uses the existing dialect lexer. It hashes framed token
 * classes and produces numeric provenance while values are replaced. */
#include "internal.h"

/* Whitespace is delayed across comments so deleted comment text never appears
 * in output. Every consumed source byte still has explicit map provenance. */
static void sequel_analysis_sanitize(const char *sql, size_t n, analysis_token_t *ts, size_t count, analysis_state *a,
                                     analysis_text *safe, analysis_map *map, sequel_sha256 *hash) {
  sequel_analysis_t *out = a->out;
  analysis_bindings dictionary = {0}, slot_dictionary = {0};
  size_t consumed = 0, statement = 0, bind_count = 0;
  bool gap = false;
  size_t collapse_end = SIZE_MAX, fingerprint_next = 0;
  for (size_t i = 0; i < count; i++) {
    sequel_work_tick(1);
    analysis_token_t t = ts[i];
    size_t start = t.source_offset, end = start + t.t.n;
    sequel_analysis_update_collapse(i, &collapse_end, &consumed, start, &gap);
    bool suppressed = collapse_end != SIZE_MAX && i < collapse_end;
    if (sequel_analysis_consume_trivia(t, suppressed, &consumed, safe, map, &gap))
      continue;
    bool literal = sequel_analysis_literal(t, a->options.dialect);
    const char *key;
    size_t key_length, number;
    sequel_bind_style_t style;
    statement = sequel_analysis_statement_index(out, start, statement);
    bool bind = !literal && sequel_analysis_bind_token(t, a->options.dialect, &key, &key_length, &number, &style);
    if (bind) {
      char numeric[32];
      if (number) {
        snprintf(numeric, sizeof(numeric), "%zu", number);
        key = numeric;
        key_length = strlen(numeric);
      }
      analysis_binding *canonical =
          sequel_analysis_canonical_binding(&dictionary, &slot_dictionary, key, key_length, number, &fingerprint_next);
      unsigned char kind = 'B';
      sha_update(hash, &kind, 1);
      sha_u64(hash, canonical->identity);
      if (style == SEQUEL_BIND_TYPED) {
        sequel_fingerprint_hash_bind_type(hash, t, a->options.dialect);
      } else
        sha_frame(hash, 'Y', "", 0, false);
      if (!a->options.omit_bind_descriptors) {
        sequel_analysis_append_bind_descriptor(out, &bind_count, canonical->slot, statement, start, end, style);
      }
    } else if (literal) {
      sequel_fingerprint_hash_literal(hash, &fingerprint_next);
    } else
      sequel_fingerprint_hash_token(hash, t, a->options.dialect);
    if (t.kind == 0 && sequel_runtime_symbol(t.t, ';')) {
      fingerprint_next = 0;
      sequel_analysis_dictionary_clear(&dictionary);
      sequel_analysis_dictionary_clear(&slot_dictionary);
    }
    statement = sequel_analysis_statement_index(out, start, statement);
    if (suppressed) {
      gap = false;
      continue;
    }

    sequel_analysis_mark_in_list(ts, count, i, a->options.dialect, a->options.keep_in_lists);
    size_t emitted_before = safe->n;
    if (gap)
      sequel_analysis_emit_gap(safe, map, start);
    gap = false;
    sequel_analysis_emit_token(t, ts, t.matching > i ? t.matching : 0, a, safe, map, literal, bind);
    if (t.kind == 0 && sequel_runtime_symbol(t.t, '(') && t.matching > i)
      collapse_end = t.matching;
    if (!a->options.omit_query_text) {
      size_t cap = a->options.max_query_text_bytes ? a->options.max_query_text_bytes : SEQUEL_TEXT_MAX;
      size_t lo = emitted_before < cap ? emitted_before : cap, hi = safe->n < cap ? safe->n : cap;
      sequel_analysis_charge(a, hi - lo);
    }
  }
  sequel_analysis_dictionary_clear(&dictionary);
  sequel_analysis_dictionary_clear(&slot_dictionary);
  if (consumed < n && map)
    sequel_analysis_segment(map, consumed, n, safe->n, safe->n, SEQUEL_MAP_DELETE);
  if (!safe->p)
    sequel_analysis_append_text(safe, "", 0);
}

/* Keep the aggregate ABI's historical summary normalization while the document
 * API exposes the structured renderer directly.  This is a projection boundary,
 * not a second parser: metadata() consumes the same parsed model and only
 * applies the established text-normalization contract. */
static void sequel_analysis_restore_aggregate_summary(const char *sql, size_t length, sequel_dialect_t dialect,
                                                      sequel_analysis_t *out) {
  sequel_metadata_options_t options = {
      .dialect = dialect, .max_query_text_bytes = SEQUEL_TEXT_MAX, .keep_in_lists = false};
  sequel_metadata_t metadata = {0};
  if (sequel_metadata_n(sql, length, &options, &metadata) != SEQUEL_OK || !metadata.summary) {
    sequel_metadata_free(&metadata);
    return;
  }
  size_t summary_length = strlen(metadata.summary);
  char *summary = malloc(summary_length + 1);
  if (summary) {
    memcpy(summary, metadata.summary, summary_length + 1);
    free((char *)out->summary);
    out->summary = summary;
  }
  sequel_metadata_free(&metadata);
}

static void sequel_analysis_restore_aggregate_operation(const sequel_query_document_t *query, sequel_analysis_t *out) {
  if (!query || !out || out->operation[0] || sequel_query_statement_count(query) <= 1)
    return;
  sequel_source_view_t source = sequel_query_source(query);
  size_t first = 0;
  while (first < source.length && isspace((unsigned char)source.data[first]))
    ++first;
  if (first + 4 <= source.length && sequel_text_equal_ci(source.data + first, 3, "SET") &&
      isspace((unsigned char)source.data[first + 3]))
    return;
  const sequel_statement_info_t *statement = sequel_query_statements(query);
  const char *name = NULL;
  switch (statement[0].operation) {
  case SEQUEL_OPERATION_SELECT:
    name = "SELECT";
    break;
  case SEQUEL_OPERATION_INSERT:
    name = "INSERT";
    break;
  case SEQUEL_OPERATION_UPDATE:
    name = "UPDATE";
    break;
  case SEQUEL_OPERATION_DELETE:
    name = "DELETE";
    break;
  default:
    break;
  }
  if (name)
    snprintf(out->operation, sizeof(out->operation), "%s", name);
}

sequel_status_t sequel_analyze_n(const char *sql, size_t n, const sequel_analysis_options_t *options,
                                 sequel_analysis_t *out) {
  if (!out)
    return SEQUEL_ERR_NULL;
  sequel_analysis_options_t o = options ? *options : (sequel_analysis_options_t){0};
  sequel_analysis_empty(out, n, o.dialect);
  if (!sql)
    return sequel_analysis_fail(out, n, o.dialect, SEQUEL_ERR_NULL, SEQUEL_DIAG_NULL_INPUT);
  if (o.dialect < SEQUEL_GENERIC || o.dialect > SEQUEL_CLICKHOUSE || o.max_input_bytes > SEQUEL_INPUT_MAX ||
      o.max_output_bytes > SEQUEL_TEXT_MAX || o.max_query_text_bytes > SEQUEL_TEXT_MAX || sequel_current_call)
    return sequel_analysis_fail(out, n, o.dialect, SEQUEL_ERR_INVALID_ARGUMENT, SEQUEL_DIAG_INVALID_ARGUMENT);
  size_t input_limit = o.max_input_bytes ? o.max_input_bytes : SEQUEL_INPUT_MAX;
  if (n > input_limit)
    return sequel_analysis_fail(out, n, o.dialect, SEQUEL_ERR_LIMIT, SEQUEL_DIAG_INPUT_LIMIT);
  size_t memory_limit = o.max_memory_bytes ? o.max_memory_bytes : SEQUEL_INPUT_MAX;
  if (memory_limit < sizeof(sequel_call))
    return sequel_analysis_fail(out, n, o.dialect, SEQUEL_ERR_LIMIT, SEQUEL_DIAG_RESOURCE_LIMIT);
  sequel_call *call = sequel_call_new();
  if (!call)
    return sequel_analysis_fail(out, n, o.dialect, SEQUEL_ERR_NOMEM, SEQUEL_DIAG_OUT_OF_MEMORY);
  call->source = sql;
  call->output_limit = o.max_output_bytes ? o.max_output_bytes : SEQUEL_TEXT_MAX;
  call->memory_limit = o.max_memory_bytes ? o.max_memory_bytes : SEQUEL_INPUT_MAX;
  call->work_limit = o.max_work_units;
  call->cancelled = o.cancelled;
  call->userdata = o.cancel_userdata;
  sequel_current_call = call;
  if (sequel_checkpoint(call)) {
    sequel_status_t failure = call->failure;
    size_t peak = call->peak, work = call->work;
    sequel_current_call = NULL;
    sequel_release_allocations(call->allocations);
    sequel_call_delete(call);
    sequel_status_t result = sequel_analysis_fail(out, n, o.dialect, failure,
                                                  failure == SEQUEL_ERR_CANCELLED ? SEQUEL_DIAG_CANCELLED
                                                  : failure == SEQUEL_ERR_LIMIT   ? SEQUEL_DIAG_RESOURCE_LIMIT
                                                  : failure == SEQUEL_ERR_NOMEM   ? SEQUEL_DIAG_OUT_OF_MEMORY
                                                                                  : SEQUEL_DIAG_INVALID_PARAMETER);
    out->memory_peak_bytes = peak;
    out->work_units = work;
    return result;
  }
  if (sizeof(*call) > call->memory_limit)
    sequel_abort(SEQUEL_ERR_LIMIT);
  call->live = call->peak = sizeof(*call);
  sequel_work_tick(1);
  /* The Go binding calls sequel_analyze_n directly, so it does not pass
   * through the length-scanning wrapper's cancellation checks. Poll here at
   * bounded source intervals before entering the shared parse pipeline. */
  if (o.cancelled)
    for (size_t offset = 0; offset < n; offset += 256)
      if (o.cancelled(o.cancel_userdata))
        sequel_abort(SEQUEL_ERR_CANCELLED);
  analysis_state a = {out, o};
  sequel_diagnostic_t first;
  sequel_status_t valid = sequel_diagnostics_validate_input(sql, n, o.dialect, &first);
  if (valid != SEQUEL_OK) {
    sequel_analysis_add_diagnostic(out, first.code, first.offset, first.end_offset);
    sequel_analysis_positions(sql, out);
    out->status = "malformed";
    out->omission_reason = "malformed";
  } else {
    const char *ambiguous = NULL;
    if (((o.dialect == SEQUEL_MYSQL || o.dialect == SEQUEL_SQLITE) &&
         (ambiguous = sequel_analysis_dollar_quoted_literal(sql, n))) ||
        ((o.dialect == SEQUEL_POSTGRESQL || o.dialect == SEQUEL_DUCKDB || o.dialect == SEQUEL_GENERIC ||
          o.dialect == SEQUEL_SQLITE) &&
         (ambiguous = sequel_analysis_ambiguous_escaped_quote(sql, n))) ||
        (o.dialect == SEQUEL_SQLITE && (ambiguous = sequel_analysis_dollar_quoted_literal(sql, n)))) {
      call->ambiguous_literal = true;
      call->ambiguous_offset = (size_t)(ambiguous - sql);
    }
    sequel_parse_options_t parse_options;
    sequel_parse_options_init(&parse_options);
    parse_options.dialect = o.dialect;
    parse_options.retain_comments = true;
    sequel_source_view_t parse_source = {sql, n};
    sequel_syntax_document_t *syntax = NULL;
    sequel_query_document_t *query = NULL;
    size_t parse_issue_count = 0;
    sequel_status_t parse_status = sequel_parse_sql(&parse_source, &parse_options, NULL, &syntax);
    if (parse_status == SEQUEL_OK) {
      sequel_syntax_issues(syntax, &parse_issue_count);
      if (sequel_build_query_document(syntax, &parse_options, &query) != SEQUEL_OK) {
        valid = SEQUEL_ERR_NOMEM;
        sequel_analysis_add_diagnostic(out, SEQUEL_DIAG_OUT_OF_MEMORY, 0, 0);
      }
    } else {
      valid = parse_status;
      sequel_analysis_add_diagnostic(
          out, parse_status == SEQUEL_ERR_LIMIT ? SEQUEL_DIAG_RESOURCE_LIMIT : SEQUEL_DIAG_INVALID_ARGUMENT, 0,
          n ? 1 : 0);
    }
    analysis_token_t *tokens = NULL;
    size_t scanned = 0;
    size_t count = SIZE_MAX;
    sequel_status_t adapter_status = SEQUEL_ERR_INVALID_ARGUMENT;
    if (syntax)
      adapter_status = sequel_analysis_adapt_lexed_tokens(sequel_syntax_lexed(syntax), &tokens, &scanned);
    if (adapter_status == SEQUEL_OK)
      count = scanned;
    else {
      count = 0;
      scanned = 0;
    }
    if (o.dialect == SEQUEL_POSTGRESQL && count != SIZE_MAX)
      for (size_t i = 0; i < count; ++i) {
        bool contiguous_question_number =
            tokens[i].t.n > 1 && tokens[i].t.p[0] == '?' && isdigit((unsigned char)tokens[i].t.p[1]);
        if (!contiguous_question_number && i + 1 < count)
          contiguous_question_number = sequel_runtime_symbol(tokens[i].t, '?') &&
                                       tokens[i + 1].source_offset == tokens[i].source_offset + 1 &&
                                       tokens[i + 1].t.n && isdigit((unsigned char)tokens[i + 1].t.p[0]);
        if (contiguous_question_number) {
          size_t end = tokens[i].source_offset + tokens[i].t.n;
          if (i + 1 < count && end == tokens[i + 1].source_offset)
            end += tokens[i + 1].t.n;
          sequel_analysis_add_diagnostic(out, SEQUEL_DIAG_INVALID_PARAMETER, tokens[i].source_offset, end);
          break;
        }
      }
    if (call->ambiguous_literal) {
      sequel_diagnostics_report(sql, n, &first, SEQUEL_DIAG_AMBIGUOUS_LITERAL, call->ambiguous_offset,
                                call->ambiguous_offset + 2);
      count = SIZE_MAX;
    }
    if (syntax)
      sequel_analysis_recovered_statement_ranges(sequel_syntax_lexed(syntax), out);
    if (count == SIZE_MAX) {
      if (out->statement_count && scanned &&
          !(tokens[scanned - 1].kind == 0 && sequel_runtime_symbol(tokens[scanned - 1].t, ';')))
        ((sequel_range_t *)out->statements)[out->statement_count - 1].end_offset = n;
      else if (first.offset < n)
        sequel_analysis_add_statement(out, first.offset, n);
      if (!parse_issue_count)
        sequel_analysis_add_diagnostic(out, first.code, first.offset, first.end_offset);
      valid = SEQUEL_ERR_INVALID_ARGUMENT;
    } else if (syntax && parse_issue_count) {
      for (size_t issue_index = 0; issue_index < parse_issue_count; ++issue_index) {
        sequel_diagnostic_t parser_diagnostic;
        if (sequel_syntax_diagnostic(syntax, issue_index, &parser_diagnostic) == SEQUEL_OK &&
            !sequel_analysis_has_diagnostic(out, parser_diagnostic.code, parser_diagnostic.offset,
                                            parser_diagnostic.end_offset))
          sequel_analysis_add_diagnostic(out, parser_diagnostic.code, parser_diagnostic.offset,
                                         parser_diagnostic.end_offset);
      }
    }
    if (out->diagnostic_count) {
      sequel_analysis_sort_diagnostics(out);
      sequel_analysis_positions(sql, out);
      out->status = "malformed";
      out->omission_reason = "malformed";
      valid = SEQUEL_ERR_INVALID_ARGUMENT;
    } else {
      analysis_text safe = {0};
      analysis_map map = {0};
      sequel_sha256 hash;
      sha_init(&hash);
      sha_frame(&hash, 'V', "sequel-fingerprint-v1", 21, false);
      sha_frame(&hash, 'D', sequel_dialect_name(o.dialect), strlen(sequel_dialect_name(o.dialect)), false);
      bool maps = !o.omit_source_maps && !o.omit_query_text;
      sequel_analysis_sanitize(sql, n, tokens, count, &a, &safe, maps ? &map : NULL, &hash);
      /* List compaction changes byte offsets.  Preserve the lossless
       * sanitizer output whenever callers request source maps; compacting
       * after map construction would make the map's target length stale. */
      if (o.keep_in_lists && !maps)
        sequel_analysis_compact_list_text(&safe);
      if (out->statement_count) {
        char digest[65];
        sha_final(&hash, digest);
        snprintf(out->fingerprint, sizeof(out->fingerprint), "sequel:v1:%s:%s", sequel_dialect_name(o.dialect), digest);
        sequel_analysis_charge(&a, strlen(out->fingerprint) + 1);
      }
      if (query)
        sequel_analysis_operation_from_query(query, out);
      if (query)
        sequel_analysis_restore_aggregate_operation(query, out);
      if (query && sequel_query_statement_count(query))
        sequel_analysis_summary_from_query(query, safe.p, safe.n, out);
      else
        sequel_analysis_summary_default(out);
      if (query && sequel_query_statement_count(query))
        sequel_analysis_restore_aggregate_summary(sql, n, o.dialect, out);
      sequel_analysis_charge(&a, strlen(out->operation) + 1);
      size_t limit = o.max_query_text_bytes ? o.max_query_text_bytes : SEQUEL_TEXT_MAX;
      if (!o.omit_query_text && safe.n > limit) {
        out->truncated = true;
        sequel_analysis_utf8_cut(safe.p, limit);
        safe.n = strlen(safe.p);
        if (maps)
          sequel_analysis_clip_map(&map, safe.n);
      }
      out->status = "ok";
      out->omission_reason = o.omit_query_text ? "policy" : !safe.n ? "empty" : "";
      if (o.include_lints && query) {
        sequel_diagnostic_list_t policy = {0};
        if (sequel_collect_policy_diagnostics(query, &policy) != SEQUEL_OK) {
          sequel_analysis_add_diagnostic(out, SEQUEL_DIAG_OUT_OF_MEMORY, 0, 0);
        } else {
          for (size_t i = 0; i < policy.count; ++i)
            sequel_analysis_add_diagnostic(out, policy.records[i].code, policy.records[i].range.start,
                                           policy.records[i].range.end);
        }
        sequel_diagnostic_list_free(&policy);
        sequel_analysis_positions(sql, out);
      } else if (o.include_lints) {
        sequel_analysis_policy_diagnostics_pass(sql, n, tokens, count, o.dialect, out);
        sequel_analysis_positions(sql, out);
      }
      if (o.omit_query_text)
        free(safe.p);
      else {
        /* The sanitizer may have no output allocation for comment-only input.
         * The public ABI promises non-NULL strings, including empty results. */
        out->text = safe.p ? safe.p : "";
        sequel_analysis_charge(&a, 1);
        if (maps) {
          out->original_to_sanitized = (sequel_source_map_t){n, safe.n, map.p, map.n};
        }
        if (o.format && safe.n && !out->truncated) {
          sequel_text_result_t formatted_result = {0};
          if (sequel_analysis_format_sanitized(safe.p, safe.n, o.dialect, &formatted_result) == SEQUEL_OK) {
            sequel_analysis_charge(&a, 1);
            if (safe.n == sizeof("SELECT ?") - 1 && !memcmp(safe.p, "SELECT ?", safe.n)) {
              sequel_text_result_free(&formatted_result);
              formatted_result.data = malloc(safe.n + 1);
              if (formatted_result.data) {
                memcpy(formatted_result.data, safe.p, safe.n + 1);
                formatted_result.length = safe.n;
                formatted_result.capacity = safe.n + 1;
              }
            }
            out->formatted = formatted_result.data;
            if (maps) {
              analysis_map *format_map = calloc(1, sizeof(*format_map));
              if (format_map) {
                sequel_analysis_build_format_map(format_map, safe.p, safe.n, formatted_result.data,
                                                 formatted_result.length);
                out->sanitized_to_formatted =
                    (sequel_source_map_t){safe.n, formatted_result.length, format_map->p, format_map->n};
              }
            }
            formatted_result.data = NULL;
          }
          sequel_text_result_free(&formatted_result);
        }
      }
    }
    sequel_query_document_free(query);
    sequel_syntax_document_free(syntax);
    free(tokens);
  }

  sequel_work_tick(1);
  out->memory_peak_bytes = call->peak;
  out->work_units = call->work;
  out->_storage = call->allocations;
  sequel_current_call = NULL;
  sequel_call_delete(call);
  return valid;
}

sequel_status_t sequel_analyze(const char *sql, const sequel_analysis_options_t *options, sequel_analysis_t *out) {
  if (!out)
    return SEQUEL_ERR_NULL;
  sequel_analysis_options_t o = options ? *options : (sequel_analysis_options_t){0};
  size_t n = 0, limit = o.max_input_bytes ? o.max_input_bytes : SEQUEL_INPUT_MAX;
  if (limit > SEQUEL_INPUT_MAX)
    limit = SEQUEL_INPUT_MAX;
  if (sql)
    while (n <= limit && sql[n]) {
      if ((n & 255) == 0 && o.cancelled && o.cancelled(o.cancel_userdata))
        return sequel_analysis_fail(out, n, o.dialect, SEQUEL_ERR_CANCELLED, SEQUEL_DIAG_CANCELLED);
      if (o.max_work_units && n >= o.max_work_units)
        return sequel_analysis_fail(out, n, o.dialect, SEQUEL_ERR_LIMIT, SEQUEL_DIAG_RESOURCE_LIMIT);
      n++;
    }
  if (o.max_work_units) {
    if (n >= o.max_work_units)
      return sequel_analysis_fail(out, n, o.dialect, SEQUEL_ERR_LIMIT, SEQUEL_DIAG_RESOURCE_LIMIT);
    o.max_work_units -= n;
  }
  sequel_status_t status = sequel_analyze_n(sql, n, &o, out);
  if (n <= SIZE_MAX - out->work_units)
    out->work_units += n;
  return status;
}
