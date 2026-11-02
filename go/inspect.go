package sequel

// Inspect returns a summary, operation, sanitized template and statement count.
// On unsafe input, ok is false and no query text is returned.
// OmitQueryText suppresses text while retaining the other metadata. Summary and
// operation describe SQL structure without executing a query or reading binds.
// Statement count includes the statements found by the shared scanner.
func Inspect(query string, options SanitizeOptions) (summary, operation string, text SanitizedQuery, statementCount int, ok bool) {
	return inspectNativeWithOptions(query, options)
}

// Operation returns the shared SQL operation, or an empty string on failure.
// It uses the same inspection path as Summary, including CTE handling, without
// collecting query text or executing SQL.
func Operation(dialect Dialect, query string) string { return statementOperation(dialect, query) }
