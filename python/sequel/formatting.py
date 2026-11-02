"""SQL layout formatting through the C engine."""

from __future__ import annotations

from . import _sql
from .diagnostics import Diagnostic, SQLDiagnosticError
from .dialect import Dialect


def format_sql(sql: str, *, dialect: Dialect = Dialect.GENERIC) -> str:
    """Format layout while preserving literals, comments, identifiers and markers.

    Raises `sequel.SQLDiagnosticError` for lexical, delimiter or layout issues.
    Input is UTF-8, limited to 1 GiB, 65536 tokens and 64 nesting levels.
    No SQL is executed or sanitized.
    """
    result, diagnostic = _sql.format(sql, int(Dialect(dialect)))
    if result is None:
        raise SQLDiagnosticError(Diagnostic(**diagnostic))
    return result


def format_sql_or_original(sql: str, *, dialect: Dialect = Dialect.GENERIC) -> str:
    """Return formatted SQL, or the original text if it cannot be formatted."""
    try:
        return format_sql(sql, dialect=dialect)
    except ValueError:
        return sql
