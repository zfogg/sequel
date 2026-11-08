/* Python wrappers for the versioned, privacy-safe native analysis contract. */
#include <limits.h>
static int analysis_set(PyObject *dict, const char *key, PyObject *value) {
  if (!value)
    return -1;
  int status = PyDict_SetItemString(dict, key, value);
  Py_DECREF(value);
  return status;
}
static PyObject *analysis_map_dict(const sequel_source_map_t *map) {
  PyObject *result = PyDict_New(), *segments = PyList_New((Py_ssize_t)map->count);
  if (!result || !segments) {
    Py_XDECREF(result);
    Py_XDECREF(segments);
    return NULL;
  }
  static const char *names[] = {"copy", "replace", "delete", "insert"};
  for (size_t i = 0; i < map->count; ++i) {
    const sequel_map_segment_t *s = &map->segments[i];
    PyObject *entry = Py_BuildValue("{s:n,s:n,s:n,s:n,s:s}", "from_start", (Py_ssize_t)s->from_start, "from_end",
                                    (Py_ssize_t)s->from_end, "to_start", (Py_ssize_t)s->to_start, "to_end",
                                    (Py_ssize_t)s->to_end, "kind", names[s->kind]);
    if (!entry) {
      Py_DECREF(result);
      Py_DECREF(segments);
      return NULL;
    }
    PyList_SET_ITEM(segments, (Py_ssize_t)i, entry);
  }
  if (analysis_set(result, "from_length", PyLong_FromSize_t(map->from_length)) < 0 ||
      analysis_set(result, "to_length", PyLong_FromSize_t(map->to_length)) < 0) {
    Py_DECREF(segments);
    Py_DECREF(result);
    return NULL;
  }
  if (analysis_set(result, "segments", segments) < 0) {
    Py_DECREF(result);
    return NULL;
  }
  return result;
}
static PyObject *analysis_dict(const sequel_analysis_t *a) {
  PyObject *result = PyDict_New();
  if (!result)
    return NULL;
#define ANALYSIS_FIELD(key, value)                                                                                     \
  if (analysis_set(result, key, value) < 0)                                                                            \
  goto failed
  ANALYSIS_FIELD("schema_version", PyLong_FromUnsignedLong(a->schema_version));
  ANALYSIS_FIELD("dialect", PyLong_FromLong(a->dialect));
  ANALYSIS_FIELD("status", PyUnicode_FromString(a->status));
  ANALYSIS_FIELD("omission_reason", PyUnicode_FromString(a->omission_reason));
  ANALYSIS_FIELD("text", PyUnicode_FromString(a->text));
  ANALYSIS_FIELD("summary", PyUnicode_FromString(a->summary));
  ANALYSIS_FIELD("formatted", PyUnicode_FromString(a->formatted));
  ANALYSIS_FIELD("operation", PyUnicode_FromString(a->operation));
  ANALYSIS_FIELD("fingerprint", PyUnicode_FromString(a->fingerprint));
  ANALYSIS_FIELD("truncated", PyBool_FromLong(a->truncated));
  ANALYSIS_FIELD("original_length", PyLong_FromSize_t(a->original_length));
  ANALYSIS_FIELD("statement_count", PyLong_FromSize_t(a->statement_count));
  ANALYSIS_FIELD("memory_peak_bytes", PyLong_FromSize_t(a->memory_peak_bytes));
  ANALYSIS_FIELD("work_units", PyLong_FromSize_t(a->work_units));
  ANALYSIS_FIELD("original_to_sanitized", analysis_map_dict(&a->original_to_sanitized));
  ANALYSIS_FIELD("sanitized_to_formatted", analysis_map_dict(&a->sanitized_to_formatted));
  PyObject *diagnostics = PyList_New((Py_ssize_t)a->diagnostic_count);
  if (!diagnostics)
    goto failed;
  for (size_t i = 0; i < a->diagnostic_count; ++i) {
    PyObject *entry = diagnostic_dict(&a->diagnostics[i]);
    if (!entry) {
      Py_DECREF(diagnostics);
      goto failed;
    }
    PyList_SET_ITEM(diagnostics, (Py_ssize_t)i, entry);
  }
  ANALYSIS_FIELD("diagnostics", diagnostics);
  PyObject *binds = PyList_New((Py_ssize_t)a->bind_count);
  if (!binds)
    goto failed;
  static const char *styles[] = {"positional", "numbered", "named", "typed"};
  for (size_t i = 0; i < a->bind_count; ++i) {
    const sequel_bind_descriptor_t *b = &a->binds[i];
    PyObject *entry =
        Py_BuildValue("{s:n,s:n,s:n,s:n,s:n,s:s}", "ordinal", (Py_ssize_t)b->ordinal, "slot", (Py_ssize_t)b->slot,
                      "statement_index", (Py_ssize_t)b->statement_index, "offset", (Py_ssize_t)b->offset, "end_offset",
                      (Py_ssize_t)b->end_offset, "style", styles[b->style]);
    if (!entry) {
      Py_DECREF(binds);
      goto failed;
    }
    PyList_SET_ITEM(binds, (Py_ssize_t)i, entry);
  }
  ANALYSIS_FIELD("binds", binds);
  PyObject *statements = PyList_New((Py_ssize_t)a->statement_count);
  if (!statements)
    goto failed;
  for (size_t i = 0; i < a->statement_count; ++i) {
    PyObject *entry = Py_BuildValue("{s:n,s:n}", "offset", (Py_ssize_t)a->statements[i].offset, "end_offset",
                                    (Py_ssize_t)a->statements[i].end_offset);
    if (!entry) {
      Py_DECREF(statements);
      goto failed;
    }
    PyList_SET_ITEM(statements, (Py_ssize_t)i, entry);
  }
  ANALYSIS_FIELD("statements", statements);
#undef ANALYSIS_FIELD
  return result;
failed:
  Py_DECREF(result);
  return NULL;
}
static bool analysis_cancel(void *userdata) {
  PyObject *callback = (PyObject *)userdata;
  if (PyErr_Occurred())
    return true;
  PyObject *value = PyObject_CallNoArgs(callback);
  if (!value)
    return true;
  int result = PyObject_IsTrue(value);
  Py_DECREF(value);
  return result != 0;
}
static PyObject *analyze_sql(PyObject *self, PyObject *args) {
  const char *sql;
  Py_ssize_t length;
  int dialect, omit, format, keep, omit_maps, omit_binds, include_lints;
  PyObject *input_limit, *output_limit, *memory_limit, *work_limit, *query_limit, *callback;
  if (!PyArg_ParseTuple(args, "s#iOOOOOppppppO", &sql, &length, &dialect, &input_limit, &output_limit, &memory_limit,
                        &work_limit, &query_limit, &omit, &format, &keep, &omit_maps, &omit_binds, &include_lints,
                        &callback))
    return NULL;
  sequel_analysis_options_t options = {0};
  options.dialect = (sequel_dialect_t)dialect;
  options.max_input_bytes = PyLong_AsSize_t(input_limit);
  options.max_output_bytes = PyLong_AsSize_t(output_limit);
  options.max_memory_bytes = PyLong_AsSize_t(memory_limit);
  options.max_work_units = PyLong_AsSize_t(work_limit);
  options.max_query_text_bytes = PyLong_AsSize_t(query_limit);
  if (PyErr_Occurred())
    return NULL;
  options.omit_query_text = omit;
  options.format = format;
  options.keep_in_lists = keep;
  options.omit_source_maps = omit_maps;
  options.omit_bind_descriptors = omit_binds;
  options.include_lints = include_lints;
  if (callback != Py_None) {
    if (!PyCallable_Check(callback)) {
      PyErr_SetString(PyExc_TypeError, "cancelled must be a callable or None");
      return NULL;
    }
    options.cancelled = analysis_cancel;
    options.cancel_userdata = callback;
  }
  sequel_analysis_t result = {0};
  sequel_analyze_n(sql, (size_t)length, &options, &result);
  PyObject *value = PyErr_Occurred() ? NULL : analysis_dict(&result);
  sequel_analysis_free(&result);
  return value;
}
static PyObject *analysis_catalog(PyObject *self, PyObject *unused) {
  size_t count = sequel_diagnostic_catalog_count();
  PyObject *result = PyTuple_New((Py_ssize_t)count);
  if (!result)
    return NULL;
  for (size_t i = 0; i < count; ++i) {
    sequel_diagnostic_t diagnostic;
    sequel_diagnostic_catalog_get(i, &diagnostic);
    PyObject *entry = diagnostic_dict(&diagnostic);
    if (!entry) {
      Py_DECREF(result);
      return NULL;
    }
    PyTuple_SET_ITEM(result, (Py_ssize_t)i, entry);
  }
  return result;
}
static PyObject *analysis_canonicalize(PyObject *self, PyObject *args) {
  PyObject *version_object;
  Py_ssize_t source_length, offset, end_offset, line, column, end_line, end_column;
  const char *code, *severity;
  int incomplete, complete;
  if (!PyArg_ParseTuple(args, "Onssnnnnnnpp", &version_object, &source_length, &code, &severity, &offset, &end_offset,
                        &line, &column, &end_line, &end_column, &incomplete, &complete))
    return NULL;
  unsigned long version = PyLong_AsUnsignedLong(version_object);
  if (PyErr_Occurred())
    return NULL;
  if (version > UINT_MAX) {
    PyErr_SetString(PyExc_ValueError, "Unsupported diagnostic schema version");
    return NULL;
  }
  if (source_length < 0 || offset < 0 || end_offset < 0 || line < 1 || column < 1 || end_line < 1 || end_column < 1) {
    PyErr_SetString(PyExc_ValueError, "Invalid diagnostic range");
    return NULL;
  }
  sequel_diagnostic_t input = {0}, output;
  input.code = (sequel_diagnostic_code_t)-1;
  for (size_t i = 0; i < sequel_diagnostic_catalog_count(); ++i)
    if (!strcmp(code, sequel_diagnostic_code_name((sequel_diagnostic_code_t)i))) {
      input.code = (sequel_diagnostic_code_t)i;
      break;
    }
  input.severity = !strcmp(severity, "note")      ? SEQUEL_NOTE
                   : !strcmp(severity, "warning") ? SEQUEL_WARNING
                   : !strcmp(severity, "error")   ? SEQUEL_ERROR
                                                  : (sequel_severity_t)-1;
  input.offset = (size_t)offset;
  input.end_offset = (size_t)end_offset;
  input.line = (size_t)line;
  input.column = (size_t)column;
  input.end_line = (size_t)end_line;
  input.end_column = (size_t)end_column;
  input.incomplete = incomplete;
  input.statement_complete = complete;
  if (sequel_diagnostic_canonicalize((unsigned)version, (size_t)source_length, &input, &output) != SEQUEL_OK) {
    PyErr_SetString(PyExc_ValueError, "Unsupported diagnostic schema, code, severity or range");
    return NULL;
  }
  return diagnostic_dict(&output);
}
static PyObject *analysis_map_range(PyObject *self, PyObject *args) {
  Py_ssize_t from_length, to_length, start, end;
  PyObject *segments;
  int reverse;
  if (!PyArg_ParseTuple(args, "nnOnnp", &from_length, &to_length, &segments, &start, &end, &reverse))
    return NULL;
  if (from_length < 0 || to_length < 0 || start < 0 || end < 0) {
    PyErr_SetString(PyExc_ValueError, "Map lengths and offsets must be nonnegative");
    return NULL;
  }
  PyObject *sequence = PySequence_Fast(segments, "segments must be a sequence");
  if (!sequence)
    return NULL;
  Py_ssize_t count = PySequence_Fast_GET_SIZE(sequence);
  sequel_map_segment_t *items = PyMem_Calloc((size_t)count + 1, sizeof(*items));
  if (!items) {
    Py_DECREF(sequence);
    return PyErr_NoMemory();
  }
  bool valid = true;
  static const char *names[] = {"copy", "replace", "delete", "insert"};
  for (Py_ssize_t i = 0; i < count; ++i) {
    Py_ssize_t a, b, c, d;
    const char *kind;
    if (!PyArg_ParseTuple(PySequence_Fast_GET_ITEM(sequence, i), "nnnns", &a, &b, &c, &d, &kind)) {
      valid = false;
      break;
    }
    int k = 0;
    while (k < 4 && strcmp(kind, names[k]))
      ++k;
    if (a < 0 || b < 0 || c < 0 || d < 0 || k == 4) {
      valid = false;
      break;
    }
    items[i] = (sequel_map_segment_t){(size_t)a, (size_t)b, (size_t)c, (size_t)d, (sequel_map_kind_t)k};
  }
  sequel_source_map_t map = {(size_t)from_length, (size_t)to_length, items, (size_t)count};
  sequel_mapped_range_t result;
  sequel_status_t status =
      valid ? sequel_source_map_range(&map, (size_t)start, (size_t)end, reverse, &result) : SEQUEL_ERR_INVALID_ARGUMENT;
  PyMem_Free(items);
  Py_DECREF(sequence);
  if (status != SEQUEL_OK) {
    if (!PyErr_Occurred())
      PyErr_SetString(PyExc_ValueError, "Invalid source map or range");
    return NULL;
  }
  return Py_BuildValue("nnO", (Py_ssize_t)result.start, (Py_ssize_t)result.end, result.exact ? Py_True : Py_False);
}
static PyObject *analysis_utf8_to_utf16(PyObject *self, PyObject *args) {
  const char *text;
  Py_ssize_t length, offset;
  if (!PyArg_ParseTuple(args, "s#n", &text, &length, &offset))
    return NULL;
  size_t result;
  if (offset < 0 || sequel_utf8_to_utf16(text, (size_t)length, (size_t)offset, &result) != SEQUEL_OK) {
    PyErr_SetString(PyExc_ValueError, "Offset must be a valid UTF-8 scalar boundary");
    return NULL;
  }
  return PyLong_FromSize_t(result);
}
static PyObject *analysis_utf16_to_utf8(PyObject *self, PyObject *args) {
  const char *text;
  Py_ssize_t length, offset;
  if (!PyArg_ParseTuple(args, "s#n", &text, &length, &offset))
    return NULL;
  size_t result;
  if (offset < 0 || sequel_utf16_to_utf8(text, (size_t)length, (size_t)offset, &result) != SEQUEL_OK) {
    PyErr_SetString(PyExc_ValueError, "Offset must be a valid UTF-16 scalar boundary");
    return NULL;
  }
  return PyLong_FromSize_t(result);
}
static PyObject *analysis_check_binds(PyObject *self, PyObject *args) {
  PyObject *binds;
  Py_ssize_t statement, supplied;
  if (!PyArg_ParseTuple(args, "Onn", &binds, &statement, &supplied))
    return NULL;
  if (statement < 0 || supplied < 0) {
    PyErr_SetString(PyExc_ValueError, "Bind counts must be nonnegative");
    return NULL;
  }
  PyObject *sequence = PySequence_Fast(binds, "binds must be a sequence");
  if (!sequence)
    return NULL;
  Py_ssize_t count = PySequence_Fast_GET_SIZE(sequence);
  sequel_bind_descriptor_t *items = PyMem_Calloc((size_t)count + 1, sizeof(*items));
  if (!items) {
    Py_DECREF(sequence);
    return PyErr_NoMemory();
  }
  bool valid = true;
  static const char *styles[] = {"positional", "numbered", "named", "typed"};
  for (Py_ssize_t i = 0; i < count; ++i) {
    Py_ssize_t ordinal, slot, index, offset, end;
    const char *style;
    if (!PyArg_ParseTuple(PySequence_Fast_GET_ITEM(sequence, i), "nnnnns", &ordinal, &slot, &index, &offset, &end,
                          &style)) {
      valid = false;
      break;
    }
    int k = 0;
    while (k < 4 && strcmp(style, styles[k]))
      ++k;
    if (ordinal < 1 || slot < 1 || index < 0 || offset < 0 || end < offset || k == 4) {
      valid = false;
      break;
    }
    items[i] = (sequel_bind_descriptor_t){(size_t)ordinal, (size_t)slot, (size_t)index,
                                          (size_t)offset,  (size_t)end,  (sequel_bind_style_t)k};
  }
  sequel_status_t status = valid ? sequel_check_binds(items, (size_t)count, (size_t)statement, (size_t)supplied)
                                 : SEQUEL_ERR_INVALID_ARGUMENT;
  PyMem_Free(items);
  Py_DECREF(sequence);
  if (status != SEQUEL_OK) {
    if (!PyErr_Occurred())
      PyErr_SetString(PyExc_ValueError, "Bind descriptors do not match supplied slot count");
    return NULL;
  }
  Py_RETURN_NONE;
}
