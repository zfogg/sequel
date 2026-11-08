package sequel

/*
#cgo CFLAGS: -I${SRCDIR}/../include -I${SRCDIR}/../third_party
#include <stdlib.h>
#include <stdint.h>
#include <sequel.h>
extern bool sequelGoCancelled(uintptr_t handle);
static bool sequel_go_cancelled(void *userdata) {
  return sequelGoCancelled((uintptr_t)userdata);
}
static void sequel_go_set_cancel(sequel_analysis_options_t *options, uintptr_t handle) {
  options->cancelled = sequel_go_cancelled;
  options->cancel_userdata = (void *)handle;
}
*/
import "C"

import (
	"context"
	"errors"
	"fmt"
	"runtime"
	"runtime/cgo"
	"unsafe"
)

// AnalysisSchemaVersion identifies the JSON shape of Analysis.
const AnalysisSchemaVersion = 1

// DiagnosticSchemaVersion identifies the canonical diagnostic catalog contract.
const DiagnosticSchemaVersion = 1

// AnalysisOptions controls a single native analysis. Zero input/output/memory
// budgets select the default 1 GiB ceiling; zero work is unlimited. Context is
// polled during native scanning and transformations. Budgets cover native work,
// including intermediate allocations, before results are copied to Go storage.
// MaxQueryTextBytes separately truncates text; zero uses 1 GiB and negative
// disables truncation within the global ceiling. OmitSourceMaps and
// OmitBindDescriptors skip metadata the caller does not need.
type AnalysisOptions struct {
	// Dialect selects lexical rules; empty selects generic SQL. Unknown dialects fail closed.
	Dialect Dialect
	// MaxInputBytes bounds accepted UTF-8 bytes; zero uses the global 1 GiB input ceiling.
	MaxInputBytes uint64
	// MaxOutputBytes bounds generated native strings and numeric arrays; zero uses 1 GiB.
	MaxOutputBytes uint64
	// MaxQueryTextBytes truncates sanitized text on UTF-8 boundaries; zero uses 1 GiB, negative disables truncation.
	MaxQueryTextBytes int
	// MaxMemoryBytes bounds live native working allocations, including intermediates; zero uses 1 GiB.
	MaxMemoryBytes uint64
	// MaxWorkUnits bounds native scan/copy/transform steps; zero permits unlimited work.
	MaxWorkUnits uint64
	// OmitQueryText suppresses Text and Formatted with reason policy; metadata/fingerprint remain available.
	OmitQueryText bool
	// OmitSourceMaps skips both provenance arrays; false generates maps for emitted text.
	OmitSourceMaps bool
	// OmitBindDescriptors skips exported marker descriptors; fingerprint identity is still computed.
	OmitBindDescriptors bool
	// IncludeLints emits opt-in structural, safety, shape and portability diagnostics.
	IncludeLints bool
	// Format requests layout of sanitized text; false leaves Formatted and its map empty.
	Format bool
	// KeepInLists preserves individual placeholders; false collapses IN lists containing only literals/markers.
	KeepInLists bool
	// Context is polled during native work; nil disables cancellation. Cancellation clears partial output.
	Context context.Context
}

// StatementRange locates a statement in the original UTF-8 input. Map this range
// through OriginalToSanitized before applying it to displayed sanitized text.
type StatementRange struct {
	// Offset is the statement's first token position in original UTF-8 bytes.
	Offset int `json:"offset"`
	// EndOffset is the exclusive original byte end, including a terminating semicolon when present.
	EndOffset int `json:"end_offset"`
}

// BindDescriptor contains marker identity and source locations without names,
// types or values. Ordinal and Slot are one-based; StatementIndex is zero-based.
// Repeated named/numbered markers in one statement share Slot.
type BindDescriptor struct {
	// Ordinal is the one-based occurrence number across the whole document.
	Ordinal int `json:"ordinal"`
	// Slot is the one-based driver identity within the statement; numbered markers retain explicit indexes.
	Slot int `json:"slot"`
	// StatementIndex indexes Analysis.Statements from zero, excluding empty semicolon statements.
	StatementIndex int `json:"statement_index"`
	// Offset is the original UTF-8 byte start of the marker.
	Offset int `json:"offset"`
	// EndOffset is the marker's exclusive original UTF-8 byte end.
	EndOffset int `json:"end_offset"`
	// Style is positional, numbered, named or typed; names, type text and values are never retained.
	Style string `json:"style"`
}

