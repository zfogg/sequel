"""Versioned SQL analysis, numeric source provenance and bind metadata.

All ranges are half-open UTF-8 byte ranges. Analysis never retains original SQL,
comment contents, literal values, or bind names/values. ``formatted`` formats the
sanitized text; use the independent ``format_sql`` API to preserve literals.
Native integer arguments must fit the platform's size types; an unrepresentable
Python integer raises ``OverflowError``. Python/native allocation failures may
raise ``MemoryError`` while copying results or validating transported metadata.
"""

from __future__ import annotations

from collections.abc import Callable, Mapping, Sequence
from dataclasses import asdict, dataclass, fields
from typing import cast

from . import _sql
from .diagnostics import Diagnostic, SQLDiagnosticError
from .dialect import Dialect

ANALYSIS_SCHEMA_VERSION = 1
"""Current analysis transport schema; reject unsupported versions when decoding."""
DIAGNOSTIC_SCHEMA_VERSION = 1
"""Current public diagnostic catalog/validation schema, independent of SQL dialect."""
_MAP_KINDS = frozenset(("copy", "replace", "delete", "insert"))
_BIND_STYLES = frozenset(("positional", "numbered", "named", "typed"))
_STATUSES = frozenset(("ok", "malformed", "limit", "cancelled", "error"))
_REASONS = frozenset(("", "empty", "policy", "malformed", "analysis_limit", "analysis_error", "cancelled"))


def _record(value: object) -> Mapping[str, object]:
    if not isinstance(value, Mapping) or any(not isinstance(key, str) for key in value):
        raise ValueError("Expected an object with string keys")
    return cast(Mapping[str, object], value)


def _known_fields(data: Mapping[str, object], allowed: set[str]) -> None:
    if set(data) - allowed:
        raise ValueError("Unsupported analysis object fields")


def _field(data: Mapping[str, object], key: str) -> object:
    try:
        return data[key]
    except KeyError as exc:
        raise ValueError(f"Missing analysis field: {key}") from exc


def _integer(value: object, *, minimum: int = 0) -> int:
    if type(value) is not int or value < minimum:
        raise ValueError("Expected a nonnegative integer" if minimum == 0 else "Expected a positive integer")
    return value


def _boolean(value: object) -> bool:
    if type(value) is not bool:
        raise ValueError("Expected a boolean")
    return value


def _string(value: object) -> str:
    if not isinstance(value, str):
        raise ValueError("Expected a string")
    try:
        value.encode("utf-8")
    except UnicodeError as exc:
        raise ValueError("Expected valid UTF-8 text") from exc
    return value


def _sequence(value: object) -> Sequence[object]:
    if not isinstance(value, (list, tuple)):
        raise ValueError("Expected an array")
    return value


@dataclass(frozen=True)
class MapSegment:
    """Immutable numeric provenance for one edit; no source text is retained.

    Attributes:
        from_start: Zero-based inclusive UTF-8 byte offset in the source document.
        from_end: Exclusive source byte offset; equal to ``from_start`` for inserts.
        to_start: Zero-based inclusive UTF-8 byte offset in the destination.
        to_end: Exclusive destination byte offset; equal to ``to_start`` for deletes.
        kind: ``copy`` requires equal nonzero byte lengths; ``replace`` requires
            nonzero lengths on both sides; ``delete`` has an empty destination;
            ``insert`` has an empty source. No segment may be empty on both sides.

    Construction stores values without validation. ``SourceMap.map_range`` and
    transport decoding validate kinds, lengths, ordering and complete coverage.
    """

    from_start: int
    from_end: int
    to_start: int
    to_end: int
    kind: str


@dataclass(frozen=True)
class MappedRange:
    """Immutable mapped interval, owned by Python and independent of native data.

    Attributes:
        start: Zero-based inclusive UTF-8 byte offset in the mapping's destination.
        end: Exclusive byte offset; may equal ``start`` at a deletion anchor.
        exact: True when the interval follows copied bytes or exact edit endpoints;
            false through replacements, deletions or inserted layout. A false
            result preserves an edit envelope rather than interpolating secrets.
    """

    start: int
    end: int
    exact: bool


