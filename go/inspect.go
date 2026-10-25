package sequel

// Inspect returns a summary, operation, sanitized template and statement count.
// On unsafe input, ok is false and no query text is returned.
func Inspect(query string, options SanitizeOptions) (summary, operation string, text SanitizedQuery, statementCount int, ok bool) {
	return inspectNativeWithOptions(query, options)
}

// Operation returns the shared SQL operation, or an empty string on failure.
func Operation(dialect Dialect, query string) string { return statementOperation(dialect, query) }
