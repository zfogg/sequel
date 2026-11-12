"""Structured SQL metadata through the shared parser."""

from __future__ import annotations

from dataclasses import dataclass

from . import _sql
from .dialect import Dialect
from .limits import DEFAULT_MAX_QUERY_TEXT_BYTES


class SQLMetadataError(ValueError):
    """Metadata extraction rejected the supplied SQL."""


@dataclass(frozen=True)
class Query:
    """Sanitized text, structural summary, operation and statement count."""

    text: str
    summary: str
    operation: str
    truncated: bool
    statement_count: int


def metadata(
    sql: str,
    *,
    dialect: Dialect = Dialect.GENERIC,
    max_query_text_bytes: int = DEFAULT_MAX_QUERY_TEXT_BYTES,
    omit_query_text: bool = False,
    keep_in_lists: bool = False,
) -> Query:
    """Extract parser-backed metadata without executing SQL or reading binds."""
    try:
        return Query(*_sql.metadata(sql, int(Dialect(dialect)), max_query_text_bytes, omit_query_text, keep_in_lists))
    except ValueError as exc:
        raise SQLMetadataError("SQL metadata extraction rejected input") from exc


