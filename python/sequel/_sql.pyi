from collections.abc import Callable
from typing import Final, TypedDict

MAX_INPUT_BYTES: Final[int]
DEFAULT_MAX_QUERY_TEXT_BYTES: Final[int]
DEFAULT_MAX_SUMMARY_BYTES: Final[int]

def metadata(
    sql: str,
    dialect: int,
    max_query_text_bytes: int,
    omit_query_text: bool,
    keep_in_lists: bool,
) -> tuple[str, str, str, bool, int]: ...

class DiagnosticDict(TypedDict):
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

def diagnose(sql: str, dialect: int) -> DiagnosticDict: ...
def format(sql: str, dialect: int) -> tuple[str | None, DiagnosticDict]: ...
def sanitize(sql: str, dialect: int, remove_comments: bool, replace_literals: bool) -> str: ...
def colorschemes() -> tuple[str, ...]: ...
def highlight(
    sql: str,
    dialect: int,
    mode: int,
    colorscheme: str,
    scheme_yaml: str,
    appearance: int,
    colors: dict[str, str],
    tolerant: bool,
) -> tuple[str | None, DiagnosticDict]: ...

class MapSegmentDict(TypedDict):
    from_start: int
    from_end: int
    to_start: int
    to_end: int
    kind: str

class SourceMapDict(TypedDict):
    from_length: int
    to_length: int
    segments: list[MapSegmentDict]

class BindDescriptorDict(TypedDict):
    ordinal: int
    slot: int
    statement_index: int
    offset: int
    end_offset: int
    style: str

class StatementRangeDict(TypedDict):
    offset: int
    end_offset: int

class AnalysisDict(TypedDict):
    schema_version: int
    dialect: int
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
    diagnostics: list[DiagnosticDict]
    binds: list[BindDescriptorDict]
    statements: list[StatementRangeDict]
    original_to_sanitized: SourceMapDict
    sanitized_to_formatted: SourceMapDict
    memory_peak_bytes: int
    work_units: int

def analyze(
    sql: str,
    dialect: int,
    max_input_bytes: int,
    max_output_bytes: int,
    max_memory_bytes: int,
    max_work_units: int,
    max_query_text_bytes: int,
    omit_query_text: bool,
    format: bool,
    keep_in_lists: bool,
    omit_source_maps: bool,
    omit_bind_descriptors: bool,
    include_lints: bool,
    cancelled: Callable[[], bool] | None,
) -> AnalysisDict: ...
def diagnostic_catalog() -> tuple[DiagnosticDict, ...]: ...
def canonicalize_diagnostic(
    version: int,
    source_length: int,
    code: str,
    severity: str,
    offset: int,
    end_offset: int,
    line: int,
    column: int,
    end_line: int,
    end_column: int,
    incomplete: bool,
    statement_complete: bool,
) -> DiagnosticDict: ...
def map_range(
    from_length: int,
    to_length: int,
    segments: list[tuple[int, int, int, int, str]],
    start: int,
    end: int,
    reverse: bool,
) -> tuple[int, int, bool]: ...
def utf8_to_utf16(text: str, offset: int) -> int: ...
def utf16_to_utf8(text: str, units: int) -> int: ...
def check_binds(
    binds: list[tuple[int, int, int, int, int, str]],
    statement_index: int,
    supplied_slots: int,
) -> None: ...
