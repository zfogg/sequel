"""Base16 SQL highlighting through the C engine."""

from __future__ import annotations

from collections.abc import Mapping
from enum import StrEnum

from . import _sql
from .diagnostics import Diagnostic, SQLDiagnosticError
from .dialect import Dialect


class HighlightMode(StrEnum):
    HTML = "html"
    ANSI = "ansi"


class Appearance(StrEnum):
    AUTO = "auto"
    DARK = "dark"
    LIGHT = "light"


def colorschemes() -> tuple[str, ...]:
    """Return bundled Base16 scheme names in sorted order."""
    return _sql.colorschemes()


def highlight_sql(
    sql: str,
    *,
    dialect: Dialect = Dialect.GENERIC,
    mode: HighlightMode | str = HighlightMode.HTML,
    colorscheme: str = "default-dark",
    scheme_yaml: str | None = None,
    appearance: Appearance | str = Appearance.AUTO,
    colors: Mapping[str, str] | None = None,
    tolerant: bool = False,
) -> str:
    """Preserve SQL as escaped HTML or truecolor ANSI, without sanitizing.

    scheme_yaml accepts any Base16 YAML palette instead of a built-in name.
    Appearance selects the actual published light/dark variant. A missing
    counterpart raises ValueError. Token overrides are applied last.
    tolerant=True colors unfinished strings, comments, identifiers and parameters
    through end of input without inserting closing delimiters. Use diagnose() to
    retrieve their warning. Lexical errors raise SQLDiagnosticError; invalid
    controls and resource limits remain errors even in tolerant mode.
    """
    if scheme_yaml is not None and (not scheme_yaml or colorscheme != "default-dark"):
        raise ValueError("scheme_yaml requires a nonempty palette and no colorscheme name")
    result, diagnostic = _sql.highlight(
        sql,
        int(Dialect(dialect)),
        0 if HighlightMode(mode) == HighlightMode.HTML else 1,
        colorscheme,
        scheme_yaml or "",
        {Appearance.AUTO: 0, Appearance.DARK: 1, Appearance.LIGHT: 2}[Appearance(appearance)],
        dict(colors or {}),
        tolerant,
    )
    if result is None:
        raise SQLDiagnosticError(Diagnostic(**diagnostic))
    return result