@dataclass(frozen=True)
class SourceMap:
    """Immutable complete provenance without storing either document's contents.

    Attributes:
        from_length: Source document length in UTF-8 bytes, excluding terminators.
        to_length: Destination document length in UTF-8 bytes.
        segments: Ordered immutable ``MapSegment`` tuple; defaults to empty. The
            segments must monotonically and completely cover both documents.
            An omitted analysis map has zero lengths and an empty tuple, even
            when the analysis contains nonempty text.

    Instances own Python numeric metadata and remain valid after native storage
    is released. Construction does not validate; mapping and decoding do.
    """

    from_length: int
    to_length: int
    segments: tuple[MapSegment, ...] = ()

    def map_range(self, start: int, end: int, *, reverse: bool = False) -> MappedRange:
        """Map a UTF-8 range forward or backward, preserving redaction provenance.

        A replacement interior maps to its entire replacement. Deleted regions
        map to empty anchors; mapping never reconstructs deleted source values.
        ``start`` and ``end`` are inclusive/exclusive UTF-8 byte boundaries in
        the source; ``reverse=True`` instead interprets them in the destination
        and maps back to the source. The default is forward mapping. Empty
        cursor ranges are accepted. Insertions strictly inside a nonempty range
        are included; insertions touching only its endpoints are excluded.
        An empty cursor at an insertion maps to its complete inserted envelope.
        The returned ``MappedRange`` owns only Python numbers. Invalid/incomplete
        maps, negative offsets and out-of-range intervals raise ``ValueError``.
        Numeric validation cannot establish scalar boundaries without the text.
        """
        _integer(self.from_length)
        _integer(self.to_length)
        _integer(start)
        _integer(end)
        _boolean(reverse)
        segments = []
        for segment in self.segments:
            if segment.kind not in _MAP_KINDS:
                raise ValueError("Unknown source-map kind")
            segments.append(
                (
                    _integer(segment.from_start),
                    _integer(segment.from_end),
                    _integer(segment.to_start),
                    _integer(segment.to_end),
                    segment.kind,
                )
            )
        return MappedRange(*_sql.map_range(self.from_length, self.to_length, segments, start, end, reverse))

    @classmethod
    def from_dict(cls, value: object) -> SourceMap:
        """Decode a map object with ``from_length``, ``to_length`` and ``segments``.

        Returns independent immutable Python metadata; does not mutate ``value``.
        Missing/unknown fields, invalid types, invalid kinds and incomplete or
        nonmonotone coverage raise ``ValueError``. Offsets and lengths are UTF-8
        bytes. An omitted map is represented by zero lengths and an empty array.
        """
        data = _record(value)
        _known_fields(data, {field.name for field in fields(cls)})
        segments = []
        for item in _sequence(_field(data, "segments")):
            row = _record(item)
            _known_fields(row, {field.name for field in fields(MapSegment)})
            segments.append(
                MapSegment(
                    _integer(_field(row, "from_start")),
                    _integer(_field(row, "from_end")),
                    _integer(_field(row, "to_start")),
                    _integer(_field(row, "to_end")),
                    _string(_field(row, "kind")),
                )
            )
        result = cls(_integer(_field(data, "from_length")), _integer(_field(data, "to_length")), tuple(segments))
        result.map_range(0, 0)
        return result


@dataclass(frozen=True)
class BindDescriptor:
    """Bind occurrence and identity within one statement, without names or values.

    Ordinals and slots are one-based; statement indices are zero-based. Explicit
    numbered markers retain their arity (``$3`` requires three supplied slots).
    Metadata is Python-owned and immutable; no driver values are accepted.

    Attributes:
        ordinal: One-based bind occurrence order across the complete document.
        slot: One-based driver binding identity within a statement. Named repeats
            share a slot; anonymous occurrences allocate successive slots.
            Explicit numbered markers retain their driver number, including gaps.
        statement_index: Zero-based index into ``Analysis.statements``; driver
            slot numbering restarts for each statement.
        offset: Inclusive original UTF-8 byte offset of the marker.
        end_offset: Exclusive original UTF-8 byte offset of the marker.
        style: Canonical syntax name: ``positional``, ``numbered``, ``named`` or
            ``typed``. Names, type spellings and values are never exported.

    Literal placeholders in sanitized text are not bind descriptors. Fingerprint
    parameter identity is independent of driver slots and includes literals too.
    """

    ordinal: int
    slot: int
    statement_index: int
    offset: int
    end_offset: int
    style: str


