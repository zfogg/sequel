"""Structural SQL inspection through the C engine."""

from __future__ import annotations

from dataclasses import dataclass

from . import _sql
from .dialect import Dialect


class SQLInspectionError(ValueError):
    """Inspection could not safely tokenize the supplied SQL."""


@dataclass(frozen=True)
class Query:
    """Sanitized template, structural summary, operation and statement count."""

    text: str
    summary: str
    operation: str
    truncated: bool
    statement_count: int


def inspect(
    sql: str,
    *,
    dialect: Dialect = Dialect.GENERIC,
    max_query_text_bytes: int = 32768,
    omit_query_text: bool = False,
    keep_in_lists: bool = False,
) -> Query:
    """Inspect SQL without executing it or reading driver bind values.

    Comments and literals are removed from query text. Malformed input raises
    `SQLInspectionError`. Query text defaults to a 32768-byte limit; use
    `omit_query_text` when only structural metadata should be collected.
    """
    try:
        return Query(*_sql.inspect(sql, int(Dialect(dialect)), max_query_text_bytes, omit_query_text, keep_in_lists))
    except ValueError as exc:
        raise SQLInspectionError("SQL inspection rejected input") from exc
