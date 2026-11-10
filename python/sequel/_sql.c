#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <sequel.h>
#include <string.h>

static PyObject *diagnostic_dict(const sequel_diagnostic_t *d) {
  return Py_BuildValue(
      "{s:s,s:s,s:n,s:n,s:n,s:n,s:n,s:n,s:O,s:O,s:s,s:s,s:s}", "code", sequel_diagnostic_code_name(d->code), "severity",
      d->severity == SEQUEL_ERROR     ? "error"
      : d->severity == SEQUEL_WARNING ? "warning"
                                      : "note",
      "offset", (Py_ssize_t)d->offset, "end_offset", (Py_ssize_t)d->end_offset, "line", (Py_ssize_t)d->line, "column",
      (Py_ssize_t)d->column, "end_line", (Py_ssize_t)d->end_line, "end_column", (Py_ssize_t)d->end_column, "incomplete",
      d->incomplete ? Py_True : Py_False, "statement_complete", d->statement_complete ? Py_True : Py_False, "message",
      d->message, "explanation", d->explanation, "hint", d->hint);
}

static PyObject *render_result(const char *text, size_t length, const sequel_diagnostic_t *d) {
  PyObject *value = text ? PyUnicode_DecodeUTF8(text, (Py_ssize_t)length, "strict") : Py_NewRef(Py_None);
  if (!value)
    return NULL;
  PyObject *diagnostic = diagnostic_dict(d);
  if (!diagnostic) {
    Py_DECREF(value);
    return NULL;
  }
  return Py_BuildValue("(NN)", value, diagnostic);
}

static PyObject *diagnose_sql(PyObject *self, PyObject *args) {
  const char *sql;
  Py_ssize_t n;
  int dialect;
  if (!PyArg_ParseTuple(args, "s#i", &sql, &n, &dialect))
    return NULL;
  sequel_diagnostic_t d;
  sequel_diagnose_n(sql, (size_t)n, (sequel_dialect_t)dialect, &d);
  return diagnostic_dict(&d);
}

static PyObject *inspect_sql(PyObject *self, PyObject *args) {
  const char *sql;
  int dialect, omit, keep;
  Py_ssize_t limit;
  if (!PyArg_ParseTuple(args, "sinpp", &sql, &dialect, &limit, &omit, &keep))
    return NULL;
  if (limit < 0 || limit > SEQUEL_TEXT_MAX) {
    PyErr_SetString(PyExc_ValueError, "query text limit must be between 0 and SEQUEL_TEXT_MAX (1 GiB)");
    return NULL;
  }
  sequel_options_t options = {0};
  options.dialect = (sequel_dialect_t)dialect;
  options.max_query_text_bytes = (size_t)limit;
  options.omit_query_text = omit;
  options.keep_in_lists = keep;
  sequel_metadata_t *metadata = PyMem_Malloc(sizeof(*metadata));
  if (!metadata)
    return PyErr_NoMemory();
  sequel_status_t status = sequel_inspect(sql, &options, metadata);
  if (status != SEQUEL_OK) {
    sequel_metadata_free(metadata);
    PyMem_Free(metadata);
    PyErr_SetString(PyExc_ValueError, "SQL inspection rejected input");
    return NULL;
  }
  PyObject *result = Py_BuildValue("sssOn", metadata->text, metadata->summary, metadata->operation,
                                   metadata->truncated ? Py_True : Py_False, (Py_ssize_t)metadata->statement_count);
  sequel_metadata_free(metadata);
  PyMem_Free(metadata);
  return result;
}