@dataclass(frozen=True)
class StatementRange:
    """Immutable statement range in original UTF-8 bytes.

    ``offset`` is the zero-based inclusive start; ``end_offset`` is exclusive.
    These Python-owned numbers refer to the original document, not sanitized or
    formatted text. Map both endpoints before underlining displayed analysis text.
    Empty/comment-only input produces no statement ranges.
    """

    offset: int
    end_offset: int


def canonicalize_diagnostic(
    diagnostic: Diagnostic,
    *,
    source_length: int,
    version: int = DIAGNOSTIC_SCHEMA_VERSION,
) -> Diagnostic:
    """Validate transported types/ranges and replace all prose from the catalog.

    Unknown schema versions, codes, severities and inconsistent locations raise
    ``ValueError``. Source length is measured in original UTF-8 bytes. This checks
    transport structure; it cannot prove locations match an unavailable source.
    ``diagnostic`` is not mutated; the return value is an independent immutable
    diagnostic with canonical message, explanation and hint. ``version`` defaults
    to ``DIAGNOSTIC_SCHEMA_VERSION``. ``source_length`` may exceed the accepted
    input ceiling for a rejected input-limit result; no source allocation occurs.
    """
    if _integer(version) != DIAGNOSTIC_SCHEMA_VERSION:
        raise ValueError("Unsupported diagnostic schema version")
    _integer(source_length)
    _string(diagnostic.code)
    _string(diagnostic.severity)
    _integer(diagnostic.offset)
    _integer(diagnostic.end_offset)
    _integer(diagnostic.line, minimum=1)
    _integer(diagnostic.column, minimum=1)
    _integer(diagnostic.end_line, minimum=1)
    _integer(diagnostic.end_column, minimum=1)
    _boolean(diagnostic.incomplete)
    _boolean(diagnostic.statement_complete)
    return Diagnostic(
        **_sql.canonicalize_diagnostic(
            version,
            source_length,
            diagnostic.code,
            diagnostic.severity,
            diagnostic.offset,
            diagnostic.end_offset,
            diagnostic.line,
            diagnostic.column,
            diagnostic.end_line,
            diagnostic.end_column,
            diagnostic.incomplete,
            diagnostic.statement_complete,
        )
    )


def validate_diagnostic(
    diagnostic: Diagnostic,
    *,
    source_length: int,
    version: int = DIAGNOSTIC_SCHEMA_VERSION,
) -> None:
    """Validate diagnostic metadata against original UTF-8 ``source_length``.

    Returns ``None`` on success without mutating the diagnostic. ``version``
    defaults to ``DIAGNOSTIC_SCHEMA_VERSION``. Unknown versions/codes, inconsistent
    severity/completion flags, invalid types and invalid ranges raise ``ValueError``.
    Incoming prose is ignored; use ``canonicalize_diagnostic`` to obtain trusted
    public prose. A source length larger than the input ceiling is permitted.
    """
    canonicalize_diagnostic(diagnostic, source_length=source_length, version=version)


def diagnostic_catalog() -> tuple[Diagnostic, ...]:
    """Return Python-owned canonical entries for ``DIAGNOSTIC_SCHEMA_VERSION``.

    The immutable tuple includes ``none`` and every public diagnostic code in
    numeric native-code order. Entries contain canonical severity, incomplete
    flag, message, explanation and hint, with zero-byte locations and one-based
    line/column origins. They describe categories, not an analyzed SQL document.
    No query input, filesystem access or driver interaction is needed.
    """
    return tuple(Diagnostic(**entry) for entry in _sql.diagnostic_catalog())


def _diagnostic_from_dict(value: object, source_length: int) -> Diagnostic:
    row = _record(value)
    _known_fields(row, {field.name for field in fields(Diagnostic)})
    # Ignore untrusted prose entirely; canonicalization supplies the public text.
    diagnostic = Diagnostic(
        code=_string(_field(row, "code")),
        severity=_string(_field(row, "severity")),
        offset=_integer(_field(row, "offset")),
        end_offset=_integer(_field(row, "end_offset")),
        line=_integer(_field(row, "line"), minimum=1),
        column=_integer(_field(row, "column"), minimum=1),
        end_line=_integer(_field(row, "end_line"), minimum=1),
        end_column=_integer(_field(row, "end_column"), minimum=1),
        incomplete=_boolean(_field(row, "incomplete")),
        statement_complete=_boolean(_field(row, "statement_complete")),
        message="",
        explanation="",
        hint="",
    )
    return canonicalize_diagnostic(diagnostic, source_length=source_length)


