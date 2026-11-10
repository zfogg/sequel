static const struct {
  const char *name, *message, *explanation, *hint;
} diagnostic_messages[] = {
    {"none", "No lexical or delimiter issues found",
     "This checks quoting, comments, delimiters and resource limits. It does not validate SQL grammar or execute a "
     "database query.",
     "End a complete interactive statement with a semicolon."},
    {"null_input", "SQL input is NULL", "The API requires a SQL input buffer.",
     "Pass an empty string to represent empty input."},
    {"invalid_argument", "Invalid API argument", "The dialect, options or output buffer arguments are invalid.",
     "Use a supported dialect and initialize options with the provided initializer."},
    {"input_limit", "SQL exceeds the configured input limit",
     "Input is bounded to keep parsing and rendering resource use predictable.",
     "Split the input into smaller SQL documents."},
    {"invalid_utf8", "Invalid UTF-8 input",
     "SQL text must be valid UTF-8; this byte begins an invalid or truncated sequence.",
     "Decode the source in its original encoding and convert it to UTF-8."},
    {"control_character", "Unsupported control character",
     "NUL and terminal control characters cannot be rendered safely. Tabs, CR and LF are supported.",
     "Remove the control character or represent it with a SQL escape sequence."},
    {"token_limit", "Legacy token-limit diagnostic",
     "Reserved for compatibility. Tokens now use dynamically allocated storage within the SQL input byte limit.",
     "Use the current library to process large token streams."},
    {"nesting_limit", "Nesting exceeds 64 levels",
     "Delimiter, parameter-type or nested-comment depth is bounded to prevent excessive parser work.",
     "Reduce nesting or split the expression into smaller parts."},
    {"unterminated_string", "Unfinished string literal",
     "The string starts here but its closing quote or dollar tag has not been entered yet. Semicolons inside the "
     "string do not finish a statement.",
     "Add the matching closing quote or exact dollar tag; use --tolerant to highlight while editing."},
    {"unterminated_identifier", "Unfinished quoted identifier",
     "A double quote, backtick, bracket or dialect-specific quote opened an identifier that is not closed yet.",
     "Add the matching closing identifier delimiter."},
    {"unterminated_comment", "Unfinished block comment",
     "A /* comment has no matching */. Nested comments must each be closed.",
     "Add */ for each open comment; --tolerant can preview the unfinished comment."},
    {"unterminated_parameter", "Unfinished bind parameter",
     "A dialect-specific parameter has an opening delimiter but no closing delimiter yet.",
     "Close the parameter, for example {name:UInt64} or $name(suffix)."},
    {"invalid_parameter", "Malformed bind parameter", "The parameter does not follow the dialect's name/type syntax.",
     "For ClickHouse use {name:Type}, for example {user_id:UInt64}."},
    {"unmatched_close", "Unexpected closing delimiter",
     "This closing parenthesis or bracket has no matching opening delimiter.",
     "Remove it or add the corresponding opening delimiter."},
    {"unclosed_group", "Unfinished delimiter group", "This opening parenthesis or bracket has not been closed yet.",
     "Add the matching closing delimiter before formatting or completing the statement."},
    {"unsupported_payload", "ClickHouse INSERT FORMAT payload is not SQL",
     "Bytes after INSERT ... FORMAT belong to a data format such as CSV or JSON, not to the SQL lexer.",
     "Separate the SQL header from its payload; process the payload with the appropriate data-format tool."},
    {"unsupported_layout", "SQL layout cannot be formatted",
     "Lexical input is readable, but the formatter cannot safely lay out this construct. This is not a database syntax "
     "verdict.",
     "Check CASE/END pairs or highlight the SQL without formatting it."},
    {"output_limit", "Output buffer is too small",
     "The transformed text needs more space than the supplied output buffer.",
     "Measure with out=NULL and capacity=0, then allocate length+1 bytes."},
    {"out_of_memory", "Cannot allocate working memory",
     "The library could not allocate its parsing or rendering buffers.",
     "Reduce the input size or retry after freeing memory."},
    {"resource_limit", "Analysis resource budget exceeded",
     "The per-call memory, output or work budget stopped analysis.",
     "Increase the relevant budget or analyze a smaller document."},
    {"cancelled", "Analysis cancelled", "The caller cancelled SQL analysis before it completed.",
     "Retry only if analysis is still needed."},
    {"ambiguous_literal", "Ambiguous literal syntax",
     "Dialect or escape modes disagree about literal boundaries. Analysis omitted SQL to avoid exposing fragments.",
     "Use the correct dialect and an unambiguous supported quote form."},
    {"empty_statement", "Empty SQL statement", "A statement boundary contains no executable tokens.",
     "Remove the empty statement or keep it only when the target driver explicitly permits it."},
    {"multiple_statements", "Multiple SQL statements", "The input contains more than one nonempty statement.",
     "Split statements before execution when the driver expects one statement per call."},
    {"mixed_operations", "Mixed SQL operations", "The document combines different top-level SQL operations.",
     "Analyze or execute each operation as a separate statement."},
    {"select_star", "Wildcard projection", "SELECT * couples the query to the current table shape.",
     "Name the required columns explicitly when a stable result contract matters."},
    {"unbounded_write", "Unbounded write", "UPDATE or DELETE has no detectable WHERE clause.",
     "Add an explicit predicate or require an intentional full-table operation."},
    {"large_in_list", "Large IN list", "The query contains a large literal or bind list.",
     "Prefer a temporary table, array parameter, or bulk join for large inputs."},
    {"deep_query_nesting", "Deep query nesting", "Nested expressions exceed the conservative query-shape threshold.",
     "Split the query into named CTEs or smaller operations."},
    {"excessive_joins", "Many joins",
     "The query contains many JOIN clauses and may be difficult to maintain or optimize.",
     "Review join selectivity and consider a purpose-built intermediate relation."},
    {"repeated_predicate", "Repeated predicate", "The same predicate token sequence appears more than once.",
     "Factor the predicate into a CTE or a named condition when practical."},
    {"unknown_operation", "Unknown SQL operation", "No recognized top-level SQL operation was found.",
     "Use a supported statement form or provide an application-owned operation name."},
    {"no_referenced_table", "No referenced table", "The statement has no detectable table reference.",
     "Confirm that the statement is intentionally relation-free or use an explicit source."},
    {"dialect_portability", "Dialect portability warning",
     "The query uses syntax that may not be portable to the selected dialect.",
     "Verify the construct against every target database dialect."},
    {"sqlc_annotation", "Malformed SQLC annotation",
     "A SQLC name annotation does not have the expected directive form.",
     "Use a complete -- name: Operation :one|:many|:exec annotation."}};

