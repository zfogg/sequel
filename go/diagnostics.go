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
	Code              string `json:"code"`
	Severity          string `json:"severity"`
	Offset            int    `json:"offset"`
	EndOffset         int    `json:"end_offset"`
	Line              int    `json:"line"`
	Column            int    `json:"column"`
	EndLine           int    `json:"end_line"`
	EndColumn         int    `json:"end_column"`
	Incomplete        bool   `json:"incomplete"`
	StatementComplete bool   `json:"statement_complete"`
	Message           string `json:"message"`
	Explanation       string `json:"explanation"`
	Hint              string `json:"hint"`
}
type DiagnosticError struct{ Diagnostic Diagnostic }

func (e *DiagnosticError) Error() string {
	return fmt.Sprintf("%d:%d: %s [%s]", e.Diagnostic.Line, e.Diagnostic.Column, e.Diagnostic.Message, e.Diagnostic.Code)
}
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
func Diagnose(dialect Dialect, query string) Diagnostic {
	n := len(query)
	if len(query) > 1024*1024+1 {
		query = query[:1024*1024+1]
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