@dataclass(frozen=True)
class Analysis:
    """Immutable, Python-owned result from the shared native analyzer.

    Empty diagnostics mean lexical/delimiter analysis succeeded only when status
    is ``ok``. A fingerprint does not assert that SQL passes database grammar.
    The wire format is versioned; maps describe exactly ``text``/``formatted``.
    All strings, tuples and numeric metadata remain valid after native storage
    is released. Original SQL, literal values, bind names and values are absent.

    Attributes:
        schema_version: Analysis transport schema, currently ``ANALYSIS_SCHEMA_VERSION``.
        dialect: Dialect used for quoting, marker rules and fingerprint namespace.
        status: ``ok``, ``malformed``, ``limit``, ``cancelled`` or ``error``.
        omission_reason: Empty when text is present; otherwise ``empty``, ``policy``,
            ``malformed``, ``analysis_limit``, ``cancelled`` or ``analysis_error``.
        text: Sanitized UTF-8 query template with comments removed and literals/
            binds replaced. Empty on omission, empty input or analysis failure.
        summary: Structural naming summary; ``SQL`` is the generic fallback.
        operation: Shared uppercase operation name, such as ``SELECT``; may be
            empty when no operation can be established or analysis fails.
        formatted: Optional layout of sanitized ``text``. Empty unless requested
            and supported; truncated text is not formatted. It contains no raw
            literal or bind values.
        fingerprint: Versioned dialect-specific structural SHA-256 identity.
            Empty for empty/comment-only input and any failed analysis.
        truncated: True if the independent query-text byte cap shortened text;
            the fingerprint still describes the complete checked document.
        original_length: Original UTF-8 byte length, including rejected input.
            It can exceed the accepted-input ceiling on an input-limit result.
        statement_count: Number of entries in ``statements``; zero for empty input.
        diagnostics: Canonical diagnostic tuple with original UTF-8 locations.
            Safe recovery can report multiple statements' issues. Empty on success.
        binds: Original bind occurrence descriptors. Empty if omitted or failed;
            sanitized placeholders originating from literals are not included.
        statements: Ordered original UTF-8 statement ranges; locations must be
            mapped before applying them to displayed sanitized/formatted strings.
        original_to_sanitized: Complete numeric map from original bytes to ``text``.
            An omitted/unavailable map has zero lengths and no segments.
        sanitized_to_formatted: Complete map from ``text`` to ``formatted``;
            zero-length empty map when formatting/maps are omitted or unavailable.
        memory_peak_bytes: Peak live native working allocation bytes, including
            intermediates/context. Excludes Python objects and JSON serialization.
            Zero when accounting could not begin.
        work_units: Counted native scan/hash/copy/transform steps; zero when
            accounting could not begin. Units are not time and are not a stable
            performance comparison across library versions.

    Malformed/resource/cancellation failures clear text, formatted text,
    fingerprints, maps and bind descriptors. Numeric diagnostics can remain.
    """

    schema_version: int
    dialect: Dialect
    status: str
    omission_reason: str
    text: str
    summary: str
    operation: str
    formatted: str
    fingerprint: str
    truncated: bool
    original_length: int
    statement_count: int
    diagnostics: tuple[Diagnostic, ...]
    binds: tuple[BindDescriptor, ...]
    statements: tuple[StatementRange, ...]
    original_to_sanitized: SourceMap
    sanitized_to_formatted: SourceMap
    memory_peak_bytes: int
    work_units: int

    def to_dict(self) -> dict[str, object]:
        """Return independent schema-v1 JSON-compatible data without source SQL.

        Validates the result and canonicalizes diagnostic prose before encoding;
        invalid metadata raises ``ValueError``. Dialect is its lowercase name,
        tuples become JSON arrays and all ranges remain UTF-8 byte offsets. The
        input instance is unchanged. Python allocation is outside native budgets.
        """
        return type(self).from_dict(self._as_dict())._as_dict()

    def _as_dict(self) -> dict[str, object]:
        result: dict[str, object] = {
            "schema_version": self.schema_version,
            "dialect": self.dialect.name.lower() if isinstance(self.dialect, Dialect) else self.dialect,
            "status": self.status,
            "omission_reason": self.omission_reason,
            "text": self.text,
            "summary": self.summary,
            "operation": self.operation,
            "formatted": self.formatted,
            "fingerprint": self.fingerprint,
            "truncated": self.truncated,
            "original_length": self.original_length,
            "statement_count": self.statement_count,
            "memory_peak_bytes": self.memory_peak_bytes,
            "work_units": self.work_units,
        }
        # JSON arrays, independent of whether the caller uses a JSON encoder.
        result["diagnostics"] = [asdict(item) for item in self.diagnostics]
        result["binds"] = [asdict(item) for item in self.binds]
        result["statements"] = [asdict(item) for item in self.statements]
        for key, mapping in (
            ("original_to_sanitized", self.original_to_sanitized),
            ("sanitized_to_formatted", self.sanitized_to_formatted),
        ):
            result[key] = {
                "from_length": mapping.from_length,
                "to_length": mapping.to_length,
                "segments": [asdict(segment) for segment in mapping.segments],
            }
        return result

    @classmethod
    def from_dict(cls, value: object) -> Analysis:
        """Validate schema-v1 transport and canonicalize untrusted diagnostics.

        Maps and ranges are checked against known document lengths. This cannot
        establish that incoming text was sanitized: accept text, summaries and
        fingerprints only from a trusted analyzer. Unknown fields are rejected.
        ``value`` is a decoded object, not a JSON string; it is not mutated.
        Missing fields, unsupported schema/codes, invalid types/ranges and
        inconsistent status/reasons raise ``ValueError``. The returned result
        owns immutable Python data and uses the current analysis schema. Incoming
        diagnostic prose is replaced from the catalog. Rejected-input lengths
        above the accepted-input ceiling are preserved without source allocation.
        """
        data = _record(value)
        _known_fields(data, {field.name for field in fields(cls)})
        if _integer(_field(data, "schema_version")) != ANALYSIS_SCHEMA_VERSION:
            raise ValueError("Unsupported analysis schema version")
        dialect_value = _field(data, "dialect")
        if isinstance(dialect_value, str):
            try:
                dialect = Dialect[dialect_value.upper()]
            except KeyError as exc:
                raise ValueError("Unsupported analysis dialect") from exc
        else:
            dialect = Dialect(_integer(dialect_value))
        status, reason = _string(_field(data, "status")), _string(_field(data, "omission_reason"))
        if status not in _STATUSES or reason not in _REASONS:
            raise ValueError("Unsupported analysis status or omission reason")
        text = _string(_field(data, "text"))
        formatted = _string(_field(data, "formatted"))
        fingerprint_value = _string(_field(data, "fingerprint"))
        original_length = _integer(_field(data, "original_length"))
        statements = []
        for item in _sequence(_field(data, "statements")):
            row = _record(item)
            _known_fields(row, {field.name for field in fields(StatementRange)})
            start, end = _integer(_field(row, "offset")), _integer(_field(row, "end_offset"))
            if start > end or end > original_length or (statements and start < statements[-1].end_offset):
                raise ValueError("Invalid statement range")
            statements.append(StatementRange(start, end))
        statement_count = _integer(_field(data, "statement_count"))
        if statement_count != len(statements):
            raise ValueError("Statement count does not match ranges")
        binds = []
        for item in _sequence(_field(data, "binds")):
            row = _record(item)
            _known_fields(row, {field.name for field in fields(BindDescriptor)})
            bind = BindDescriptor(
                _integer(_field(row, "ordinal"), minimum=1),
                _integer(_field(row, "slot"), minimum=1),
                _integer(_field(row, "statement_index")),
                _integer(_field(row, "offset")),
                _integer(_field(row, "end_offset")),
                _string(_field(row, "style")),
            )
            if (
                bind.style not in _BIND_STYLES
                or bind.offset >= bind.end_offset
                or bind.end_offset > original_length
                or bind.statement_index >= statement_count
                or bind.ordinal != len(binds) + 1
                or (binds and bind.offset < binds[-1].end_offset)
                or (binds and bind.statement_index < binds[-1].statement_index)
            ):
                raise ValueError("Invalid bind descriptor")
            statement = statements[bind.statement_index]
            if bind.offset < statement.offset or bind.end_offset > statement.end_offset:
                raise ValueError("Bind descriptor lies outside its statement")
            binds.append(bind)
        source_map = SourceMap.from_dict(_field(data, "original_to_sanitized"))
        format_map = SourceMap.from_dict(_field(data, "sanitized_to_formatted"))
        if source_map.segments and (
            source_map.from_length != original_length or source_map.to_length != len(text.encode("utf-8"))
        ):
            raise ValueError("Source map lengths do not match analysis documents")
        if format_map.segments and (
            format_map.from_length != len(text.encode("utf-8"))
            or format_map.to_length != len(formatted.encode("utf-8"))
        ):
            raise ValueError("Formatting map lengths do not match analysis documents")
        if status != "ok" and (
            text or formatted or fingerprint_value or binds or source_map.segments or format_map.segments
        ):
            raise ValueError("Failed analysis must not expose partial transformed data")
        if (text and reason) or (not text and not reason):
            raise ValueError("Omission reason is inconsistent with query text")
        if status != "ok":
            expected = {
                "malformed": "malformed",
                "limit": "analysis_limit",
                "cancelled": "cancelled",
                "error": "analysis_error",
            }
            if reason != expected[status] or _boolean(_field(data, "truncated")):
                raise ValueError("Failure status is inconsistent with omission reason")
        elif not text and reason not in ("empty", "policy"):
            raise ValueError("Successful analysis has an invalid omission reason")
        if fingerprint_value:
            prefix = f"sequel:v1:{dialect.name.lower()}:"
            digest = fingerprint_value.removeprefix(prefix)
            if (
                not fingerprint_value.startswith(prefix)
                or len(digest) != 64
                or any(c not in "0123456789abcdef" for c in digest)
            ):
                raise ValueError("Unsupported fingerprint version, dialect or digest")
        diagnostics = tuple(
            _diagnostic_from_dict(item, original_length) for item in _sequence(_field(data, "diagnostics"))
        )
        if status == "ok" and any(item.severity == "error" for item in diagnostics):
            raise ValueError("Successful analysis cannot contain error diagnostics")
        return cls(
            ANALYSIS_SCHEMA_VERSION,
            dialect,
            status,
            reason,
            text,
            _string(_field(data, "summary")),
            _string(_field(data, "operation")),
            formatted,
            fingerprint_value,
            _boolean(_field(data, "truncated")),
            original_length,
            statement_count,
            diagnostics,
            tuple(binds),
            tuple(statements),
            source_map,
            format_map,
            _integer(_field(data, "memory_peak_bytes")),
            _integer(_field(data, "work_units")),
        )