static PyObject *format_sql(PyObject *self, PyObject *args) {
  const char *sql;
  Py_ssize_t n;
  int dialect;
  if (!PyArg_ParseTuple(args, "s#i", &sql, &n, &dialect))
    return NULL;
  size_t length = 0;
  sequel_diagnostic_t diagnostic;
  if (memchr(sql, 0, (size_t)n)) {
    sequel_diagnose_n(sql, (size_t)n, (sequel_dialect_t)dialect, &diagnostic);
    return render_result(NULL, 0, &diagnostic);
  }
  if (sequel_format_ex(sql, (sequel_dialect_t)dialect, NULL, 0, &length, &diagnostic) != SEQUEL_OK)
    return render_result(NULL, 0, &diagnostic);
  char *out = PyMem_Malloc(length + 1);
  if (!out)
    return PyErr_NoMemory();
  sequel_status_t status = sequel_format_ex(sql, (sequel_dialect_t)dialect, out, length + 1, &length, &diagnostic);
  PyObject *result = render_result(status == SEQUEL_OK ? out : NULL, length, &diagnostic);
  PyMem_Free(out);
  return result;
}

static PyObject *sanitize_sql(PyObject *self, PyObject *args) {
  const char *sql;
  int dialect, remove_comments, replace_literals;
  if (!PyArg_ParseTuple(args, "sipp", &sql, &dialect, &remove_comments, &replace_literals))
    return NULL;
  size_t n = strlen(sql);
  if (n > SEQUEL_INPUT_MAX) {
    PyErr_SetString(PyExc_ValueError, "SQL exceeds SEQUEL_INPUT_MAX (1 GiB)");
    return NULL;
  }
  char *out = PyMem_Malloc(n + 2);
  if (!out)
    return PyErr_NoMemory();
  unsigned flags = (remove_comments ? SEQUEL_REMOVE_COMMENTS : 0) | (replace_literals ? SEQUEL_REPLACE_LITERALS : 0);
  bool truncated;
  sequel_status_t status =
      sequel_sanitize_with_flags(sql, (sequel_dialect_t)dialect, flags, true, n + 1, out, n + 2, &truncated);
  PyObject *result = NULL;
  if (status == SEQUEL_OK && !truncated)
    result = PyUnicode_DecodeUTF8(out, (Py_ssize_t)strlen(out), "strict");
  else
    PyErr_SetString(PyExc_ValueError, "SQL sanitization rejected input");
  PyMem_Free(out);
  return result;
}

static PyObject *highlight_sql(PyObject *self, PyObject *args) {
  const char *sql, *name, *yaml;
  Py_ssize_t n;
  int dialect, mode, appearance, tolerant;
  PyObject *colors;
  if (!PyArg_ParseTuple(args, "s#iissiO!p", &sql, &n, &dialect, &mode, &name, &yaml, &appearance, &PyDict_Type, &colors,
                        &tolerant))
    return NULL;
  sequel_highlight_options_t options;
  sequel_highlight_options_init(&options);
  options.dialect = (sequel_dialect_t)dialect;
  options.mode = (sequel_highlight_mode_t)mode;
  options.tolerant = tolerant;
  sequel_colorscheme_t palette;
  sequel_status_t status = *yaml ? sequel_colorscheme_parse(yaml, &palette) : sequel_colorscheme(name, &palette);
  if (status == SEQUEL_OK)
    status = *yaml ? sequel_colorscheme_set_appearance(&palette, (sequel_appearance_t)appearance)
                   : sequel_colorscheme_for_appearance(name, (sequel_appearance_t)appearance, &palette);
  if (status != SEQUEL_OK || sequel_highlight_set_colorscheme(&options, &palette) != SEQUEL_OK) {
    PyErr_SetString(PyExc_ValueError, "Invalid colorscheme or no published variant for the requested appearance");
    return NULL;
  }
  Py_ssize_t pos = 0;
  PyObject *key, *value;
  while (PyDict_Next(colors, &pos, &key, &value)) {
    const char *type = PyUnicode_AsUTF8(key), *hex = PyUnicode_AsUTF8(value);
    if (!type || !hex)
      return NULL;
    if ((Py_ssize_t)strlen(type) != PyUnicode_GET_LENGTH(key) ||
        (Py_ssize_t)strlen(hex) != PyUnicode_GET_LENGTH(value) ||
        sequel_highlight_set_color(&options, type, hex) != SEQUEL_OK) {
      PyErr_SetString(PyExc_ValueError, "Invalid token type or color (expected #RRGGBB)");
      return NULL;
    }
  }
  size_t length = 0;
  sequel_diagnostic_t diagnostic;
  if (memchr(sql, 0, (size_t)n)) {
    sequel_diagnose_n(sql, (size_t)n, (sequel_dialect_t)dialect, &diagnostic);
    return render_result(NULL, 0, &diagnostic);
  }
  if (sequel_highlight_ex(sql, &options, NULL, 0, &length, &diagnostic) != SEQUEL_OK)
    return render_result(NULL, 0, &diagnostic);
  char *out = PyMem_Malloc(length + 1);
  if (!out)
    return PyErr_NoMemory();
  status = sequel_highlight_ex(sql, &options, out, length + 1, &length, &diagnostic);
  PyObject *result = render_result(status == SEQUEL_OK ? out : NULL, length, &diagnostic);
  PyMem_Free(out);
  return result;
}

