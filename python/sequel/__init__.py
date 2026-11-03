"""Public APIs for sequel's shared C SQL engine."""

from .analysis import (
    ANALYSIS_SCHEMA_VERSION,
    DIAGNOSTIC_SCHEMA_VERSION,
    Analysis,
    BindDescriptor,
    MappedRange,
    MapSegment,
    SourceMap,
    StatementRange,
    analyze,
    canonicalize_diagnostic,
    check_binds,
    diagnostic_catalog,
    fingerprint,
    utf8_to_utf16,
    utf16_to_utf8,
    validate_diagnostic,
)
from .diagnostics import Diagnostic, SQLDiagnosticError, diagnose
from .dialect import Dialect
from .formatting import format_sql, format_sql_or_original
from .highlighting import Appearance, HighlightMode, colorschemes, highlight_sql
from .inspection import Query, SQLInspectionError, inspect
from .limits import DEFAULT_MAX_QUERY_TEXT_BYTES, DEFAULT_MAX_SUMMARY_BYTES, MAX_INPUT_BYTES
from .sanitization import sanitize

__all__ = [
    "ANALYSIS_SCHEMA_VERSION",
    "DIAGNOSTIC_SCHEMA_VERSION",
    "Analysis",
    "BindDescriptor",
    "MapSegment",
    "MappedRange",
    "SourceMap",
    "StatementRange",
    "analyze",
    "canonicalize_diagnostic",
    "check_binds",
    "diagnostic_catalog",
    "fingerprint",
    "utf8_to_utf16",
    "utf16_to_utf8",
    "validate_diagnostic",
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
