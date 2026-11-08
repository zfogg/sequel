"""Structured SQL diagnostics and explanations from the shared lexer."""

from dataclasses import dataclass

from . import _sql
from .dialect import Dialect


@dataclass(frozen=True)
class Diagnostic:
    """Issue and completion state with UTF-8 offsets and one-based scalar columns.

    Ranges end exclusively. Code `none` means no issue was found; incomplete
    lexical input has warning severity. Explanations never include input SQL.
    """

    code: str
    severity: str
    offset: int
    end_offset: int
    line: int
    column: int
    end_line: int
    end_column: int
    incomplete: bool
    statement_complete: bool
    message: str
    explanation: str
    hint: str


class SQLDiagnosticError(ValueError):
    """SQL transformation failure with a structured `diagnostic` attribute."""

    def __init__(self, diagnostic: Diagnostic) -> None:
        self.diagnostic = diagnostic
        super().__init__(f"{diagnostic.line}:{diagnostic.column}: {diagnostic.message} [{diagnostic.code}]")


def diagnose(sql: str, *, dialect: Dialect = Dialect.GENERIC) -> Diagnostic:
    """Return the first lexical/delimiter issue, or code='none'. Never executes SQL.

    Offsets are zero-based UTF-8 bytes; lines/Unicode scalar columns are one-based.
    Incomplete input is a warning. This is not a SQL grammar validator.
    """
    return Diagnostic(**_sql.diagnose(sql, int(Dialect(dialect))))


def diagnose_all(sql: str, *, dialect: Dialect = Dialect.GENERIC) -> tuple[Diagnostic, ...]:
    """Return every safe-boundary diagnostic in original UTF-8 coordinates.

    Valid SQL returns an empty tuple. This uses unified analysis without
    retaining sanitized text, source maps, bind descriptors, or fingerprints.
    It does not validate database grammar or execute SQL.
    """
    from .analysis import analyze

    return analyze(
        sql,
        dialect=dialect,
        omit_query_text=True,
        omit_source_maps=True,
        omit_bind_descriptors=True,
        include_lints=True,
    ).diagnostics
