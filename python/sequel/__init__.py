"""Public APIs for sequel's shared C SQL engine."""

from .diagnostics import Diagnostic, SQLDiagnosticError, diagnose
from .dialect import Dialect
from .formatting import format_sql, format_sql_or_original
from .highlighting import Appearance, HighlightMode, colorschemes, highlight_sql
from .inspection import Query, SQLInspectionError, inspect
from .limits import DEFAULT_MAX_QUERY_TEXT_BYTES, DEFAULT_MAX_SUMMARY_BYTES, MAX_INPUT_BYTES
from .sanitization import sanitize

__all__ = [
    "DEFAULT_MAX_QUERY_TEXT_BYTES",
    "DEFAULT_MAX_SUMMARY_BYTES",
    "MAX_INPUT_BYTES",
    "Appearance",
    "Dialect",
    "Diagnostic",
    "SQLDiagnosticError",
    "diagnose",
    "HighlightMode",
    "Query",
    "SQLInspectionError",
    "colorschemes",
    "format_sql",
    "format_sql_or_original",
    "highlight_sql",
    "inspect",
    "sanitize",
]