const char *sequel_diagnostic_code_name(sequel_diagnostic_code_t code) {
  return code >= 0 && (size_t)code < sizeof(diagnostic_messages) / sizeof(diagnostic_messages[0])
             ? diagnostic_messages[code].name
             : NULL;
}
static void diagnostic_position(const char *sql, size_t n, size_t offset, size_t *line, size_t *column) {
  *line = *column = 1;
  if (!sql)
    return;
  if (offset > n)
    offset = n;
  for (size_t i = 0; (sequel_work_tick(1), i < offset); ++i) {
    unsigned char c = (unsigned char)sql[i];
    if (c == '\r') {
      ++*line;
      *column = 1;
      if (i + 1 < offset && sql[i + 1] == '\n')
        ++i;
    } else if (c == '\n') {
      ++*line;
      *column = 1;
    } else if ((c & 0xc0) != 0x80)
      ++*column;
  }
}
static void diagnostic_report(const char *sql, size_t n, sequel_diagnostic_t *out, sequel_diagnostic_code_t code,
                              size_t start, size_t end) {
  if (!out)
    return;
  memset(out, 0, sizeof(*out));
  out->code = code;
  out->incomplete = code >= SEQUEL_DIAG_UNTERMINATED_STRING && code <= SEQUEL_DIAG_UNTERMINATED_PARAMETER;
  out->incomplete |= code == SEQUEL_DIAG_UNCLOSED_GROUP;
  out->severity = code == SEQUEL_DIAG_NONE              ? SEQUEL_NOTE
                  : code >= SEQUEL_DIAG_EMPTY_STATEMENT ? SEQUEL_WARNING
                  : out->incomplete                     ? SEQUEL_WARNING
                                                        : SEQUEL_ERROR;
  out->offset = start;
  out->end_offset = end;
  diagnostic_position(sql, n, start, &out->line, &out->column);
  diagnostic_position(sql, n, end, &out->end_line, &out->end_column);
  if (sequel_diagnostic_code_name(code)) {
    snprintf(out->message, sizeof(out->message), "%s", diagnostic_messages[code].message);
    snprintf(out->explanation, sizeof(out->explanation), "%s", diagnostic_messages[code].explanation);
    snprintf(out->hint, sizeof(out->hint), "%s", diagnostic_messages[code].hint);
  }
}
static sequel_status_t diagnostic_input(const char *sql, size_t n, sequel_dialect_t dialect, sequel_diagnostic_t *out) {
  diagnostic_report(sql, 0, out, SEQUEL_DIAG_NONE, 0, 0);
  if (!sql) {
    diagnostic_report(NULL, 0, out, SEQUEL_DIAG_NULL_INPUT, 0, 0);
    return SEQUEL_ERR_NULL;
  }
  if (dialect < SEQUEL_GENERIC || dialect > SEQUEL_CLICKHOUSE) {
    diagnostic_report(sql, n, out, SEQUEL_DIAG_INVALID_ARGUMENT, 0, 0);
    return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  if (n > INPUT_MAX) {
    diagnostic_report(sql, INPUT_MAX, out, SEQUEL_DIAG_INPUT_LIMIT, INPUT_MAX, INPUT_MAX);
    return SEQUEL_ERR_OVERFLOW;
  }
  for (size_t i = 0; (sequel_work_tick(1), i < n);) {
    unsigned char c = (unsigned char)sql[i];
    size_t width = c < 0x80                 ? 1
                   : c >= 0xc2 && c <= 0xdf ? 2
                   : c >= 0xe0 && c <= 0xef ? 3
                   : c >= 0xf0 && c <= 0xf4 ? 4
                                            : 0;
    if (!width || width > n - i || !sequel_utf8_valid(sql + i, width)) {
      diagnostic_report(sql, n, out, SEQUEL_DIAG_INVALID_UTF8, i, i + 1);
      return SEQUEL_ERR_INVALID_ARGUMENT;
    }
    if ((c < 32 && c != '\t' && c != '\r' && c != '\n') || c == 127 ||
        (width == 2 && c == 0xc2 && (unsigned char)sql[i + 1] <= 0x9f)) {
      diagnostic_report(sql, n, out, SEQUEL_DIAG_CONTROL_CHARACTER, i, i + width);
      return SEQUEL_ERR_INVALID_ARGUMENT;
    }
    i += width;
  }
  return SEQUEL_OK;
}
sequel_status_t sequel_diagnose_n(const char *sql, size_t n, sequel_dialect_t dialect, sequel_diagnostic_t *out) {
  if (!out)
    return SEQUEL_ERR_NULL;
  sequel_status_t status = diagnostic_input(sql, n, dialect, out);
  if (status != SEQUEL_OK)
    return status;
  size_t nesting = 0;
  for (size_t i = 0; i < n; ++i) {
    if (sql[i] == '(' && ++nesting > 64) {
      diagnostic_report(sql, n, out, SEQUEL_DIAG_NESTING_LIMIT, i, i + 1);
      return SEQUEL_ERR_OVERFLOW;
    }
    if (sql[i] == ')' && nesting)
      --nesting;
  }
  size_t comment_depth = 0;
  for (size_t i = 0; i + 1 < n; ++i) {
    if (sql[i] == '/' && sql[i + 1] == '*') {
      if (++comment_depth > 64) {
        diagnostic_report(sql, n, out, SEQUEL_DIAG_NESTING_LIMIT, i, i + 2);
        return SEQUEL_ERR_OVERFLOW;
      }
      ++i;
      continue;
    }
    if (sql[i] == '*' && sql[i + 1] == '/' && comment_depth) {
      --comment_depth;
      ++i;
    }
  }
  if (dialect == SEQUEL_CLICKHOUSE && strstr(sql, " FORMAT ")) {
    const char *format = strstr(sql, " FORMAT ") + 1;
    diagnostic_report(sql, n, out, SEQUEL_DIAG_UNSUPPORTED_PAYLOAD, (size_t)(format - sql), (size_t)(format - sql) + 6);
    return SEQUEL_ERR_INVALID_ARGUMENT;
  }
  sequel_parse_options_t options;
  sequel_parse_options_init(&options);
  options.dialect = dialect;
  options.retain_comments = true;
  sequel_source_view_t source = {sql, n};
  sequel_syntax_document_t *document = NULL;
  status = sequel_parse_sql(&source, &options, NULL, &document);
  if (status != SEQUEL_OK)
    return status;
  size_t issue_count = 0;
  sequel_syntax_issues(document, &issue_count);
  if (issue_count) {
    status = sequel_syntax_diagnostic(document, 0, out);
    sequel_syntax_document_free(document);
    return status == SEQUEL_OK ? SEQUEL_ERR_INVALID_ARGUMENT : status;
  }
  const sequel_lexed_document_t *lexed = sequel_syntax_lexed(document);
  size_t token_count = 0;
  const sequel_token_t *tokens = sequel_lexed_tokens(lexed, &token_count);
  for (size_t i = token_count; i > 0; --i) {
    if (tokens[i - 1].kind == SEQUEL_TOKEN_EOF || tokens[i - 1].kind == SEQUEL_TOKEN_WHITESPACE ||
        tokens[i - 1].kind == SEQUEL_TOKEN_LINE_COMMENT || tokens[i - 1].kind == SEQUEL_TOKEN_BLOCK_COMMENT)
      continue;
    out->statement_complete =
        tokens[i - 1].kind == SEQUEL_TOKEN_PUNCTUATION && source.data[tokens[i - 1].range.start] == ';';
    break;
  }
  sequel_syntax_document_free(document);
  return SEQUEL_OK;
}
sequel_status_t sequel_diagnose(const char *sql, sequel_dialect_t dialect, sequel_diagnostic_t *out) {
  size_t n = 0;
  if (sql)
    while (n <= INPUT_MAX && sql[n])
      ++n;
  return sequel_diagnose_n(sql, n, dialect, out);
}
sequel_status_t sequel_format_ex(const char *sql, sequel_dialect_t dialect, char *out, size_t capacity, size_t *length,
                                 sequel_diagnostic_t *diagnostic) {
  sequel_diagnostic_t local;
  if (!diagnostic)
    diagnostic = &local;
  if (out && capacity)
    *out = 0;
  if (length)
    *length = 0;
  sequel_status_t status = sequel_diagnose(sql, dialect, diagnostic);
  if (status != SEQUEL_OK)
    return status;
  status = sequel_format(sql, dialect, out, capacity, length);
  if (status != SEQUEL_OK) {
    sequel_diagnostic_code_t code = status == SEQUEL_ERR_NOMEM      ? SEQUEL_DIAG_OUT_OF_MEMORY
                                    : status == SEQUEL_ERR_OVERFLOW ? SEQUEL_DIAG_OUTPUT_LIMIT
                                    : !length || (!out && capacity) || (out && !capacity)
                                        ? SEQUEL_DIAG_INVALID_ARGUMENT
                                        : SEQUEL_DIAG_UNSUPPORTED_LAYOUT;
    diagnostic_report(sql, strlen(sql), diagnostic, code, 0, 0);
  }
  return status;
}