def analyze(
    sql: str,
    *,
    dialect: Dialect = Dialect.GENERIC,
    max_input_bytes: int = 0,
    max_output_bytes: int = 0,
    max_memory_bytes: int = 0,
    max_work_units: int = 0,
    max_query_text_bytes: int = 0,
    omit_query_text: bool = False,
    format: bool = False,
    keep_in_lists: bool = False,
    omit_source_maps: bool = False,
    omit_bind_descriptors: bool = False,
    include_lints: bool = False,
    cancelled: Callable[[], bool] | None = None,
) -> Analysis:
    """Analyze SQL once without executing it or inspecting driver values.

    Zero input/output/memory limits use the native 1 GiB default; zero work means
    unlimited. ``max_output_bytes`` budgets the total generated output, whereas
    ``max_query_text_bytes`` independently truncates query text (zero or negative
    uses the default ceiling). A cancellation callback is checked during native
    work; callback exceptions propagate. Malformed/limited/cancelled input returns
    structured failure with no partial text or fingerprint. Original input bytes
    are never retained. Formatting uses sanitized text and has its own source map.
    ``omit_source_maps`` and ``omit_bind_descriptors`` skip metadata generation
    when a caller needs only naming, sanitized text, diagnostics and fingerprints.

    Args:
        sql: Caller-owned Python SQL string, encoded as UTF-8 for native analysis.
            Embedded NUL is diagnosed. The result does not retain the original.
        dialect: Quoting/comment/marker policy; defaults to ``Dialect.GENERIC``.
        max_input_bytes: Accepted UTF-8 input byte ceiling; zero uses 1 GiB.
            Smaller positive values are per-call limits. Exceeding it returns
            status ``limit`` without exporting partial text.
        max_output_bytes: Total generated native strings/metadata byte budget;
            zero uses 1 GiB. Includes maps and descriptor/range arrays, not just
            query text. Exceeding it returns status ``limit``.
        max_memory_bytes: Live native allocation budget in bytes, including
            intermediate arrays, buffers and call context; zero uses 1 GiB.
            Excludes Python result objects and subsequent transport serialization.
        max_work_units: Native scan/hash/copy/transform step budget. Zero is
            unlimited; exceeding a positive value returns status ``limit``.
            Work units are implementation-specific counts, not milliseconds.
        max_query_text_bytes: Independent UTF-8 display-text cap, excluding any
            native NUL terminator. Zero or negative selects the 1 GiB ceiling.
            Truncation preserves scalar boundaries and sets ``truncated`` without
            changing the full-document fingerprint or structural metadata.
        omit_query_text: Defaults to False. True omits ``text``/``formatted`` and
            their maps with reason ``policy``, retaining other checked metadata.
        format: Defaults to False. True requests layout of sanitized text and its
            source map. Unsupported layout or truncation leaves formatted output
            empty while successful lexical analysis remains status ``ok``.
        keep_in_lists: Defaults to False, collapsing IN lists consisting only of
            literals/binds to one placeholder. True preserves their item count
            in sanitized text. Fingerprints always describe the full token stream.
        omit_source_maps: Defaults to False. True skips generation and allocation
            of both numeric maps; returned maps have zero lengths/empty segments.
        omit_bind_descriptors: Defaults to False. True skips descriptor generation
            and returns an empty bind tuple; fingerprints retain binding identity.
        cancelled: Optional no-argument callback, default None. A truthy return
            during native work stops with status ``cancelled``. Callback exceptions
            propagate; the callback must not call ``analyze`` recursively.

    Returns:
        An immutable Python-owned ``Analysis``; native storage is released before
        returning. Malformed SQL, native resource limits and cancellation are
        structured results with canonical diagnostics and no partial SQL payload.

    Raises:
        ValueError: Negative/bool resource limits, nonboolean flags, an unknown
            Python dialect, noninteger limits, or invalid transport produced by
            the native contract.
        OverflowError: A numeric native option does not fit the platform size type.
        TypeError: SQL/callback argument types are invalid.
        UnicodeError: SQL contains scalars that cannot be encoded as UTF-8.

    Input/output/query-text options above their native 1 GiB ceilings are rejected
    as structured ``invalid_argument`` diagnostics. Python allocation failures and
    cancellation-callback exceptions propagate normally. No SQL is executed.
    """
    for limit in (max_input_bytes, max_output_bytes, max_memory_bytes, max_work_units):
        _integer(limit)
    if type(max_query_text_bytes) is not int:
        raise ValueError("Query text limit must be an integer")
    for flag in (omit_query_text, format, keep_in_lists, omit_source_maps, omit_bind_descriptors, include_lints):
        _boolean(flag)
    result = _sql.analyze(
        sql,
        int(Dialect(dialect)),
        max_input_bytes,
        max_output_bytes,
        max_memory_bytes,
        max_work_units,
        max(0, max_query_text_bytes),
        omit_query_text,
        format,
        keep_in_lists,
        omit_source_maps,
        omit_bind_descriptors,
        include_lints,
        cancelled,
    )
    return Analysis.from_dict(result)


