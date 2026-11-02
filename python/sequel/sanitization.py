"""Selective SQL sanitization through the C engine."""

from __future__ import annotations

from . import _sql
from .dialect import Dialect


def sanitize(
    sql: str, *, dialect: Dialect = Dialect.GENERIC, remove_comments: bool = True, replace_literals: bool = True
) -> str:
    """Select transformations; retained comments or literals may contain secrets."""
    return _sql.sanitize(sql, int(Dialect(dialect)), remove_comments, replace_literals)
