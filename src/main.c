#define _POSIX_C_SOURCE 200809L
#include "embedded.h"
#include "diagnostics.h"
#include <errno.h>
#include <fcntl.h>
#include <sequel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <windows.h>
/* shellapi requires the Windows base types above. */
#include <shellapi.h>
#define isatty _isatty
#define fileno _fileno
#define close _close
#define fdopen _fdopen
#define getpid _getpid
#else
#include <unistd.h>
#endif

#ifdef _WIN32
typedef struct _stat file_info;
#else
typedef struct stat file_info;
#endif

#include "input.h"
#define MAX_SQL SEQUEL_INPUT_MAX
static const char usage[] = "Usage:\n"
                            "  sequel format [OPTIONS] [SQL | PATH ...]\n"
                            "  sequel sanitize [OPTIONS] [SQL | PATH ...]\n\n"
                            "  sequel highlight [OPTIONS] [SQL | PATH ...]\n\n"
                            "  sequel explain [--all] [--json] [--dialect DIALECT] [SQL | PATH ...]\n"
                            "  sequel interactive [HIGHLIGHT OPTIONS] [--json]\n\n"
                            "Read piped or redirected stdin when no SQL or paths are supplied.\n"
                            "Files are read in argument order and results go to stdout by default.\n"
                            "  --edit-file         Modify each path in place; no stdout output.\n"
                            "  --embedded          Transform SQL inside host-language source.\n"
                            "  --language LANGUAGE Set source language for strings or stdin.\n"
                            "  --dialect DIALECT   generic, postgresql, sqlite, mysql, duckdb, clickhouse.\n"
                            "Sanitize: --remove-comments and --replace-literals select transformations.\n"
                            "With neither selection flag, sanitize applies both.\n"
                            "Highlight: --html | --ansi (default: --html).\n"
                            "  --tolerant               Highlight unfinished quotes/comments while editing.\n"
                            "  --interactive            Multiline SQL workbench (ANSI by default; no execution).\n"
                            "  --json                   JSON explanations (explain/diagnose/interactive).\n"
                            "  --all                    All safe-boundary diagnostics (explain/diagnose).\n"
                            "  diagnose                 Alias for explain.\n"
                            "  --colorscheme NAME|PATH  Built-in Base16 name or Base16 YAML file (default-dark).\n"
                            "  --light | --dark         Force appearance; otherwise preserve the scheme.\n"
                            "  --list-colorschemes      List built-in Base16 names.\n"
                            "  --color-TYPE '#RRGGBB'    Override foreground, background, keyword, type, function,\n"
                            "                           variable, string, number, boolean, null, comment, operator,\n"
                            "                           punctuation or identifier (after scheme and appearance).\n"
                            "Languages: go, c, c++, cpp, python, javascript, typescript, rust, haskell, clojure.\n";

static int error(const char *message) {
  fprintf(stderr, "sequel: %s\n", message);
  return 1;
}

#ifdef _WIN32
/* Internally argv and paths are UTF-8; Windows APIs receive UTF-16. */
static wchar_t *wide_path(const char *path) {
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
  wchar_t *wide = n ? malloc((size_t)n * sizeof(*wide)) : NULL;
  if (wide && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, n)) {
    free(wide);
    return NULL;
  }
  return wide;
}

static FILE *open_input(const char *path) {
  wchar_t *wide = wide_path(path);
  FILE *file = wide ? _wfopen(wide, L"rb") : NULL;
  free(wide);
  return file;
}

static int create_temp(const char *path) {
  wchar_t *wide = wide_path(path);
  int fd = wide ? _wopen(wide, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE) : -1;
  free(wide);
  return fd;
}

