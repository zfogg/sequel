package sequel

import (
	"bytes"
	"encoding/hex"
	"encoding/json"
	"errors"
	"io"
	"reflect"
	"strconv"
	"strings"
	"unicode/utf8"
)

// CanonicalizeAnalysis checks the versioned transport shape and replaces every
// diagnostic's prose with the public catalog text. It does not authenticate the
// sender or certify that arbitrary received Text/Summary fields are sanitized.
func CanonicalizeAnalysis(a Analysis) (Analysis, error) {
	fail := func() (Analysis, error) { return Analysis{}, errors.New("invalid versioned SQL analysis") }
	if a.SchemaVersion != AnalysisSchemaVersion || a.OriginalLength < 0 || a.StatementCount < 0 {
		return fail()
	}
	for _, text := range []string{a.Text, a.Summary, a.Operation, a.Formatted, a.Fingerprint} {
		if !utf8.ValidString(text) {
			return fail()
		}
	}
	switch a.Dialect {
	case DialectGeneric, DialectSQLite, DialectPostgreSQL, DialectDuckDB, DialectMySQL, DialectClickHouse:
	default:
		return fail()
	}
	switch a.Status {
	case "ok", "malformed", "limit", "cancelled", "error":
	default:
		return fail()
	}
	if a.Text != "" {
		if a.OmissionReason != "" {
			return fail()
		}
	} else {
		switch a.OmissionReason {
		case "empty", "policy", "malformed", "analysis_limit", "analysis_error", "cancelled":
		default:
			return fail()
		}
	}
	if a.Status != "ok" && (a.Text != "" || a.Formatted != "" || a.Fingerprint != "" || len(a.Binds) != 0 ||
		len(a.OriginalToSanitized.Segments) != 0 || len(a.SanitizedToFormatted.Segments) != 0) {
		return fail()
	}
	if a.Status != "ok" {
		expected := map[string]string{"malformed": "malformed", "limit": "analysis_limit", "cancelled": "cancelled", "error": "analysis_error"}
		if a.OmissionReason != expected[a.Status] || a.Truncated {
			return fail()
		}
	} else if a.Text == "" && a.OmissionReason != "empty" && a.OmissionReason != "policy" {
		return fail()
	}
	if a.Status == "ok" && len(a.Diagnostics) != 0 {
		return fail()
	}
	if a.Fingerprint != "" {
		prefix := "sequel:v1:" + string(a.Dialect) + ":"
		if !strings.HasPrefix(a.Fingerprint, prefix) {
			return fail()
		}
		digest := strings.TrimPrefix(a.Fingerprint, prefix)
		if len(digest) != 64 || digest != strings.ToLower(digest) {
			return fail()
		}
		if _, err := hex.DecodeString(digest); err != nil {
			return fail()
		}
	}
	diagnostics := make([]Diagnostic, 0, len(a.Diagnostics))
	for _, d := range a.Diagnostics {
		canonical, err := CanonicalizeDiagnostic(DiagnosticSchemaVersion, a.OriginalLength, d)
		if err != nil {
			return fail()
		}
		diagnostics = append(diagnostics, canonical)
	}
	a.Diagnostics = diagnostics
	// The wire schema uses arrays, including empty arrays, rather than null.
	if a.Binds == nil {
		a.Binds = []BindDescriptor{}
	}
	if a.Statements == nil {
		a.Statements = []StatementRange{}
	}
	if a.OriginalToSanitized.Segments == nil {
		a.OriginalToSanitized.Segments = []MapSegment{}
	}
	if a.SanitizedToFormatted.Segments == nil {
		a.SanitizedToFormatted.Segments = []MapSegment{}
	}
	if len(a.Statements) != a.StatementCount {
		return fail()
	}
	end := 0
	for _, s := range a.Statements {
		if s.Offset < end || s.EndOffset < s.Offset || s.EndOffset > a.OriginalLength {
			return fail()
		}
		end = s.EndOffset
	}
	if (len(a.OriginalToSanitized.Segments) > 0 && a.OriginalToSanitized.ToLength != len(a.Text)) ||
		(len(a.SanitizedToFormatted.Segments) > 0 && a.SanitizedToFormatted.ToLength != len(a.Formatted)) {
		return fail()
	}
	if len(a.OriginalToSanitized.Segments) > 0 && a.OriginalToSanitized.FromLength != a.OriginalLength {
		return fail()
	}
	if len(a.SanitizedToFormatted.Segments) > 0 && a.SanitizedToFormatted.FromLength != len(a.Text) {
		return fail()
	}
	if _, err := a.OriginalToSanitized.MapRange(0, 0, false); err != nil {
		return fail()
	}
	if _, err := a.SanitizedToFormatted.MapRange(0, 0, false); err != nil {
		return fail()
	}
	bindEnd := 0
	previousStatement := 0
	for i, b := range a.Binds {
		if b.Ordinal != i+1 || b.Slot < 1 || b.StatementIndex < previousStatement || b.StatementIndex >= a.StatementCount ||
			b.Offset < 0 || b.EndOffset <= b.Offset || b.EndOffset > a.OriginalLength {
			return fail()
		}
		statement := a.Statements[b.StatementIndex]
		if b.Offset < bindEnd || b.Offset < statement.Offset || b.EndOffset > statement.EndOffset {
			return fail()
		}
		if _, err := bindStyleNative(b.Style); err != nil {
			return fail()
		}
		bindEnd = b.EndOffset
		previousStatement = b.StatementIndex
	}
	return a, nil
}