static PyObject *colorschemes(PyObject *self, PyObject *unused) {
  PyObject *result = PyTuple_New((Py_ssize_t)sequel_colorscheme_count());
  if (!result)
    return NULL;
  for (size_t i = 0; i < sequel_colorscheme_count(); ++i) {
    PyObject *name = PyUnicode_FromString(sequel_colorscheme_name(i));
    if (!name) {
      Py_DECREF(result);
      return NULL;
    }
    PyTuple_SET_ITEM(result, (Py_ssize_t)i, name);
  }
  return result;
}

static PyObject *parse_document_sql(PyObject *self, PyObject *args) {
  const char *sql;
  Py_ssize_t length;
  int dialect;
  if (!PyArg_ParseTuple(args, "s#i", &sql, &length, &dialect))
    return NULL;
  sequel_source_view_t source = {sql, (size_t)length};
  sequel_parse_options_t options;
  sequel_parse_options_init(&options);
  options.dialect = (sequel_dialect_t)dialect;
  sequel_syntax_document_t *syntax = NULL;
  sequel_status_t status = sequel_parse_sql(&source, &options, NULL, &syntax);
  if (status != SEQUEL_OK) {
    PyErr_Format(PyExc_ValueError, "SQL parse failed with status %d", status);
    return NULL;
  }
  sequel_query_document_t *query = NULL;
  status = sequel_build_query_document(syntax, &options, &query);
  if (status != SEQUEL_OK) {
    sequel_syntax_document_free(syntax);
    PyErr_Format(PyExc_ValueError, "query indexing failed with status %d", status);
    return NULL;
  }
  PyObject *result = PyDict_New();
  PyObject *tokens = PyList_New(0), *statements = PyList_New(0), *relations = PyList_New(0), *issues = PyList_New(0);
  PyObject *diagnostics = PyList_New(0);
  sequel_diagnostic_list_t lexical_diagnostics = {0}, syntax_diagnostics = {0}, query_diagnostics = {0};
  if (!result || !tokens || !statements || !relations || !issues || !diagnostics)
    goto error;
  size_t token_count = 0;
  const sequel_token_t *token_values = sequel_lexed_tokens(sequel_syntax_lexed(syntax), &token_count);
  for (size_t i = 0; i < token_count; ++i) {
    PyObject *value =
        Py_BuildValue("{s:i,s:I,s:n,s:n}", "kind", token_values[i].kind, "flags", token_values[i].flags, "start",
                      (Py_ssize_t)token_values[i].range.start, "end", (Py_ssize_t)token_values[i].range.end);
    if (!value || PyList_Append(tokens, value) < 0) {
      Py_XDECREF(value);
      goto error;
    }
    Py_DECREF(value);
  }
  size_t statement_count = sequel_query_statement_count(query);
  const sequel_statement_info_t *statement_values = sequel_query_statements(query);
  for (size_t i = 0; i < statement_count; ++i) {
    PyObject *value =
        Py_BuildValue("{s:i,s:n,s:n}", "operation", statement_values[i].operation, "start",
                      (Py_ssize_t)statement_values[i].range.start, "end", (Py_ssize_t)statement_values[i].range.end);
    if (!value || PyList_Append(statements, value) < 0) {
      Py_XDECREF(value);
      goto error;
    }
    Py_DECREF(value);
  }
  size_t relation_count = sequel_query_relation_count(query);
  const sequel_relation_info_t *relation_values = sequel_query_relations(query);
  for (size_t i = 0; i < relation_count; ++i) {
    PyObject *value = Py_BuildValue(
        "{s:i,s:n,s:n,s:n,s:n}", "kind", relation_values[i].kind, "start", (Py_ssize_t)relation_values[i].range.start,
        "end", (Py_ssize_t)relation_values[i].range.end, "name_start", (Py_ssize_t)relation_values[i].name_range.start,
        "name_end", (Py_ssize_t)relation_values[i].name_range.end);
    if (!value || PyList_Append(relations, value) < 0) {
      Py_XDECREF(value);
      goto error;
    }
    Py_DECREF(value);
  }
  size_t issue_count = 0;
  const sequel_parse_issue_t *issue_values = sequel_syntax_issues(syntax, &issue_count);
  for (size_t i = 0; i < issue_count; ++i) {
    PyObject *value =
        Py_BuildValue("{s:i,s:i,s:n,s:n}", "code", issue_values[i].code, "recovery", issue_values[i].recovery, "start",
                      (Py_ssize_t)issue_values[i].range.start, "end", (Py_ssize_t)issue_values[i].range.end);
    if (!value || PyList_Append(issues, value) < 0) {
      Py_XDECREF(value);
      goto error;
    }
    Py_DECREF(value);
  }
  if (sequel_collect_lexical_diagnostics(sequel_syntax_lexed(syntax), &lexical_diagnostics) != SEQUEL_OK ||
      sequel_collect_syntax_diagnostics(syntax, &syntax_diagnostics) != SEQUEL_OK ||
      sequel_collect_query_diagnostics(query, &query_diagnostics) != SEQUEL_OK)
    goto error;
  sequel_diagnostic_list_t *diagnostic_lists[] = {&lexical_diagnostics, &syntax_diagnostics, &query_diagnostics};
  for (size_t list_index = 0; list_index < sizeof(diagnostic_lists) / sizeof(diagnostic_lists[0]); ++list_index) {
    sequel_diagnostic_list_t *list = diagnostic_lists[list_index];
    for (size_t i = 0; i < list->count; ++i) {
      PyObject *value =
          Py_BuildValue("{s:i,s:i,s:i,s:n,s:n}", "origin", list->records[i].origin, "code", list->records[i].code,
                        "severity", list->records[i].severity, "start", (Py_ssize_t)list->records[i].range.start, "end",
                        (Py_ssize_t)list->records[i].range.end);
      if (!value || PyList_Append(diagnostics, value) < 0) {
        Py_XDECREF(value);
        goto error;
      }
      Py_DECREF(value);
    }
  }
  sequel_diagnostic_list_free(&lexical_diagnostics);
  sequel_diagnostic_list_free(&syntax_diagnostics);
  sequel_diagnostic_list_free(&query_diagnostics);
  sequel_summary_options_t summary_options;
  sequel_summary_options_init(&summary_options);
  sequel_text_result_t summary = {0};
  char fingerprint[SEQUEL_FINGERPRINT_LENGTH];
  if (sequel_summarize_query(query, &summary_options, &summary) != SEQUEL_OK ||
      sequel_fingerprint_document(syntax, query, SEQUEL_FINGERPRINT_QUERY_SHAPE, fingerprint) != SEQUEL_OK)
    goto error;
  PyObject *summary_value = PyUnicode_DecodeUTF8(summary.data, (Py_ssize_t)summary.length, "strict");
  PyObject *fingerprint_value = PyUnicode_FromString(fingerprint);
  sequel_text_result_free(&summary);
  if (!summary_value || !fingerprint_value || PyDict_SetItemString(result, "summary", summary_value) < 0 ||
      PyDict_SetItemString(result, "fingerprint", fingerprint_value) < 0 ||
      PyDict_SetItemString(result, "diagnostics", diagnostics) < 0) {
    Py_XDECREF(summary_value);
    Py_XDECREF(fingerprint_value);
    goto error;
  }
  Py_DECREF(summary_value);
  Py_DECREF(fingerprint_value);
  if (PyDict_SetItemString(result, "tokens", tokens) < 0 ||
      PyDict_SetItemString(result, "statements", statements) < 0 ||
      PyDict_SetItemString(result, "relations", relations) < 0 || PyDict_SetItemString(result, "issues", issues) < 0)
    goto error;
  Py_DECREF(tokens);
  Py_DECREF(statements);
  Py_DECREF(relations);
  Py_DECREF(issues);
  Py_DECREF(diagnostics);
  sequel_query_document_free(query);
  sequel_syntax_document_free(syntax);
  return result;
error:
  sequel_diagnostic_list_free(&lexical_diagnostics);
  sequel_diagnostic_list_free(&syntax_diagnostics);
  sequel_diagnostic_list_free(&query_diagnostics);
  Py_XDECREF(result);
  Py_XDECREF(tokens);
  Py_XDECREF(statements);
  Py_XDECREF(relations);
  Py_XDECREF(issues);
  Py_XDECREF(diagnostics);
  sequel_query_document_free(query);
  sequel_syntax_document_free(syntax);
  if (!PyErr_Occurred())
    PyErr_NoMemory();
  return NULL;
}