def fingerprint(sql: str, *, dialect: Dialect = Dialect.GENERIC) -> str:
    """Return a versioned dialect-specific SHA-256 structural fingerprint.

    Literals, comments, whitespace and bind spelling are excluded. Token framing,
    quoted identifiers, operators and repeated bind relationships are preserved.
    Each literal and fresh untyped bind has the same anonymous parameter shape;
    equal literal values remain independent while repeated binds share identity.
    Ordinary words use ASCII case folding in generic, SQLite, PostgreSQL and
    DuckDB; MySQL and ClickHouse retain word spelling because identifier/function
    case can be significant. Quoted identifiers and ClickHouse type words retain
    their spelling. Typed parameters include a separately framed type digest.
    Malformed input raises ``SQLDiagnosticError`` and never yields a fingerprint.
    SHA-256 collisions remain theoretically possible. This is a lexical shape
    identity, not a database grammar verdict or an unbounded metric dimension.
    ``sql`` is not retained; ``dialect`` defaults to ``Dialect.GENERIC``. Uses the
    default analysis budgets and returns a Python-owned string, or an empty
    string for empty/comment-only input. Native analysis failures raise
    ``SQLDiagnosticError`` with canonical diagnostic prose; invalid Python
    arguments follow ``analyze``'s exception behavior.
    """
    result = analyze(sql, dialect=dialect, omit_query_text=True)
    if result.status != "ok":
        if result.diagnostics:
            raise SQLDiagnosticError(result.diagnostics[0])
        raise ValueError("SQL fingerprint analysis failed")
    return result.fingerprint