// MarshalAnalysis encodes a validated schema-versioned result, canonicalizing
// diagnostic prose before serialization. The input is not mutated.
func MarshalAnalysis(a Analysis) ([]byte, error) {
	canonical, err := CanonicalizeAnalysis(a)
	if err != nil {
		return nil, err
	}
	return json.Marshal(canonical)
}

// UnmarshalAnalysis decodes one strict versioned JSON result. Unknown fields,
// versions/codes and invalid ranges are rejected. Incoming diagnostic prose is
// discarded and replaced with catalog text. Errors never quote the JSON payload.
func UnmarshalAnalysis(data []byte) (Analysis, error) {
	if !validJSONUnicode(data) || !validAnalysisJSONShape(data) {
		return Analysis{}, errors.New("invalid SQL analysis JSON")
	}
	decoder := json.NewDecoder(bytes.NewReader(data))
	decoder.DisallowUnknownFields()
	var a Analysis
	if decoder.Decode(&a) != nil {
		return Analysis{}, errors.New("invalid SQL analysis JSON")
	}
	var extra any
	if decoder.Decode(&extra) != io.EOF {
		return Analysis{}, errors.New("invalid SQL analysis JSON")
	}
	return CanonicalizeAnalysis(a)
}

// A streaming shape check rejects null, missing fields and duplicate keys before
// encoding/json can coerce them to zero values or silently overwrite a field.
// It keeps validation independent of any input diagnostic prose.
func validAnalysisJSONShape(data []byte) bool {
	decoder := json.NewDecoder(bytes.NewReader(data))
	decoder.UseNumber()
	if !validJSONShapeValue(decoder, reflect.TypeOf(Analysis{})) {
		return false
	}
	_, err := decoder.Token()
	return err == io.EOF
}

func validJSONShapeValue(decoder *json.Decoder, expected reflect.Type) bool {
	token, err := decoder.Token()
	if err != nil {
		return false
	}
	switch expected.Kind() {
	case reflect.Struct:
		if token != json.Delim('{') {
			return false
		}
		fields := make(map[string]reflect.Type, expected.NumField())
		for i := 0; i < expected.NumField(); i++ {
			field := expected.Field(i)
			fields[field.Tag.Get("json")] = field.Type
		}
		seen := make(map[string]bool, len(fields))
		for decoder.More() {
			keyToken, err := decoder.Token()
			if err != nil {
				return false
			}
			key, ok := keyToken.(string)
			field, known := fields[key]
			if !ok || !known || seen[key] {
				return false
			}
			seen[key] = true
			if !validJSONShapeValue(decoder, field) {
				return false
			}
		}
		close, err := decoder.Token()
		return err == nil && close == json.Delim('}') && len(seen) == len(fields)
	case reflect.Slice:
		if token != json.Delim('[') {
			return false
		}
		for decoder.More() {
			if !validJSONShapeValue(decoder, expected.Elem()) {
				return false
			}
		}
		close, err := decoder.Token()
		return err == nil && close == json.Delim(']')
	case reflect.String:
		_, ok := token.(string)
		return ok
	case reflect.Bool:
		_, ok := token.(bool)
		return ok
	case reflect.Int, reflect.Int8, reflect.Int16, reflect.Int32, reflect.Int64:
		number, ok := token.(json.Number)
		if !ok {
			return false
		}
		_, err := strconv.ParseInt(string(number), 10, expected.Bits())
		return err == nil
	case reflect.Uint, reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64:
		number, ok := token.(json.Number)
		if !ok {
			return false
		}
		_, err := strconv.ParseUint(string(number), 10, expected.Bits())
		return err == nil
	default:
		return false
	}
}

// encoding/json replaces malformed Unicode with U+FFFD. Reject those inputs
// before decoding, since silently changing text would invalidate byte maps.
func validJSONUnicode(data []byte) bool {
	if !utf8.Valid(data) {
		return false
	}
	hex4 := func(offset int) (uint16, bool) {
		if offset+4 > len(data) {
			return 0, false
		}
		var value uint16
		for _, c := range data[offset : offset+4] {
			value <<= 4
			switch {
			case c >= '0' && c <= '9':
				value |= uint16(c - '0')
			case c >= 'a' && c <= 'f':
				value |= uint16(c - 'a' + 10)
			case c >= 'A' && c <= 'F':
				value |= uint16(c - 'A' + 10)
			default:
				return 0, false
			}
		}
		return value, true
	}
	inString := false
	for i := 0; i < len(data); i++ {
		if data[i] == '"' {
			inString = !inString
			continue
		}
		if !inString || data[i] != '\\' {
			continue
		}
		i++
		if i >= len(data) {
			return false
		}
		if data[i] != 'u' {
			continue
		}
		value, valid := hex4(i + 1)
		if !valid {
			return false
		}
		i += 4
		if value >= 0xdc00 && value <= 0xdfff {
			return false
		}
		if value < 0xd800 || value > 0xdbff {
			continue
		}
		if i+2 >= len(data) || data[i+1] != '\\' || data[i+2] != 'u' {
			return false
		}
		low, valid := hex4(i + 3)
		if !valid || low < 0xdc00 || low > 0xdfff {
			return false
		}
		i += 6
	}
	return true
}
