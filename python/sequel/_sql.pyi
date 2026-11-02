from typing import TypedDict

def inspect(
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
