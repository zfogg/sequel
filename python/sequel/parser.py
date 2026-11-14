"""Shared parser/query document access for Python tooling."""

from . import _sql
from .dialect import Dialect


def parse_document(sql: str, dialect: Dialect = Dialect.GENERIC) -> dict:
    """Return copied token, statement, relation, and recovery records."""
    return _sql.parse_document(sql, int(Dialect(dialect)))
