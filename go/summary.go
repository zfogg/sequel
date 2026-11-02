package sequel

/*
#cgo CFLAGS: -I${SRCDIR}/../include -I${SRCDIR}/../third_party -DUTF8PROC_STATIC
#include <stdlib.h>
#include <sequel.h>
*/
import "C"

import (
	"strings"
	"unsafe"
)

// Dialect selects SQL quoting, placeholder, and sanitization rules.
type Dialect string

const (
	// DialectGeneric selects the engine's generic SQL lexical rules.
	DialectGeneric Dialect = "generic"
	// DialectPostgreSQL supports PostgreSQL quoting, dollar strings and bind markers.
	DialectPostgreSQL Dialect = "postgresql"
	// DialectDuckDB selects DuckDB lexical rules, including dollar-quoted strings.
	DialectDuckDB Dialect = "duckdb"
	// DialectSQLite supports SQLite quoting and numbered, named and Tcl-style binds.
	DialectSQLite Dialect = "sqlite"
	// DialectMySQL supports MySQL escaping, charset-prefixed strings and comments.
	DialectMySQL Dialect = "mysql"
	// DialectClickHouse supports ClickHouse quoting, comments and typed parameters.
	// INSERT FORMAT data payloads are not interpreted as SQL.
	DialectClickHouse Dialect = "clickhouse"
	// DefaultMaxSummaryBytes documents the shared engine's 32768-byte summary cap.
	// It is not an option that can be changed for an individual Summary call.
	DefaultMaxSummaryBytes = 32 * 1024
)

// nativeDialect maps Go dialect names to C enums; unknown names fall back to generic.
// Strict public operations validate their dialect before using this helper.
func nativeDialect(d Dialect) C.sequel_dialect_t {
	switch strings.ToLower(string(d)) {
	case "sqlite":
		return C.SEQUEL_SQLITE
	case "postgresql":
		return C.SEQUEL_POSTGRESQL
	case "duckdb":
		return C.SEQUEL_DUCKDB
	case "clickhouse":
		return C.SEQUEL_CLICKHOUSE
	case "mysql":
		return C.SEQUEL_MYSQL
	default:
		return C.SEQUEL_GENERIC
	}
}

// inspectNative obtains a summary and operation without collecting query text.
func inspectNative(d Dialect, query string) (string, string, bool) {
	summary, operation, _, _, ok := inspectNativeWithOptions(query, SanitizeOptions{Dialect: d, OmitQueryText: true})
	return summary, operation, ok
}

// inspectNativeWithOptions copies bounded C metadata and separately sanitizes text
// when a requested text limit exceeds the fixed metadata buffer.
func inspectNativeWithOptions(query string, policy SanitizeOptions) (string, string, SanitizedQuery, int, bool) {
	if strings.IndexByte(query, 0) >= 0 {
		return "", "", SanitizedQuery{}, 0, false
	}
	sql := C.CString(query)
	defer C.free(unsafe.Pointer(sql))
	limit := policy.MaxQueryTextBytes
	if limit == 0 {
		limit = DefaultMaxQueryTextBytes
	}
	largeLimit := limit < 0 || limit > C.SEQUEL_TEXT_MAX
	options := C.sequel_options_t{dialect: nativeDialect(policy.Dialect),
		omit_query_text: C.bool(policy.OmitQueryText || largeLimit),
		keep_in_lists:   C.bool(!policy.CollapseInLists)}
	if !largeLimit {
		options.max_query_text_bytes = C.size_t(limit)
	}
	var metadata C.sequel_metadata_t
	if C.sequel_inspect(sql, &options, &metadata) != C.SEQUEL_OK {
		return "", "", SanitizedQuery{}, 0, false
	}
	safe := SanitizedQuery{Text: C.GoString(&metadata.text[0]), Truncated: bool(metadata.truncated)}
	if largeLimit && !policy.OmitQueryText {
		var ok bool
		safe, ok = sanitizeNative(query, policy)
		if !ok {
			return "", "", SanitizedQuery{}, 0, false
		}
	}
	return C.GoString(&metadata.summary[0]), C.GoString(&metadata.operation[0]), safe, int(metadata.statement_count), true
}

// sanitizeNative enforces the input bound and copies a UTF-8-bounded C template.
func sanitizeNative(query string, policy SanitizeOptions) (SanitizedQuery, bool) {
	if strings.IndexByte(query, 0) >= 0 || len(query) > 1024*1024 {
		return SanitizedQuery{}, false
	}
	limit := policy.MaxQueryTextBytes
	if limit == 0 {
		limit = DefaultMaxQueryTextBytes
	}
	if limit < 0 || limit > len(query) {
		limit = len(query)
	}
	if limit == 0 {
		limit = 1
	}
	buf := make([]byte, limit+1)
	sql := C.CString(query)
	defer C.free(unsafe.Pointer(sql))
	var truncated C.bool
	status := C.sequel_sanitize(sql, nativeDialect(policy.Dialect), C.bool(policy.CollapseInLists),
		C.size_t(limit), (*C.char)(unsafe.Pointer(&buf[0])), C.size_t(len(buf)), &truncated)
	if status != C.SEQUEL_OK {
		return SanitizedQuery{}, false
	}
	return SanitizedQuery{Text: C.GoString((*C.char)(unsafe.Pointer(&buf[0]))), Truncated: bool(truncated)}, true
}

// Summary returns a bounded SQL skeleton from the sequel parser.
// It returns "SQL" when inspection fails or no summary is available. The result
// is limited to DefaultMaxSummaryBytes; no query is executed or bind value read.
func Summary(dialect Dialect, query string) string {
	summary, _, ok := inspectNative(dialect, query)
	if !ok || summary == "" {
		return "SQL"
	}
	return summary
}

// statementOperation uses the same parser as Summary, including CTE handling.
func statementOperation(dialect Dialect, statement string) string {
	_, operation, ok := inspectNative(dialect, statement)
	if !ok {
		return ""
	}
	return operation
}