// Analysis is a versioned, sanitized result. It never retains original SQL or
// bind values. Status is ok, malformed, limit, cancelled or error. Fingerprint is
// empty on a failed analysis; its presence does not validate database grammar.
// Diagnostics use original UTF-8 locations and catalog prose. Format requests
// layout of sanitized text, so Formatted is also safe for telemetry transport.
type Analysis struct {
	// SchemaVersion identifies the transport contract and currently equals AnalysisSchemaVersion.
	SchemaVersion int `json:"schema_version"`
	// OriginalLength is the supplied UTF-8 byte length, retained even when input exceeds its budget.
	OriginalLength int `json:"original_length"`
	// Dialect is the resolved lexical dialect; the empty option resolves to generic.
	Dialect Dialect `json:"dialect"`
	// Status is ok, malformed, limit, cancelled or error; only ok permits a fingerprint/text.
	Status string `json:"status"`
	// OmissionReason is empty for present Text, otherwise empty, policy, malformed, analysis_limit, cancelled or analysis_error.
	OmissionReason string `json:"omission_reason"`
	// Text is sanitized UTF-8 with literals/markers replaced and comments removed; failures return an empty string.
	Text string `json:"text"`
	// Summary describes sanitized structure; SQL is the fallback for unknown/failed analysis.
	Summary string `json:"summary"`
	// Operation is the shared structural operation name, or empty when unknown or unavailable.
	Operation string `json:"operation"`
	// Formatted contains layout of Text when requested and supported; it never formats original SQL values.
	Formatted string `json:"formatted"`
	// Fingerprint is the versioned SHA-256 shape identifier, empty on failure or empty/comment-only input.
	Fingerprint string `json:"fingerprint"`
	// Truncated reports successful shortening to MaxQueryTextBytes, separate from a total output-budget failure.
	Truncated bool `json:"truncated"`
	// StatementCount is the number of nonempty lexical statements and equals len(Statements).
	StatementCount int `json:"statement_count"`
	// Diagnostics contains original locations and catalog prose; successful analysis has an empty array.
	Diagnostics []Diagnostic `json:"diagnostics"`
	// Binds contains value-free marker identities, empty on failure or OmitBindDescriptors.
	Binds []BindDescriptor `json:"binds"`
	// Statements contains half-open ranges in original UTF-8 bytes, including recovered statements on malformed input.
	Statements []StatementRange `json:"statements"`
	// OriginalToSanitized maps original byte ranges to Text, including deletion/replacement provenance.
	OriginalToSanitized SourceMap `json:"original_to_sanitized"`
	// SanitizedToFormatted maps Text to Formatted; empty when formatting/maps were not requested or unavailable.
	SanitizedToFormatted SourceMap `json:"sanitized_to_formatted"`
	// MemoryPeakBytes reports peak accounted live native allocation bytes, excluding caller-owned input/result storage.
	MemoryPeakBytes uint64 `json:"memory_peak_bytes"`
	// WorkUnits reports native scanning/copying/transform steps consumed; it is not elapsed time.
	WorkUnits uint64 `json:"work_units"`
}

func analysisDialect(dialect Dialect) C.sequel_dialect_t {
	switch dialect {
	case "", DialectGeneric, DialectSQLite, DialectPostgreSQL, DialectDuckDB, DialectMySQL, DialectClickHouse:
		return nativeDialect(dialect)
	default:
		return C.sequel_dialect_t(255)
	}
}

func dialectFromNative(dialect C.sequel_dialect_t) Dialect {
	switch dialect {
	case C.SEQUEL_SQLITE:
		return DialectSQLite
	case C.SEQUEL_POSTGRESQL:
		return DialectPostgreSQL
	case C.SEQUEL_DUCKDB:
		return DialectDuckDB
	case C.SEQUEL_MYSQL:
		return DialectMySQL
	case C.SEQUEL_CLICKHOUSE:
		return DialectClickHouse
	default:
		return DialectGeneric
	}
}

