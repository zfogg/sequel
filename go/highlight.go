package sequel

/*
#cgo CFLAGS: -I${SRCDIR}/../include -I${SRCDIR}/../third_party
#include <stdlib.h>
#include <sequel.h>
*/
import "C"

import (
	"fmt"
	"strings"
	"unsafe"
)

// HighlightMode selects escaped HTML or truecolor ANSI output from HighlightSQL.
// The empty mode selects HighlightHTML.
type HighlightMode string

const (
	// HighlightHTML renders an escaped pre/code fragment with inline token colors.
	HighlightHTML HighlightMode = "html"
	// HighlightANSI renders truecolor SGR sequences and finishes with a reset.
	HighlightANSI HighlightMode = "ansi"
)

// Appearance selects a palette's actual published light/dark variant.
// Missing counterparts are errors; colors are never inverted or synthesized.
type Appearance string

const (
	// AppearanceAuto preserves the selected palette. The empty appearance does too.
	AppearanceAuto Appearance = "auto"
	// AppearanceDark selects the published dark counterpart, when available.
	AppearanceDark Appearance = "dark"
	// AppearanceLight selects the published light counterpart, when available.
	AppearanceLight Appearance = "light"
)

// HighlightOptions selects a Base16 palette and token overrides. Empty fields
// use HTML, default-dark and the palette's own appearance. SchemeYAML accepts
// any Base16 YAML palette instead of a built-in Colorscheme name. Colors maps
// token types (e.g. "variable") to #RRGGBB or RRGGBB; applied last.
// Explicit appearance selects an actual published variant; missing variants
// return an error. No colors are inverted or synthesized.
type HighlightOptions struct {
	// Mode selects HTML or ANSI output; empty means HighlightHTML.
	Mode HighlightMode
	// Colorscheme names a palette from Colorschemes; empty means "default-dark".
	// A nonempty name and SchemeYAML are mutually exclusive.
	Colorscheme string
	// SchemeYAML supplies Base16 YAML contents instead of a built-in palette name.
	// It must define all sixteen base00 through base0F colors; no file is opened.
	SchemeYAML string
	// Appearance selects a published counterpart; empty preserves the palette.
	// A custom YAML palette can switch only when a bundled counterpart is recognized.
	Appearance Appearance
	// Colors overrides token/wrapper colors after palette and appearance selection.
	// Values are #RRGGBB or RRGGBB. Keys are foreground, background, keyword, type,
	// function, variable, string, number, boolean, null, comment, operator,
	// punctuation and identifier. A nil map keeps all palette-derived colors.
	Colors map[string]string
	// Tolerant colors unfinished quotes, comments, identifiers and parameters
	// through EOF without inserting closing delimiters. Use Diagnose for warnings.
	// Invalid UTF-8, controls, limits and unsupported payloads remain errors.
	Tolerant bool
}

// Colorschemes lists the bundled Base16 palette names in sorted order.
// The returned slice belongs to the caller; changing it does not alter the registry.
func Colorschemes() []string {
	result := make([]string, int(C.sequel_colorscheme_count()))
	for i := range result {
		result[i] = C.GoString(C.sequel_colorscheme_name(C.size_t(i)))
	}
	return result
}

