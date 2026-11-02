package sequel

/*
#cgo CFLAGS: -I${SRCDIR}/../include -I${SRCDIR}/../third_party
#include <stdlib.h>
#include <sequel.h>
*/
import "C"

import "unsafe"

// FormatSQL formats SQL layout using sequel. It preserves comments, literals,
// identifiers and bind placeholders verbatim. It checks lexical/delimiter
// structure without validating database grammar, executing SQL, interpolating
// parameters or sanitizing sensitive values. Input is limited
// to 1 GiB, 65536 tokens and 64 nesting levels. Errors return the original SQL.
// Use errors.As with *DiagnosticError for a source range, explanation and hint.
func FormatSQL(dialect Dialect, query string) (string, error) {
	if diagnostic := Diagnose(dialect, query); diagnostic.Code != "none" {
		return query, &DiagnosticError{diagnostic}
	}
	sql := C.CString(query)
	defer C.free(unsafe.Pointer(sql))
	var length C.size_t
	var diagnostic C.sequel_diagnostic_t
	status := C.sequel_format_ex(sql, nativeDialect(dialect), nil, 0, &length, &diagnostic)
	if status != C.SEQUEL_OK {
		return query, &DiagnosticError{diagnosticFromNative(&diagnostic)}
	}
	buf := make([]byte, int(length)+1)
	status = C.sequel_format_ex(sql, nativeDialect(dialect), (*C.char)(unsafe.Pointer(&buf[0])), C.size_t(len(buf)), &length, &diagnostic)
	if status != C.SEQUEL_OK {
		return query, &DiagnosticError{diagnosticFromNative(&diagnostic)}
	}
	return string(buf[:int(length)]), nil
}

// FormatSQLOrOriginal is for presentation paths that must tolerate incomplete
// or unsupported input. Editors should use FormatSQL to surface errors.
func FormatSQLOrOriginal(dialect Dialect, query string) string {
	formatted, _ := FormatSQL(dialect, query)
	return formatted
}
