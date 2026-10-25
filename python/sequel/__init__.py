"""SQL formatting, sanitization and structural inspection using sequel's C engine."""
from __future__ import annotations
from dataclasses import dataclass
from enum import IntEnum
from . import _sql

class Dialect(IntEnum):
    GENERIC = 0
    SQLITE = 1
    POSTGRESQL = 2
    DUCKDB = 3
    MYSQL = 4
    CLICKHOUSE = 5


class SQLInspectionError(ValueError):
    pass


@dataclass(frozen=True)
class Query:
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
    try:
        return Query(*_sql.inspect(sql, int(Dialect(dialect)), max_query_text_bytes, omit_query_text, keep_in_lists))
    except ValueError as exc:
        raise SQLInspectionError("SQL inspection rejected input") from exc


def format_sql(sql: str, *, dialect: Dialect = Dialect.GENERIC) -> str:
    return _sql.format(sql, int(Dialect(dialect)))


def format_sql_or_original(sql: str, *, dialect: Dialect = Dialect.GENERIC) -> str:
    try:
        return format_sql(sql, dialect=dialect)
    except ValueError:
        return sql



def sanitize(sql: str, *, dialect: Dialect = Dialect.GENERIC,
             remove_comments: bool = True, replace_literals: bool = True) -> str:
    """Select transformations; retained comments or literals may contain secrets."""
    return _sql.sanitize(sql, int(Dialect(dialect)), remove_comments, replace_literals)