#include "_analysis.c"
static PyMethodDef methods[] = {
    {"analyze", analyze_sql, METH_VARARGS, "Analyze SQL with bounded work and privacy-safe metadata."},
    {"diagnostic_catalog", analysis_catalog, METH_NOARGS, "List canonical public diagnostics."},
    {"canonicalize_diagnostic", analysis_canonicalize, METH_VARARGS, "Validate and canonicalize diagnostic transport."},
    {"map_range", analysis_map_range, METH_VARARGS, "Map a UTF-8 range through numeric provenance."},
    {"utf8_to_utf16", analysis_utf8_to_utf16, METH_VARARGS, "Convert UTF-8 byte boundaries to UTF-16 units."},
    {"utf16_to_utf8", analysis_utf16_to_utf8, METH_VARARGS, "Convert UTF-16 unit boundaries to UTF-8 bytes."},
    {"check_binds", analysis_check_binds, METH_VARARGS, "Check bind slots without reading values."},
    {"diagnose", diagnose_sql, METH_VARARGS, "Locate and explain the first lexical/delimiter issue."},
    {"highlight", highlight_sql, METH_VARARGS, "Highlight SQL as escaped HTML or truecolor ANSI."},
    {"colorschemes", colorschemes, METH_NOARGS, "List bundled Base16 names."},
    {"sanitize", sanitize_sql, METH_VARARGS, "Selectively remove comments and replace literals."},
    {"inspect", inspect_sql, METH_VARARGS, "Inspect using the shared C engine."},
    {"format", format_sql, METH_VARARGS, "Format without sanitizing."},
    {"parse_document", parse_document_sql, METH_VARARGS,
     "Parse into shared token, statement, relation and issue records."},
    {NULL, NULL, 0, NULL}};
static struct PyModuleDef module = {PyModuleDef_HEAD_INIT, "_sql", NULL, -1, methods};

PyMODINIT_FUNC PyInit__sql(void) {
  PyObject *result = PyModule_Create(&module);
  if (!result)
    return NULL;
  if (PyModule_AddIntConstant(result, "MAX_INPUT_BYTES", SEQUEL_INPUT_MAX) < 0 ||
      PyModule_AddIntConstant(result, "DEFAULT_MAX_QUERY_TEXT_BYTES", SEQUEL_TEXT_MAX) < 0 ||
      PyModule_AddIntConstant(result, "DEFAULT_MAX_SUMMARY_BYTES", SEQUEL_SUMMARY_MAX) < 0) {
    Py_DECREF(result);
    return NULL;
  }
  return result;
}
