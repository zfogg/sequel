package sequel

/*
#include <stdlib.h>
#include <sequel.h>
*/
import "C"

import (
	"errors"
	"runtime"
	"unsafe"
)

// MapSegment records numeric provenance between UTF-8 byte ranges. Kind is
// copy, replace, delete or insert. It contains no source or replacement bytes.
type MapSegment struct {
	// FromStart is the zero-based source UTF-8 byte start.
	FromStart int `json:"from_start"`
	// FromEnd is the exclusive source byte end; it equals FromStart for inserted layout.
	FromEnd int `json:"from_end"`
	// ToStart is the zero-based destination UTF-8 byte start.
	ToStart int `json:"to_start"`
	// ToEnd is the exclusive destination byte end; it equals ToStart for deleted input.
	ToEnd int `json:"to_end"`
	// Kind is copy (equal lengths), replace (both nonempty), delete or insert.
	Kind string `json:"kind"`
}

// SourceMap describes a monotone complete mapping between two UTF-8 documents.
// It includes deleted comments and replaced literals rather than pretending their
// original byte offsets remain valid in the transformed output.
type SourceMap struct {
	// FromLength is the complete source document size in UTF-8 bytes.
	FromLength int `json:"from_length"`
	// ToLength is the complete destination document size in UTF-8 bytes.
	ToLength int `json:"to_length"`
	// Segments monotonically cover both documents without gaps; zero lengths/empty segments mean an absent map.
	Segments []MapSegment `json:"segments"`
}

// MappedRange is a half-open mapped byte interval. Exact is false when mapping
// crosses replacement, deletion or inserted layout; deleted input maps to an
// empty anchor and replacement interiors map to the whole replacement.
type MappedRange struct {
	// Start is the mapped zero-based UTF-8 byte start, possibly an empty deletion anchor.
	Start int `json:"start"`
	// End is the exclusive mapped UTF-8 byte end; replacement interiors cover the whole replacement.
	End int `json:"end"`
	// Exact identifies copied bytes or exact segment endpoints; transformed interiors are inexact.
	Exact bool `json:"exact"`
}

func mapKindName(kind C.sequel_map_kind_t) string {
	switch kind {
	case C.SEQUEL_MAP_COPY:
		return "copy"
	case C.SEQUEL_MAP_REPLACE:
		return "replace"
	case C.SEQUEL_MAP_DELETE:
		return "delete"
	case C.SEQUEL_MAP_INSERT:
		return "insert"
	default:
		return ""
	}
}

func mapKindNative(kind string) (C.sequel_map_kind_t, error) {
	switch kind {
	case "copy":
		return C.SEQUEL_MAP_COPY, nil
	case "replace":
		return C.SEQUEL_MAP_REPLACE, nil
	case "delete":
		return C.SEQUEL_MAP_DELETE, nil
	case "insert":
		return C.SEQUEL_MAP_INSERT, nil
	default:
		return 0, errors.New("unknown source map kind")
	}
}

func sourceMapFromNative(source C.sequel_source_map_t) SourceMap {
	out := SourceMap{FromLength: int(source.from_length), ToLength: int(source.to_length), Segments: make([]MapSegment, 0, int(source.count))}
	for _, s := range unsafe.Slice(source.segments, int(source.count)) {
		out.Segments = append(out.Segments, MapSegment{int(s.from_start), int(s.from_end), int(s.to_start), int(s.to_end), mapKindName(s.kind)})
	}
	return out
}

// MapRange maps a UTF-8 byte range forward, or backward when reverse is true.
// It validates map structure and bounds through the shared native implementation.
// Invalid/incomplete maps and out-of-bounds ranges return an error and zero result.
func (m SourceMap) MapRange(start, end int, reverse bool) (MappedRange, error) {
	if m.FromLength < 0 || m.ToLength < 0 || start < 0 || end < start {
		return MappedRange{}, errors.New("invalid source map range")
	}
	var pointer *C.sequel_map_segment_t
	if len(m.Segments) > 0 {
		allocation := C.malloc(C.size_t(len(m.Segments)) * C.size_t(C.sizeof_sequel_map_segment_t))
		if allocation == nil {
			return MappedRange{}, errors.New("cannot allocate source map")
		}
		defer C.free(allocation)
		pointer = (*C.sequel_map_segment_t)(allocation)
		for i, s := range m.Segments {
			kind, err := mapKindNative(s.Kind)
			if err != nil {
				return MappedRange{}, err
			}
			if s.FromStart < 0 || s.FromEnd < s.FromStart || s.ToStart < 0 || s.ToEnd < s.ToStart {
				return MappedRange{}, errors.New("invalid source map segment")
			}
			unsafe.Slice(pointer, len(m.Segments))[i] = C.sequel_map_segment_t{
				from_start: C.size_t(s.FromStart), from_end: C.size_t(s.FromEnd), to_start: C.size_t(s.ToStart), to_end: C.size_t(s.ToEnd), kind: kind,
			}
		}
	}
	native := C.sequel_source_map_t{from_length: C.size_t(m.FromLength), to_length: C.size_t(m.ToLength), segments: pointer, count: C.size_t(len(m.Segments))}
	var result C.sequel_mapped_range_t
	if C.sequel_source_map_range(&native, C.size_t(start), C.size_t(end), C.bool(reverse), &result) != C.SEQUEL_OK {
		return MappedRange{}, errors.New("invalid source map or range")
	}
	return MappedRange{int(result.start), int(result.end), bool(result.exact)}, nil
}

// UTF8ToUTF16 converts a global UTF-8 byte offset to UTF-16 code units. It rejects
// invalid UTF-8 and offsets inside a scalar. CR and LF each occupy one unit.
func UTF8ToUTF16(text string, byteOffset int) (int, error) {
	if byteOffset < 0 {
		return 0, errors.New("invalid UTF-8 offset")
	}
	empty := byte(0)
	pointer := (*C.char)(unsafe.Pointer(unsafe.StringData(text)))
	if len(text) == 0 {
		pointer = (*C.char)(unsafe.Pointer(&empty))
	}
	var result C.size_t
	status := C.sequel_utf8_to_utf16(pointer, C.size_t(len(text)), C.size_t(byteOffset), &result)
	runtime.KeepAlive(text)
	if status != C.SEQUEL_OK {
		return 0, errors.New("invalid UTF-8 text or scalar boundary")
	}
	return int(result), nil
}

// UTF16ToUTF8 converts global UTF-16 units to a UTF-8 byte offset. It rejects
// offsets inside surrogate pairs rather than rounding an underline boundary.
func UTF16ToUTF8(text string, units int) (int, error) {
	if units < 0 {
		return 0, errors.New("invalid UTF-16 offset")
	}
	empty := byte(0)
	pointer := (*C.char)(unsafe.Pointer(unsafe.StringData(text)))
	if len(text) == 0 {
		pointer = (*C.char)(unsafe.Pointer(&empty))
	}
	var result C.size_t
	status := C.sequel_utf16_to_utf8(pointer, C.size_t(len(text)), C.size_t(units), &result)
	runtime.KeepAlive(text)
	if status != C.SEQUEL_OK {
		return 0, errors.New("invalid UTF-8 text or UTF-16 boundary")
	}
	return int(result), nil
}
