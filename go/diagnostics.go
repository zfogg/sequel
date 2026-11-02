package sequel

/*
#cgo CFLAGS: -I${SRCDIR}/../include -I${SRCDIR}/../third_party
#include <stdlib.h>
#include <sequel.h>
*/
import "C"

import (
	"fmt"
	"unsafe"
)

// Diagnostic describes a lexical/delimiter issue, not database grammar. Offsets
// are UTF-8 bytes; lines/Unicode scalar columns are one-based; ends are exclusive.
type Diagnostic struct {
	// Code identifies the issue, such as "unterminated_string" or "input_limit".
	// "none" means the lexical and delimiter checks found no issue.
	Code string `json:"code"`
	// Severity is "note", "warning" for incomplete input, or "error".
	Severity string `json:"severity"`
	// Offset is the zero-based UTF-8 byte position where the issue starts.
	Offset int `json:"offset"`
	// EndOffset is the exclusive UTF-8 byte position where the issue ends.
	EndOffset int `json:"end_offset"`
	// Line is the one-based starting line. CRLF counts as one newline.
	Line int `json:"line"`
	// Column is the one-based starting Unicode scalar column; a tab counts as one.
	Column int `json:"column"`
	// EndLine is the one-based line containing the exclusive range end.
	EndLine int `json:"end_line"`
	// EndColumn is the one-based Unicode scalar column of the exclusive range end.
	EndColumn int `json:"end_column"`
	// Incomplete reports an unfinished quote, comment, parameter or delimiter group.
	// Hard failures, including invalid UTF-8 and exceeded limits, are not incomplete.
	Incomplete bool `json:"incomplete"`
	// StatementComplete requires balanced delimiters and a final semicolon outside
	// quotes/comments. It does not establish that the SQL is executable.
	StatementComplete bool `json:"statement_complete"`
	// Message is a short description that does not include input SQL.
	Message string `json:"message"`
	// Explanation describes the issue without interpolating input SQL.
	Explanation string `json:"explanation"`
	// Hint suggests how to resolve or handle the issue without including input SQL.
	Hint string `json:"hint"`
}

// DiagnosticError wraps a structured SQL failure from FormatSQL or HighlightSQL.
// Use errors.As to inspect it. Invalid highlight options may return other errors.
type DiagnosticError struct {
	// Diagnostic contains the source range, issue code, explanation and fix hint.
	Diagnostic Diagnostic
}

// Error returns the starting line/column, message and code without input SQL.
func (e *DiagnosticError) Error() string {
	return fmt.Sprintf("%d:%d: %s [%s]", e.Diagnostic.Line, e.Diagnostic.Column, e.Diagnostic.Message, e.Diagnostic.Code)
}

// diagnosticFromNative copies the C diagnostic into Go-owned strings and values.
func diagnosticFromNative(d *C.sequel_diagnostic_t) Diagnostic {
	severity := "note"
	if d.severity == C.SEQUEL_WARNING {
		severity = "warning"
	}
	if d.severity == C.SEQUEL_ERROR {
		severity = "error"
	}
	return Diagnostic{
		Code: C.GoString(C.sequel_diagnostic_code_name(d.code)), Severity: severity,
		Offset: int(d.offset), EndOffset: int(d.end_offset), Line: int(d.line), Column: int(d.column),
		EndLine: int(d.end_line), EndColumn: int(d.end_column), Incomplete: bool(d.incomplete),
		StatementComplete: bool(d.statement_complete), Message: C.GoString(&d.message[0]),
		Explanation: C.GoString(&d.explanation[0]), Hint: C.GoString(&d.hint[0]),
	}
}

// Diagnose locates and explains the first lexical/delimiter issue. Code "none"
// means no issue found; incomplete input is a warning. No SQL is executed.
// Input is limited to 1 GiB, 65536 tokens and 64 nesting levels. Embedded NULs,
// invalid UTF-8, controls and unsupported data payloads have distinct codes.
// An empty dialect selects generic SQL; an unknown dialect is invalid_argument.
// Use StatementComplete to detect a final semicolon outside quotes/comments.
func Diagnose(dialect Dialect, query string) Diagnostic {
	n := len(query)
	if len(query) > MaxInputBytes+1 {
		query = query[:MaxInputBytes+1]
	}
	input := C.CString(query)
	defer C.free(unsafe.Pointer(input))
	d := nativeDialect(dialect)
	switch dialect {
	case "", DialectGeneric, DialectSQLite, DialectPostgreSQL, DialectMySQL, DialectClickHouse, DialectDuckDB:
	default:
		d = C.sequel_dialect_t(255)
	}
	var result C.sequel_diagnostic_t
	C.sequel_diagnose_n(input, C.size_t(n), d, &result)
	return diagnosticFromNative(&result)
}