def utf8_to_utf16(text: str, offset: int) -> int:
    """Convert zero-based UTF-8 ``offset`` in ``text`` to global UTF-16 units.

    Returns an integer; does not retain or modify text. Offset may equal the
    UTF-8 document length. CR/LF each count one unit, astral scalars two and
    combining marks one. Negative/out-of-range offsets and split scalar byte
    positions raise ``ValueError``. Text beyond the native 1 GiB ceiling is also
    rejected. Conversion failure never returns an approximate position.
    """
    return _sql.utf8_to_utf16(text, _integer(offset))


def utf16_to_utf8(text: str, units: int) -> int:
    """Convert global UTF-16 units to bytes; reject split astral surrogate pairs.

    CR and LF each count one unit; combining marks each remain distinct scalars.
    These are document offsets, independent of one-based diagnostic line columns.
    ``text`` is caller-owned and not retained or changed. ``units`` is a zero-based
    global count and may equal the document's UTF-16 length. Returns an integer
    UTF-8 byte boundary. Negative/out-of-range positions, split surrogate pairs
    and text beyond the native 1 GiB ceiling raise ``ValueError``; no approximate
    position is returned on failure.
    """
    return _sql.utf16_to_utf8(text, _integer(units))


def check_binds(binds: Sequence[BindDescriptor], supplied_slots: int, *, statement_index: int = 0) -> None:
    """Validate a statement's supplied arity using descriptors without bind values.

    Repeated named markers share a slot, numbered markers retain explicit arity,
    and positional markers allocate new slots. Mismatches raise ``ValueError``.
    ``binds`` is a caller-owned sequence of descriptors, usually ``Analysis.binds``;
    it is not modified or retained. ``statement_index`` is zero-based and defaults
    to the first statement. ``supplied_slots`` is the nonnegative supplied arity,
    which must equal that statement's maximum driver slot, including numbered
    gaps. A statement with no descriptors requires zero slots. Returns ``None``
    on success. Invalid styles, descriptor ranges, indices or counts also raise
    ``ValueError``. No values, mapping keys or type checks are accepted.
    """
    _integer(supplied_slots)
    _integer(statement_index)
    values = []
    for bind in binds:
        if bind.style not in _BIND_STYLES:
            raise ValueError("Unknown bind style")
        values.append(
            (
                _integer(bind.ordinal, minimum=1),
                _integer(bind.slot, minimum=1),
                _integer(bind.statement_index),
                _integer(bind.offset),
                _integer(bind.end_offset),
                bind.style,
            )
        )
    _sql.check_binds(values, statement_index, supplied_slots)


