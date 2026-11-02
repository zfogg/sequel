"""SQL layout formatting through the C engine."""

from __future__ import annotations

from . import _sql
from .dialect import Dialect


def format_sql(sql: str, *, dialect: Dialect = Dialect.GENERIC) -> str:
    return _sql.format(sql, int(Dialect(dialect)))


def format_sql_or_original(sql: str, *, dialect: Dialect = Dialect.GENERIC) -> str:
    try:
        return format_sql(sql, dialect=dialect)
    except ValueError:
        return sql
