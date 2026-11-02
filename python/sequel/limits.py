"""Byte limits shared with the C engine; buffers grow to actual content size."""

from typing import Final

from . import _sql

MAX_INPUT_BYTES: Final[int] = _sql.MAX_INPUT_BYTES
"""Maximum UTF-8 SQL input length (1 GiB)."""
DEFAULT_MAX_QUERY_TEXT_BYTES: Final[int] = _sql.DEFAULT_MAX_QUERY_TEXT_BYTES
"""Default sanitized query text limit (1 GiB)."""
DEFAULT_MAX_SUMMARY_BYTES: Final[int] = _sql.DEFAULT_MAX_SUMMARY_BYTES
"""Maximum structural summary length (1 GiB)."""
