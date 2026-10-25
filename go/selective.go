package sequel

/*
#cgo CFLAGS: -I${SRCDIR}/../include
#include <sequel.h>
#include <stdlib.h>
*/
import "C"

import (
	"fmt"
	"strings"
	"unsafe"
)

// SanitizeSelective independently removes comments and replaces literals and
// bind markers. Retained comments or literals can contain sensitive values.
// Unlike bounded telemetry sanitization, this returns the complete result.
func SanitizeSelective(dialect Dialect, query string, removeComments, replaceLiterals bool) (string, error) {
	if len(query) > 1024*1024 || strings.IndexByte(query, 0) >= 0 {
		return "", fmt.Errorf("SQL exceeds 1 MiB or contains NUL")
	}
	switch dialect {
	case "", DialectGeneric, DialectSQLite, DialectPostgreSQL, DialectDuckDB, DialectMySQL, DialectClickHouse:
	default:
		return "", fmt.Errorf("unsupported SQL dialect %q", dialect)
	}
	var flags C.uint
	if removeComments {
		flags |= C.SEQUEL_REMOVE_COMMENTS
	}
	if replaceLiterals {
		flags |= C.SEQUEL_REPLACE_LITERALS
	}
	sql := C.CString(query)
	defer C.free(unsafe.Pointer(sql))
	buf := make([]byte, len(query)+2)
	var truncated C.bool
	status := C.sequel_sanitize_with_flags(sql, nativeDialect(dialect), flags, true,
		C.size_t(len(buf)-1), (*C.char)(unsafe.Pointer(&buf[0])), C.size_t(len(buf)), &truncated)
	if status != C.SEQUEL_OK || bool(truncated) {
		return "", fmt.Errorf("sanitize SQL: native status %d", status)
	}
	return C.GoString((*C.char)(unsafe.Pointer(&buf[0]))), nil
}
