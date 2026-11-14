/* A portable line-oriented SQL workbench. No SQL is ever executed. */
#include "diagnostics.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif
#include "input.h"

/* Used only on fatal input errors. Retain a bounded prefix for source locations. */
static void reject_input(char **buffer, size_t *capacity, size_t used, const char *line, size_t n, sequel_dialect_t dialect, int json) {
  size_t needed = used + n + 2;
  if (needed > SEQUEL_INPUT_MAX + 2)
    needed = SEQUEL_INPUT_MAX + 2;
  if (!input_reserve(buffer, capacity, needed)) {
    fputs("sequel: out of memory\n", stderr);
    return;
  }
  char *sql = *buffer;
  if (used && sql[used - 1] != '\n' && sql[used - 1] != '\r')
    sql[used++] = '\n';
  size_t total = used + n, copy = n;
  if (copy > SEQUEL_INPUT_MAX + 1 - used)
    copy = SEQUEL_INPUT_MAX + 1 - used;
  memcpy(sql + used, line, copy);
  sequel_diagnostic_t diagnostic;
  sequel_diagnose_n(sql, total, dialect, &diagnostic);
  print_diagnostic(stderr, &diagnostic, json);
}

static int preview(const char *sql, const sequel_highlight_options_t *options, int json) {
  sequel_diagnostic_t diagnostic;
  size_t length;
  sequel_status_t status = sequel_highlight_ex(sql, options, NULL, 0, &length, &diagnostic);
  if (status != SEQUEL_OK) {
    print_diagnostic(stderr, &diagnostic, json);
    return 2;
  }
  char *out = malloc(length + 1);
  if (!out)
    return 1;
  status = sequel_highlight_ex(sql, options, out, length + 1, &length, &diagnostic);
  int failed = status != SEQUEL_OK || fwrite(out, 1, length, stdout) != length || fputc('\n', stdout) == EOF || fflush(stdout);
  free(out);
  return failed;
}