static int finish_temp(const char *temp, const char *path, file_info *info, int failed) {
  wchar_t *from = wide_path(temp), *to = wide_path(path);
  if (!from || !to)
    failed = 1;
  if (!failed && _wchmod(from, info->st_mode & (_S_IREAD | _S_IWRITE)))
    failed = 1;
  if (!failed && !MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    failed = 1;
  if (failed && from) {
    /* A read-only original can reject replacement after chmod of the sibling.
     * Restore write access before removing that sibling on the error path. */
    _wchmod(from, _S_IREAD | _S_IWRITE);
    _wremove(from);
  }
  free(from);
  free(to);
  return failed;
}
#else
static FILE *open_input(const char *path) { return fopen(path, "rb"); }
#endif

static int regular_file(const char *path, file_info *info) {
#ifdef _WIN32
  wchar_t *wide = wide_path(path);
  if (!wide)
    return 0;
  DWORD attributes = GetFileAttributesW(wide);
  int status = _wstat(wide, info);
  free(wide);
  if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
    return 0;
  return status == 0 && (info->st_mode & S_IFMT) == S_IFREG;
#else
  return lstat(path, info) == 0 && S_ISREG(info->st_mode);
#endif
}

/* Create an exclusive sibling and finish all writes before replacing the
 * original. Failed formatting or writes never truncate the original file. */
static int replace_file(const char *path, const char *sql, size_t n, file_info *info) {
  char *temp = malloc(strlen(path) + 64);
  if (!temp)
    return error("out of memory");
  int fd = -1;
  for (unsigned i = 0; i < 100; ++i) {
    snprintf(temp, strlen(path) + 64, "%s.gotel-%ld-%u.tmp", path, (long)getpid(), i);
#ifdef _WIN32
    fd = create_temp(temp);
#else
    fd = open(temp, O_WRONLY | O_CREAT | O_EXCL, 0600);
#endif
    if (fd >= 0 || errno != EEXIST)
      break;
  }
  if (fd < 0) {
    free(temp);
    return error("cannot create temporary sibling");
  }
  FILE *file = fdopen(fd, "wb");
  int failed = 0;
  if (!file) {
    close(fd);
    failed = 1;
  } else {
    failed = fwrite(sql, 1, n, file) != n || fflush(file) != 0;
#ifdef _WIN32
    if (!failed && _commit(fd) != 0)
      failed = 1;
#else
    if (!failed && (fchmod(fd, info->st_mode & 0777) != 0 || fsync(fd) != 0))
      failed = 1;
#endif
    if (fclose(file) != 0)
      failed = 1;
  }
#ifdef _WIN32
  failed = finish_temp(temp, path, info, failed);
#else
  if (!failed && rename(temp, path) != 0)
    failed = 1;
#endif
#ifndef _WIN32
  if (failed)
    remove(temp);
#endif
  free(temp);
  return failed ? error("cannot replace file; original retained") : 0;
}

static int process_input(int format, int d, int embedded, const char *language_name, unsigned sanitize_flags, int edit,
                         int explaining, int all_diagnostics, int json, const sequel_highlight_options_t *highlight,
                         const char *argument, int source_is_path) {
  const char *source_path = source_is_path ? argument : NULL;
  const char *path = edit ? source_path : NULL;
  int language = embedded ? embedded_language(language_name, source_path) : 0;
  if (embedded && !language)
    return error("unknown source language; use --language "
                 "go|c|c++|cpp|python|javascript|typescript|rust|haskell|clojure");
  if (source_path)
    argument = NULL;
  file_info info;
  FILE *input = stdin;
  if (source_path) {
    if (!regular_file(source_path, &info))
      return error("input path must be a regular file (no symlinks)");
    input = open_input(source_path);
    if (!input)
      return error("cannot open file");
  } else if (!argument && isatty(fileno(stdin)))
    return error(usage);
#ifdef _WIN32
  if (!source_path && !argument)
    _setmode(fileno(stdin), _O_BINARY);
  _setmode(fileno(stdout), _O_BINARY);
#endif
  char *sql = NULL;
  size_t n = 0;
  int failed = 0;
  if (argument) {
    n = strlen(argument);
    if (n > MAX_SQL)
      n = MAX_SQL + 1;
    sql = malloc(n + 1);
    if (sql) {
      memcpy(sql, argument, n);
      sql[n] = 0;
    }
  } else {
    sql = input_read(input, &n);
    if (source_path && fclose(input) != 0)
      failed = 1;
  }
  if (!sql)
    return error("input read error or out of memory");
  if (failed) {
    free(sql);
    return error("input read error");
  }
  if (explaining && all_diagnostics) {
    sequel_status_t status = diagnose_all(stdout, sql, n, (sequel_dialect_t)d, json);
    free(sql);
    return status != SEQUEL_OK || ferror(stdout);
  }
  if (n > MAX_SQL || memchr(sql, 0, n)) {
    sequel_diagnostic_t diagnostic;
    sequel_diagnose_n(sql, n, (sequel_dialect_t)d, &diagnostic);
    print_diagnostic(explaining ? stdout : stderr, &diagnostic, json);
    free(sql);
    return 1;
  }
  if (explaining) {
    sequel_diagnostic_t diagnostic;
    sequel_status_t status = sequel_diagnose_n(sql, n, (sequel_dialect_t)d, &diagnostic);
    print_diagnostic(stdout, &diagnostic, json);
    free(sql);
    return status != SEQUEL_OK || ferror(stdout);
  }
  if (embedded) {
    char *result = transform_embedded(sql, language, d, !format, sanitize_flags);
    if (!result) {
      free(sql);
      return error("cannot safely format embedded source (invalid UTF-8, "
                   "malformed source, unsupported template or output limit)");
    }
    int status = path ? (strcmp(sql, result) ? replace_file(path, result, strlen(result), &info) : 0)
                      : (fwrite(result, 1, strlen(result), stdout) != strlen(result) || fflush(stdout) != 0
                             ? error("cannot write stdout")
                             : 0);
    free(result);
    free(sql);
    return status;
  }
  size_t capacity = n + 2, length = 0;
  sequel_diagnostic_t diagnostic = {0};
  if (highlight || (format && !path)) {
    sequel_status_t status = highlight ? sequel_highlight_ex(sql, highlight, NULL, 0, &length, &diagnostic)
                                       : sequel_format_ex(sql, (sequel_dialect_t)d, NULL, 0, &length, &diagnostic);
    if (status != SEQUEL_OK) {
      print_diagnostic(stderr, &diagnostic, 0);
      free(sql);
      return 1;
    }
    capacity = length + 1;
  }
  char *out = calloc(capacity, 1);
  if (!out) {
    free(sql);
    return error("out of memory");
  }
  if (highlight)
    failed = sequel_highlight_ex(sql, highlight, out, capacity, &length, &diagnostic) != SEQUEL_OK;
  else if (path && format) {
    sequel_status_t diagnostic_status = sequel_diagnose_n(sql, n, (sequel_dialect_t)d, &diagnostic);
    if (diagnostic_status != SEQUEL_OK) {
      print_diagnostic(stderr, &diagnostic, 0);
      free(sql);
      free(out);
      return 1;
    }
    failed = sequel_compact(sql, (sequel_dialect_t)d, out, capacity) != SEQUEL_OK;
  } else if (format)
    failed = sequel_format_ex(sql, (sequel_dialect_t)d, out, capacity, &length, &diagnostic) != SEQUEL_OK;
  else {
    bool truncated = false;
    sequel_status_t diagnostic_status = sequel_diagnose_n(sql, n, (sequel_dialect_t)d, &diagnostic);
    if (diagnostic_status != SEQUEL_OK) {
      print_diagnostic(stderr, &diagnostic, 0);
      free(sql);
      free(out);
      return 1;
    }
    failed = sequel_sanitize_with_flags(sql, (sequel_dialect_t)d, sanitize_flags, true, capacity - 1, out, capacity,
                                        &truncated) != SEQUEL_OK;
    failed |= truncated;
  }
  int result;
  if (failed) {
    if (diagnostic.code == SEQUEL_DIAG_NONE)
      sequel_diagnose(sql, (sequel_dialect_t)d, &diagnostic);
    if (diagnostic.code == SEQUEL_DIAG_NONE)
      result = error("invalid or unsupported SQL transformation");
    else {
      print_diagnostic(stderr, &diagnostic, 0);
      result = 1;
    }
  } else if (path)
    result = strcmp(sql, out) ? replace_file(path, out, strlen(out), &info) : 0;
  else
    result =
        fwrite(out, 1, strlen(out), stdout) != strlen(out) || fflush(stdout) != 0 ? error("cannot write stdout") : 0;
  free(sql);
  free(out);
  return result;
}

static int run_with_inputs(int argc, char **argv, const char **inputs) {
  if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
    fputs(usage, stdout);
    return 0;
  }
  if (argc < 2 || (strcmp(argv[1], "format") && strcmp(argv[1], "sanitize") && strcmp(argv[1], "highlight") &&
                   strcmp(argv[1], "interactive") && strcmp(argv[1], "explain") && strcmp(argv[1], "diagnose")))
    return error(usage);
  int format = !strcmp(argv[1], "format"), options = 1, embedded = 0, edit = 0, count = 0;
  const char *dialect = "generic", *language_name = NULL;
  int workbench = !strcmp(argv[1], "interactive"),
      explaining = !strcmp(argv[1], "explain") || !strcmp(argv[1], "diagnose"), all_diagnostics = 0, json = 0,
      tolerant = 0;
  int highlighting = !strcmp(argv[1], "highlight") || workbench, output_mode = -1, appearance = 0, list_schemes = 0;
  const char *scheme_name = "default-dark", *overrides[SEQUEL_COLOR_COUNT] = {0};
  unsigned flags = 0;
  for (int i = 2; i < argc; ++i) {
    const char *arg = argv[i];
    if (options && (!strcmp(arg, "--help") || !strcmp(arg, "-h"))) {
      fputs(usage, stdout);
      return 0;
    }
    if (options && !strcmp(arg, "--")) {
      options = 0;
      continue;
    }
    if (options && !strcmp(arg, "--interactive")) {
      if (!highlighting)
        return error("--interactive requires highlight; or use sequel interactive");
      workbench = 1;
      continue;
    }
    if (options && !strcmp(arg, "--tolerant")) {
      if (!highlighting)
        return error("--tolerant requires highlight");
      tolerant = 1;
      continue;
    }
    if (options && !strcmp(arg, "--all")) {
      if (!explaining)
        return error("--all requires explain or diagnose");
      all_diagnostics = 1;
      continue;
    }
    if (options && !strcmp(arg, "--json")) {
      json = 1;
      continue;
    }
    if (options && (!strcmp(arg, "--html") || !strcmp(arg, "--ansi"))) {
      int selected = !strcmp(arg, "--ansi") ? SEQUEL_ANSI : SEQUEL_HTML;
      if (!highlighting || (output_mode != -1 && output_mode != selected))
        return error("--html and --ansi require highlight and are mutually exclusive");
      output_mode = selected;
      continue;
    }
    if (options && (!strcmp(arg, "--light") || !strcmp(arg, "--dark"))) {
      int selected = !strcmp(arg, "--light") ? SEQUEL_APPEARANCE_LIGHT : SEQUEL_APPEARANCE_DARK;
      if (!highlighting || (appearance && appearance != selected))
        return error("--light and --dark require highlight and are mutually exclusive");
      appearance = selected;
      continue;
    }
    if (options && !strcmp(arg, "--list-colorschemes")) {
      if (!highlighting)
        return error("--list-colorschemes requires highlight");
      list_schemes = 1;
      continue;
    }
    if (options && (!strcmp(arg, "--colorscheme") || !strncmp(arg, "--colorscheme=", 14))) {
      if (!highlighting)
        return error("--colorscheme requires highlight");
      if (arg[13] == '=')
        scheme_name = arg + 14;
      else if (++i < argc)
        scheme_name = argv[i];
      else
        return error("--colorscheme requires a value");
      if (!*scheme_name)
        return error("--colorscheme requires a value");
      continue;
    }
    if (options && !strncmp(arg, "--color-", 8)) {
      if (!highlighting)
        return error("color overrides require highlight");
      int type;
      for (type = 0; type < SEQUEL_COLOR_COUNT; ++type) {
        const char *name = sequel_highlight_color_name((sequel_highlight_color_t)type);
        size_t n = strlen(name);
        if (!strncmp(arg + 8, name, n) && (!arg[8 + n] || arg[8 + n] == '=')) {
          if (arg[8 + n] == '=')
            overrides[type] = arg + 9 + n;
          else if (++i < argc)
            overrides[type] = argv[i];
          else
            return error("color override requires a value");
          break;
        }
      }
      if (type == SEQUEL_COLOR_COUNT)
        return error("unknown color type; see --help");
      continue;
    }
    if (options && !strcmp(arg, "--edit-file")) {
      edit = 1;
      continue;
    }
    if (options && !strncmp(arg, "--edit-file=", 12)) {
      if (!arg[12])
        return error("--edit-file requires paths");
      edit = 1;
      inputs[count++] = arg + 12;
      continue;
    }
    if (options && !strcmp(arg, "--embedded")) {
      embedded = 1;
      continue;
    }
    if (options && (!strcmp(arg, "--remove-comments") || !strcmp(arg, "--replace-literals"))) {
      if (format || highlighting || explaining)
        return error("sanitization options require sanitize");
      flags |= !strcmp(arg, "--remove-comments") ? SEQUEL_REMOVE_COMMENTS : SEQUEL_REPLACE_LITERALS;
      continue;
    }
    if (options && (!strcmp(arg, "--dialect") || !strcmp(arg, "--language"))) {
      if (++i == argc || !*argv[i])
        return error("option requires a value");
      if (!strcmp(arg, "--dialect"))
        dialect = argv[i];
      else
        language_name = argv[i];
      continue;
    }
    if (options && !strncmp(arg, "--dialect=", 10)) {
      dialect = arg + 10;
      continue;
    }
    if (options && !strncmp(arg, "--language=", 11)) {
      language_name = arg + 11;
      continue;
    }
    if (options && arg[0] == '-')
      return error("unknown option; use -- before input starting with '-'");
    inputs[count++] = arg;
  }
  if (language_name && !embedded)
    return error("--language requires --embedded");
  if (json && !explaining && !workbench)
    return error("--json requires explain, diagnose or interactive mode");
  if (workbench && (count || edit || embedded || list_schemes))
    return error(
        "interactive mode reads stdin; paths, --edit-file, --embedded and --list-colorschemes are not supported");
  if (explaining && (edit || embedded))
    return error("explain does not edit files or process embedded host-language source");
  if (highlighting && embedded)
    return error("highlight expects SQL, not embedded host-language source");
  if (edit && !count)
    return error("--edit-file requires one or more paths");
  const char *names[] = {"generic", "sqlite", "postgresql", "duckdb", "mysql", "clickhouse"};
  int d;
  for (d = 0; d < 6 && strcmp(dialect, names[d]); ++d) {
  }
  if (d == 6)
    return error("unsupported SQL dialect");
  if (!flags)
    flags = SEQUEL_SANITIZE_ALL;
  sequel_highlight_options_t highlight;
  sequel_highlight_options_t *highlight_options = NULL;
  if (highlighting) {
    if (list_schemes) {
      for (size_t i = 0; i < sequel_colorscheme_count(); ++i)
        puts(sequel_colorscheme_name(i));
      return ferror(stdout) ? error("cannot write stdout") : 0;
    }
    sequel_colorscheme_t scheme;
    int named_scheme = sequel_colorscheme(scheme_name, &scheme) == SEQUEL_OK;
    if (!named_scheme) {
      FILE *file = open_input(scheme_name);
      if (!file)
        return error("unknown Base16 scheme or unreadable YAML file; use --list-colorschemes");
      size_t n = 0;
      char *yaml = input_read(file, &n);
      int failed = fclose(file) != 0;
      failed |= !yaml || n > MAX_SQL;
      if (!failed)
        failed = memchr(yaml, 0, n) != NULL;
      if (!failed)
        failed = sequel_colorscheme_parse(yaml, &scheme) != SEQUEL_OK;
      free(yaml);
      if (failed)
        return error("invalid Base16 YAML palette (requires base00 through base0F)");
    }
    sequel_highlight_options_init(&highlight);
    highlight.dialect = (sequel_dialect_t)d;
    highlight.mode = output_mode == SEQUEL_ANSI || (workbench && output_mode == -1) ? SEQUEL_ANSI : SEQUEL_HTML;
    highlight.tolerant = tolerant || workbench;
    sequel_status_t variant_status =
        named_scheme ? sequel_colorscheme_for_appearance(scheme_name, (sequel_appearance_t)appearance, &scheme)
                     : sequel_colorscheme_set_appearance(&scheme, (sequel_appearance_t)appearance);
    if (variant_status != SEQUEL_OK || sequel_highlight_set_colorscheme(&highlight, &scheme) != SEQUEL_OK)
      return error("colorscheme has no published variant for the requested appearance");
    for (int i = 0; i < SEQUEL_COLOR_COUNT; ++i)
      if (overrides[i] &&
          sequel_highlight_set_color(&highlight, sequel_highlight_color_name((sequel_highlight_color_t)i),
                                     overrides[i]) != SEQUEL_OK)
        return error("invalid color; expected #RRGGBB or RRGGBB");
    highlight_options = &highlight;
  }
  if (workbench)
    return interactive(highlight_options, json);
  if (!count)
    return process_input(format, d, embedded, language_name, flags, 0, explaining, all_diagnostics, json,
                         highlight_options, NULL, 0);
  int result = 0;
  for (int i = 0; i < count; ++i) {
    file_info info;
    const char *extension = strrchr(inputs[i], '.');
    int from_path = count > 1 || edit || regular_file(inputs[i], &info) ||
                    (extension && !strcmp(extension, ".sql") && !strpbrk(inputs[i], " \t\r\n"));
    /* Embedded source can also be passed as a string. Existing files and
     * multiple arguments are paths; language inference requires a path. */
    if (embedded && !language_name)
      from_path = 1;
    int status = process_input(format, d, embedded, language_name, flags, edit, explaining, all_diagnostics, json,
                               highlight_options, inputs[i], from_path);
    if (status && from_path && !all_diagnostics)
      fprintf(stderr, "sequel: failed: %s\n", inputs[i]);
    result |= status;
  }
  return result;
}

static int run(int argc, char **argv) {
  const char **inputs = calloc((size_t)argc + 1, sizeof(*inputs));
  if (!inputs)
    return error("out of memory");
  int result = run_with_inputs(argc, argv, inputs);
  free(inputs);
  return result;
}

int main(int argc, char **argv) {
#ifdef _WIN32
  wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!wide)
    return error("cannot read Unicode command line");
  char **utf8 = calloc((size_t)argc + 1, sizeof(*utf8));
  int failed = !utf8;
  for (int i = 0; !failed && i < argc; ++i) {
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1, NULL, 0, NULL, NULL);
    utf8[i] = n ? malloc((size_t)n) : NULL;
    if (!utf8[i] || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1, utf8[i], n, NULL, NULL))
      failed = 1;
  }
  LocalFree(wide);
  int result = failed ? error("cannot decode Unicode command line") : run(argc, utf8);
  if (utf8) {
    for (int i = 0; i < argc; ++i)
      free(utf8[i]);
    free(utf8);
  }
  (void)argv;
  return result;
#else
  return run(argc, argv);
#endif
}