def check_binds_diagnostic(
    binds: Sequence[BindDescriptor], supplied_slots: int, *, statement_index: int = 0
) -> Diagnostic | None:
    """Return a canonical bind diagnostic, or ``None`` when arity is valid."""
    _integer(supplied_slots)
    _integer(statement_index)
    needed = 0
    for bind in binds:
        if bind.style not in _BIND_STYLES:
            return Diagnostic(
                "bind_invalid",
                "error",
                bind.offset,
                bind.end_offset,
                1,
                1,
                1,
                1,
                False,
                False,
                "Invalid bind descriptors",
                "Bind metadata is inconsistent or contains an unsupported style.",
                "Regenerate descriptors from the original SQL and preserve marker order and ranges.",
            )
        if bind.statement_index == statement_index:
            needed = max(needed, bind.slot)
    if supplied_slots == needed:
        check_binds(binds, supplied_slots, statement_index=statement_index)
        return None
    code = "bind_missing" if supplied_slots < needed else "bind_extra"
    message = "Missing bind values" if code == "bind_missing" else "Extra bind values"
    explanation = (
        "The supplied bind count is smaller than the slots required by the statement."
        if code == "bind_missing"
        else "The supplied bind count exceeds the slots used by the statement."
    )
    hint = (
        "Supply every required bind slot, including numbered gaps."
        if code == "bind_missing"
        else "Remove unused values or update the SQL template and its bind descriptors."
    )
    return Diagnostic(code, "error", 0, 0, 1, 1, 1, 1, False, False, message, explanation, hint)
