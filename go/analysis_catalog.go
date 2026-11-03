package sequel

/*
#include <sequel.h>
*/
import "C"

import "errors"

// DiagnosticCatalog returns independent copies of the public canonical catalog.
// Consumers should use these entries instead of probing Diagnose with example SQL.
func DiagnosticCatalog() []Diagnostic {
	count := int(C.sequel_diagnostic_catalog_count())
	out := make([]Diagnostic, 0, count)
	for i := 0; i < count; i++ {
		var d C.sequel_diagnostic_t
		if C.sequel_diagnostic_catalog_get(C.size_t(i), &d) == C.SEQUEL_OK {
			out = append(out, diagnosticFromNative(&d))
		}
	}
	return out
}

// CanonicalizeDiagnostic validates a versioned transport diagnostic's code,
// severity, completion flags and ranges, then replaces incoming prose with trusted
// catalog text. It validates numeric structure without needing the original SQL.
func CanonicalizeDiagnostic(version, sourceLength int, d Diagnostic) (Diagnostic, error) {
	if version != DiagnosticSchemaVersion || sourceLength < 0 || d.Offset < 0 || d.EndOffset < d.Offset ||
		d.Line < 1 || d.Column < 1 || d.EndLine < 1 || d.EndColumn < 1 {
		return Diagnostic{}, errors.New("invalid diagnostic version or range")
	}
	var native C.sequel_diagnostic_t
	found := false
	for i := C.size_t(0); i < C.sequel_diagnostic_catalog_count(); i++ {
		var entry C.sequel_diagnostic_t
		C.sequel_diagnostic_catalog_get(i, &entry)
		if C.GoString(C.sequel_diagnostic_code_name(entry.code)) == d.Code {
			native.code = entry.code
			found = true
			break
		}
	}
	if !found {
		return Diagnostic{}, errors.New("unknown diagnostic code")
	}
	switch d.Severity {
	case "note":
		native.severity = C.SEQUEL_NOTE
	case "warning":
		native.severity = C.SEQUEL_WARNING
	case "error":
		native.severity = C.SEQUEL_ERROR
	default:
		return Diagnostic{}, errors.New("unknown diagnostic severity")
	}
	native.offset = C.size_t(d.Offset)
	native.end_offset = C.size_t(d.EndOffset)
	native.line = C.size_t(d.Line)
	native.column = C.size_t(d.Column)
	native.end_line = C.size_t(d.EndLine)
	native.end_column = C.size_t(d.EndColumn)
	native.incomplete = C.bool(d.Incomplete)
	native.statement_complete = C.bool(d.StatementComplete)
	var out C.sequel_diagnostic_t
	if C.sequel_diagnostic_canonicalize(C.uint(version), C.size_t(sourceLength), &native, &out) != C.SEQUEL_OK {
		return Diagnostic{}, errors.New("invalid diagnostic structure")
	}
	return diagnosticFromNative(&out), nil
}

// ValidateDiagnostic checks transport version, catalog identity and range/flag
// structure. Use CanonicalizeDiagnostic before displaying any received prose.
func ValidateDiagnostic(version, sourceLength int, d Diagnostic) error {
	_, err := CanonicalizeDiagnostic(version, sourceLength, d)
	return err
}