// HighlightSQL preserves spelling and whitespace, producing escaped HTML or
// truecolor ANSI. It neither formats nor sanitizes SQL. Invalid lexical input,
// terminal controls and invalid options return an empty result and an error.
// Input is limited to 1 GiB and 65536 tokens. Tolerant permits unfinished lexical
// constructs; delimiter balance is checked separately by Diagnose. SQL failures
// return *DiagnosticError. Invalid options may return other error types.
// Literal values and comments remain visible, so sanitize first when needed.
func HighlightSQL(dialect Dialect, query string, options HighlightOptions) (string, error) {
	if len(query) > MaxInputBytes || strings.IndexByte(query, 0) >= 0 {
		return "", &DiagnosticError{Diagnose(dialect, query)}
	}
	switch dialect {
	case "", DialectGeneric, DialectPostgreSQL, DialectDuckDB, DialectSQLite, DialectMySQL, DialectClickHouse:
	default:
		return "", fmt.Errorf("unsupported SQL dialect %q", dialect)
	}
	var native C.sequel_highlight_options_t
	C.sequel_highlight_options_init(&native)
	native.dialect = nativeDialect(dialect)
	native.tolerant = C.bool(options.Tolerant)
	switch options.Mode {
	case "", HighlightHTML:
	case HighlightANSI:
		native.mode = C.SEQUEL_ANSI
	default:
		return "", fmt.Errorf("unsupported highlight mode %q", options.Mode)
	}
	name := options.Colorscheme
	if name == "" {
		name = "default-dark"
	}
	source := name
	if options.SchemeYAML != "" {
		if options.Colorscheme != "" {
			return "", fmt.Errorf("Colorscheme and SchemeYAML are mutually exclusive")
		}
		source = options.SchemeYAML
	}
	if strings.IndexByte(source, 0) >= 0 {
		return "", fmt.Errorf("colorscheme contains NUL")
	}
	paletteSource := C.CString(source)
	defer C.free(unsafe.Pointer(paletteSource))
	var palette C.sequel_colorscheme_t
	var status C.sequel_status_t
	if options.SchemeYAML != "" {
		status = C.sequel_colorscheme_parse(paletteSource, &palette)
	} else {
		status = C.sequel_colorscheme(paletteSource, &palette)
	}
	if status != C.SEQUEL_OK {
		return "", fmt.Errorf("invalid or unknown Base16 colorscheme")
	}
	appearance := C.sequel_appearance_t(C.SEQUEL_APPEARANCE_AUTO)
	switch options.Appearance {
	case "", AppearanceAuto:
	case AppearanceDark:
		appearance = C.SEQUEL_APPEARANCE_DARK
	case AppearanceLight:
		appearance = C.SEQUEL_APPEARANCE_LIGHT
	default:
		return "", fmt.Errorf("unsupported appearance %q", options.Appearance)
	}
	if options.SchemeYAML != "" {
		status = C.sequel_colorscheme_set_appearance(&palette, appearance)
	} else {
		status = C.sequel_colorscheme_for_appearance(paletteSource, appearance, &palette)
	}
	if status != C.SEQUEL_OK ||
		C.sequel_highlight_set_colorscheme(&native, &palette) != C.SEQUEL_OK {
		return "", fmt.Errorf("colorscheme has no published variant for the requested appearance")
	}
	for kind, hex := range options.Colors {
		if strings.IndexByte(kind, 0) >= 0 || strings.IndexByte(hex, 0) >= 0 {
			return "", fmt.Errorf("color override contains NUL")
		}
		key, value := C.CString(kind), C.CString(hex)
		status = C.sequel_highlight_set_color(&native, key, value)
		C.free(unsafe.Pointer(key))
		C.free(unsafe.Pointer(value))
		if status != C.SEQUEL_OK {
			return "", fmt.Errorf("invalid color override %q: %q", kind, hex)
		}
	}
	sql := C.CString(query)
	defer C.free(unsafe.Pointer(sql))
	var length C.size_t
	var diagnostic C.sequel_diagnostic_t
	if C.sequel_highlight_ex(sql, &native, nil, 0, &length, &diagnostic) != C.SEQUEL_OK {
		return "", &DiagnosticError{diagnosticFromNative(&diagnostic)}
	}
	buf := make([]byte, int(length)+1)
	if C.sequel_highlight_ex(sql, &native, (*C.char)(unsafe.Pointer(&buf[0])), C.size_t(len(buf)), &length, &diagnostic) != C.SEQUEL_OK {
		return "", &DiagnosticError{diagnosticFromNative(&diagnostic)}
	}
	return string(buf[:int(length)]), nil
}