// Analyze produces one result from the shared C engine. Failures return a
// fail-closed result with canonical diagnostics instead of interpolating SQL into
// an error. Context cancellation never publishes partially transformed text.
func Analyze(query string, options AnalysisOptions) Analysis {
	native := C.sequel_analysis_options_t{
		dialect:               analysisDialect(options.Dialect),
		max_input_bytes:       C.size_t(options.MaxInputBytes),
		max_output_bytes:      C.size_t(options.MaxOutputBytes),
		max_memory_bytes:      C.size_t(options.MaxMemoryBytes),
		max_work_units:        C.size_t(options.MaxWorkUnits),
		omit_query_text:       C.bool(options.OmitQueryText),
		omit_source_maps:      C.bool(options.OmitSourceMaps),
		omit_bind_descriptors: C.bool(options.OmitBindDescriptors),
		include_lints:         C.bool(options.IncludeLints),
		format:                C.bool(options.Format),
		keep_in_lists:         C.bool(options.KeepInLists),
	}
	if uint64(native.max_input_bytes) != options.MaxInputBytes ||
		uint64(native.max_output_bytes) != options.MaxOutputBytes ||
		uint64(native.max_memory_bytes) != options.MaxMemoryBytes ||
		uint64(native.max_work_units) != options.MaxWorkUnits {
		native.dialect = C.sequel_dialect_t(255)
	}
	if options.MaxQueryTextBytes > 0 {
		native.max_query_text_bytes = C.size_t(options.MaxQueryTextBytes)
	}
	if options.Context != nil {
		handle := cgo.NewHandle(options.Context)
		defer handle.Delete()
		C.sequel_go_set_cancel(&native, C.uintptr_t(handle))
	}
	// _n reads synchronously and does not retain this pointer. An explicit empty
	// byte avoids treating the valid empty Go string as C NULL input.
	empty := byte(0)
	input := (*C.char)(unsafe.Pointer(unsafe.StringData(query)))
	if len(query) == 0 {
		input = (*C.char)(unsafe.Pointer(&empty))
	}
	// The native failure path points diagnostics at an inline result member, so
	// the result itself must have C ownership too. Keeping it in Go memory would
	// produce a Go pointer nested in a Go allocation on the later free call.
	result := (*C.sequel_analysis_t)(C.calloc(1, C.size_t(C.sizeof_sequel_analysis_t)))
	if result == nil {
		return Analysis{SchemaVersion: AnalysisSchemaVersion, OriginalLength: len(query),
			Dialect: dialectFromNative(native.dialect), Status: "error", OmissionReason: "analysis_error", Summary: "SQL",
			Diagnostics: []Diagnostic{}, Binds: []BindDescriptor{}, Statements: []StatementRange{}}
	}
	defer C.free(unsafe.Pointer(result))
	C.sequel_analyze_n(input, C.size_t(len(query)), &native, result)
	runtime.KeepAlive(query)
	defer C.sequel_analysis_free(result)
	out := Analysis{
		SchemaVersion: int(result.schema_version), OriginalLength: int(result.original_length), Dialect: dialectFromNative(result.dialect),
		Status: C.GoString(result.status), OmissionReason: C.GoString(result.omission_reason),
		Text: C.GoString(result.text), Summary: C.GoString(result.summary), Formatted: C.GoString(result.formatted),
		Operation: C.GoString(&result.operation[0]), Fingerprint: C.GoString(&result.fingerprint[0]),
		Truncated: bool(result.truncated), StatementCount: int(result.statement_count),
		Diagnostics:          make([]Diagnostic, 0, int(result.diagnostic_count)),
		Binds:                make([]BindDescriptor, 0, int(result.bind_count)),
		Statements:           make([]StatementRange, 0, int(result.statement_count)),
		OriginalToSanitized:  sourceMapFromNative(result.original_to_sanitized),
		SanitizedToFormatted: sourceMapFromNative(result.sanitized_to_formatted),
		MemoryPeakBytes:      uint64(result.memory_peak_bytes), WorkUnits: uint64(result.work_units),
	}
	for _, d := range unsafe.Slice(result.diagnostics, int(result.diagnostic_count)) {
		out.Diagnostics = append(out.Diagnostics, diagnosticFromNative(&d))
	}
	for _, b := range unsafe.Slice(result.binds, int(result.bind_count)) {
		out.Binds = append(out.Binds, BindDescriptor{Ordinal: int(b.ordinal), Slot: int(b.slot),
			StatementIndex: int(b.statement_index), Offset: int(b.offset), EndOffset: int(b.end_offset), Style: bindStyleName(b.style)})
	}
	if result.statements != nil {
		for _, s := range unsafe.Slice(result.statements, int(result.statement_count)) {
			out.Statements = append(out.Statements, StatementRange{int(s.offset), int(s.end_offset)})
		}
	}
	return out
}

