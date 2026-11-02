"""SQL dialect identifiers."""

from __future__ import annotations

from enum import IntEnum


class Dialect(IntEnum):
    """Select the shared engine's quote, comment and bind-marker rules."""

    GENERIC = 0
    SQLITE = 1
    POSTGRESQL = 2
    DUCKDB = 3
    MYSQL = 4
    CLICKHOUSE = 5
