"""Structured SQL diagnostics and explanations from the shared lexer."""

from dataclasses import dataclass

from . import _sql
from .dialect import Dialect


@dataclass(frozen=True)
class Diagnostic:
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
    def __init__(self, diagnostic: Diagnostic) -> None:
        self.diagnostic = diagnostic
        super().__init__(f"{diagnostic.line}:{diagnostic.column}: {diagnostic.message} [{diagnostic.code}]")


def diagnose(sql: str, *, dialect: Dialect = Dialect.GENERIC) -> Diagnostic:
    """Return the first lexical/delimiter issue, or code='none'. Never executes SQL.

    Offsets are zero-based UTF-8 bytes; lines/Unicode scalar columns are one-based.
    Incomplete input is a warning. This is not a SQL grammar validator.
    """
    return Diagnostic(**_sql.diagnose(sql, int(Dialect(dialect))))