int interactive(const sequel_highlight_options_t *options, int json) {
  int terminal = isatty(fileno(stdin));
#ifdef _WIN32
  unsigned input_cp = GetConsoleCP(), output_cp = GetConsoleOutputCP();
  DWORD console_mode = 0;
  HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
  int restore_mode = GetConsoleMode(console, &console_mode);
  if (terminal) {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    if (restore_mode)
      SetConsoleMode(console, console_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }
  _setmode(fileno(stdin), _O_BINARY);
  _setmode(fileno(stdout), _O_BINARY);
#endif
  char *sql = NULL, *line = NULL;
  size_t sql_capacity = 0, line_capacity = 0;
  int result = 0, finished = 0;
  size_t used = 0;
  if (!input_reserve(&sql, &sql_capacity, 1) || !input_reserve(&line, &line_capacity, 1)) {
    result = 1;
    goto done;
  }
  sql[0] = line[0] = 0;
  if (terminal)
    fputs("sequel SQL workbench (no SQL execution). :help for commands.\n", stderr);
  for (;;) {
    if (terminal) {
      fputs(used && !finished ? "...> " : "sql> ", stderr);
      fflush(stderr);
    }
    size_t n = 0;
    int ch;
    while ((ch = fgetc(stdin)) != EOF) {
      if (!input_reserve(&line, &line_capacity, n + 2)) {
        result = 1;
        goto done;
      }
      if (!ch || n == SEQUEL_INPUT_MAX) {
        line[n++] = (char)ch;
        reject_input(&sql, &sql_capacity, finished ? 0 : used, line, n, options->dialect, json);
        result = 1;
        goto done;
      }
      line[n++] = (char)ch;
      if (ch == '\n')
        break;
    }
    if (!n) {
      result = ferror(stdin) ? 1 : 0;
      break;
    }
    line[n] = 0;
    size_t command_end = n;
    while (command_end && (line[command_end - 1] == '\r' || line[command_end - 1] == '\n'))
      --command_end;
    if (line[0] == ':') {
      char ending = line[command_end];
      line[command_end] = 0;
      if (!strcmp(line, ":quit") || !strcmp(line, ":q"))
        break;
      if (!strcmp(line, ":help")) {
        fputs(":show     Highlight the current SQL buffer\n"
              ":explain  Explain the first diagnostic and show completion state\n"
              ":explain-all  Explain all safe-boundary diagnostics\n"
              ":format   Format the current complete SQL buffer\n"
              ":clear    Clear the buffer\n:quit     Exit (:q also works)\n"
              "SQL accumulates across lines. A final semicolon starts a new buffer on the next SQL line.\n"
              "Exact command lines are reserved, even inside unfinished SQL. Other lines are SQL input.\n",
              stderr);
        continue;
      }
      sequel_diagnostic_t current;
      sequel_diagnose(sql, options->dialect, &current);
      {
        if (!strcmp(line, ":clear")) {
          sql[used = 0] = 0;
          finished = 0;
          continue;
        }
        if (!strcmp(line, ":explain-all")) {
          diagnose_all(stdout, sql, used, options->dialect, json);
          if (fflush(stdout) || ferror(stdout)) {
            result = 1;
            break;
          }
          continue;
        }
        if (!strcmp(line, ":explain")) {
          print_diagnostic(stdout, &current, json);
          if (fflush(stdout) || ferror(stdout)) {
            result = 1;
            break;
          }
          continue;
        }
        if (!strcmp(line, ":show")) {
          if (preview(sql, options, json) == 1) {
            result = 1;
            break;
          }
          continue;
        }
        if (!strcmp(line, ":format")) {
          size_t length;
          if (sequel_format_ex(sql, options->dialect, NULL, 0, &length, &current) != SEQUEL_OK) {
            print_diagnostic(stderr, &current, json);
            continue;
          }
          if (length > SEQUEL_INPUT_MAX) {
            fputs("sequel: formatted buffer would exceed 1 GiB; original retained\n", stderr);
            continue;
          }
          char *formatted = malloc(length + 1);
          if (!formatted) {
            result = 1;
            break;
          }
          sequel_status_t status = sequel_format_ex(sql, options->dialect, formatted, length + 1, &length, &current);
          if (status == SEQUEL_OK && !input_reserve(&sql, &sql_capacity, length + 1)) {
            free(formatted);
            result = 1;
            break;
          }
          if (status == SEQUEL_OK) {
            memcpy(sql, formatted, length + 1);
            used = length;
          }
          free(formatted);
          if (status != SEQUEL_OK)
            print_diagnostic(stderr, &current, json);
          else if (preview(sql, options, json) == 1) {
            result = 1;
            break;
          }
          continue;
        }
      }
      /* Restore the line ending removed while checking a command. */
      line[command_end] = ending;
    }
    if (finished) {
      sql[used = 0] = 0;
      finished = 0;
    }
    size_t gap = used && sql[used - 1] != '\n' && sql[used - 1] != '\r' ? 1 : 0;
    if (n + gap > SEQUEL_INPUT_MAX - used) {
      reject_input(&sql, &sql_capacity, used, line, n, options->dialect, json);
      result = 1;
      break;
    }
    if (!input_reserve(&sql, &sql_capacity, used + gap + n + 1)) {
      result = 1;
      break;
    }
    if (gap)
      sql[used++] = '\n';
    memcpy(sql + used, line, n + 1);
    used += n;
    sequel_diagnostic_t diagnostic;
    sequel_diagnose(sql, options->dialect, &diagnostic);
    finished = diagnostic.statement_complete;
    int shown = preview(sql, options, json);
    if (shown == 1) {
      result = 1;
      break;
    }
    if (shown == 0 && diagnostic.code != SEQUEL_DIAG_NONE)
      print_diagnostic(stderr, &diagnostic, json);
  }
done:
  free(sql);
  free(line);
#ifdef _WIN32
  if (terminal) {
    SetConsoleCP(input_cp);
    SetConsoleOutputCP(output_cp);
    if (restore_mode)
      SetConsoleMode(console, console_mode);
  }
#endif
  return result;
}