// DiagnoseAll returns every safe-boundary diagnostic for query in original
// UTF-8 coordinates. It is a convenience wrapper around Analyze for callers
// that do not need sanitized text, fingerprints, maps, or bind descriptors.
// The returned slice is always non-nil; valid SQL returns an empty slice.
func DiagnoseAll(query string, dialect Dialect) []Diagnostic {
	a := Analyze(query, AnalysisOptions{Dialect: dialect, OmitQueryText: true, OmitSourceMaps: true, OmitBindDescriptors: true, IncludeLints: true})
	return append([]Diagnostic{}, a.Diagnostics...)
}

// DiagnosticOptions controls the inspection-only diagnostic API.
type DiagnosticOptions struct {
	Dialect        Dialect
	CheckBinds     bool
	CheckSafety    bool
	CheckStyle     bool
	MaxDiagnostics int
}

// DiagnoseSQL returns bounded diagnostics without executing SQL or emitting telemetry.
func DiagnoseSQL(query string, options DiagnosticOptions) []Diagnostic {
	diagnostics := DiagnoseAll(query, options.Dialect)
	if options.MaxDiagnostics > 0 && len(diagnostics) > options.MaxDiagnostics {
		diagnostics = diagnostics[:options.MaxDiagnostics]
	}
	return diagnostics
}

// Fingerprint returns a versioned SHA-256 query-shape identifier. It excludes
// literals, comments, whitespace and bind spelling while preserving repeated
// marker relationships, token boundaries, quoted identifiers and dialect.
func Fingerprint(dialect Dialect, query string) (string, error) {
	result := Analyze(query, AnalysisOptions{Dialect: dialect, OmitQueryText: true, OmitSourceMaps: true, OmitBindDescriptors: true})
	if result.Status != "ok" {
		if len(result.Diagnostics) != 0 {
			return "", &DiagnosticError{result.Diagnostics[0]}
		}
		return "", fmt.Errorf("SQL analysis failed: %s", result.Status)
	}
	return result.Fingerprint, nil
}

func bindStyleName(style C.sequel_bind_style_t) string {
	switch style {
	case C.SEQUEL_BIND_POSITIONAL:
		return "positional"
	case C.SEQUEL_BIND_NUMBERED:
		return "numbered"
	case C.SEQUEL_BIND_NAMED:
		return "named"
	case C.SEQUEL_BIND_TYPED:
		return "typed"
	default:
		return ""
	}
}

func bindStyleNative(style string) (C.sequel_bind_style_t, error) {
	switch style {
	case "positional":
		return C.SEQUEL_BIND_POSITIONAL, nil
	case "numbered":
		return C.SEQUEL_BIND_NUMBERED, nil
	case "named":
		return C.SEQUEL_BIND_NAMED, nil
	case "typed":
		return C.SEQUEL_BIND_TYPED, nil
	default:
		return 0, errors.New("unknown bind style")
	}
}

// CheckBinds checks supplied arity for one zero-based statement without accepting
// or inspecting bind values. Numbered markers retain their explicit slot index;
// $3 requires three supplied positions even when slots 1 and 2 are unused.
func CheckBinds(binds []BindDescriptor, statementIndex, suppliedSlots int) error {
	if statementIndex < 0 || suppliedSlots < 0 {
		return errors.New("invalid bind count or statement index")
	}
	var pointer *C.sequel_bind_descriptor_t
	if len(binds) > 0 {
		allocation := C.malloc(C.size_t(len(binds)) * C.size_t(C.sizeof_sequel_bind_descriptor_t))
		if allocation == nil {
			return errors.New("cannot allocate bind descriptors")
		}
		defer C.free(allocation)
		pointer = (*C.sequel_bind_descriptor_t)(allocation)
		for i, b := range binds {
			style, err := bindStyleNative(b.Style)
			if err != nil {
				return err
			}
			if b.Ordinal < 1 || b.Slot < 1 || b.StatementIndex < 0 || b.Offset < 0 || b.EndOffset < b.Offset {
				return errors.New("invalid bind descriptor")
			}
			unsafe.Slice(pointer, len(binds))[i] = C.sequel_bind_descriptor_t{
				ordinal: C.size_t(b.Ordinal), slot: C.size_t(b.Slot), statement_index: C.size_t(b.StatementIndex),
				offset: C.size_t(b.Offset), end_offset: C.size_t(b.EndOffset), style: style,
			}
		}
	}
	if C.sequel_check_binds(pointer, C.size_t(len(binds)), C.size_t(statementIndex), C.size_t(suppliedSlots)) != C.SEQUEL_OK {
		return errors.New("bind descriptors or supplied slot count are invalid")
	}
	return nil
}
