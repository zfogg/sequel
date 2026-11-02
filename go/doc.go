// Package sequel formats, sanitizes, inspects and highlights SQL through a shared
// C engine. No query is executed and bind values are never read by inspection.
//
// FormatSQL and HighlightSQL preserve SQL contents, including potentially
// sensitive literals and comments. Use SanitizeWithOptions or SanitizeSelective
// when those contents must be removed. SQL is UTF-8 and operations are bounded
// by input, token and nesting limits.
//
// Diagnose provides source locations, explanations and hints. Formatting and
// highlighting SQL errors expose a DiagnosticError through errors.As. For editor
// previews, HighlightOptions.Tolerant colors unfinished lexical constructs
// without closing them; use Diagnose to inspect their warnings.
package sequel
