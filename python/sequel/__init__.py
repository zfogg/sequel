"""Public APIs for sequel's shared C SQL engine."""

from .diagnostics import Diagnostic, SQLDiagnosticError, diagnose
from .dialect import Dialect
from .formatting import format_sql, format_sql_or_original
from .highlighting import Appearance, HighlightMode, colorschemes, highlight_sql
from .inspection import Query, SQLInspectionError, inspect
from .sanitization import sanitize

__all__ = [
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
