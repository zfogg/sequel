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

    @property
    def kind(self) -> str:
        """Category used for policy decisions without inspecting the code."""
        if self.code in {"bind_missing", "bind_extra", "bind_invalid"}:
            return "bind"
        if self.code in {"input_limit", "output_limit", "resource_limit", "cancelled", "out_of_memory"}:
            return "policy"
        if self.code in {"select_star", "unbounded_write"}:
            return "safety"
        if self.code in {"large_in_list", "deep_query_nesting", "excessive_joins", "repeated_predicate"}:
            return "shape"
        if self.code == "dialect_portability":
            return "portability"
        if self.code in {"empty_statement", "multiple_statements", "mixed_operations"}:
            return "structure"
        if self.code in {"unknown_operation", "no_referenced_table", "sqlc_annotation"}:
            return "metadata"
        return "lexical"


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


