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
    PyErr_SetString(PyExc_ValueError, "query text limit must be between 0 and 32768");
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
    PyMem_Free(metadata);
    PyErr_SetString(PyExc_ValueError, "SQL inspection rejected input");
    return NULL;
  }
  PyObject *result = Py_BuildValue("sssOn", metadata->text, metadata->summary, metadata->operation,
                                   metadata->truncated ? Py_True : Py_False, (Py_ssize_t)metadata->statement_count);
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
  if (n > 1024 * 1024) {
    PyErr_SetString(PyExc_ValueError, "SQL exceeds 1 MiB");
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
static PyMethodDef methods[] = {
    {"diagnose", diagnose_sql, METH_VARARGS, "Locate and explain the first lexical/delimiter issue."},
    {"highlight", highlight_sql, METH_VARARGS, "Highlight SQL as escaped HTML or truecolor ANSI."},
    {"colorschemes", colorschemes, METH_NOARGS, "List bundled Base16 names."},
    {"sanitize", sanitize_sql, METH_VARARGS, "Selectively remove comments and replace literals."},
    {"inspect", inspect_sql, METH_VARARGS, "Inspect using the shared C engine."},
    {"format", format_sql, METH_VARARGS, "Format without sanitizing."},
    {NULL, NULL, 0, NULL}};
static struct PyModuleDef module = {PyModuleDef_HEAD_INIT, "_sql", NULL, -1, methods};
PyMODINIT_FUNC PyInit__sql(void) { return PyModule_Create(&module); }
