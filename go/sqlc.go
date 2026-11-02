package sequel

import (
	"regexp"
	"strings"
)

// sqlcNamePattern accepts an ASCII letter followed by ASCII letters, digits or underscores.
var sqlcNamePattern = regexp.MustCompile(`^[A-Za-z][A-Za-z0-9_]*$`)

// SQLCName extracts a sqlc -- name: QueryName directive.
// It returns the first valid name or an empty string when none is found. Names
// start with an ASCII letter and contain only ASCII letters, digits or underscores.
// Directive matching is case-sensitive; surrounding line whitespace is ignored.
func SQLCName(statement string) string {
	for _, line := range strings.Split(statement, "\n") {
		line = strings.TrimSpace(line)
		if !strings.HasPrefix(line, "-- name:") {
			continue
		}
		fields := strings.Fields(strings.TrimSpace(strings.TrimPrefix(line, "-- name:")))
		if len(fields) > 0 && sqlcNamePattern.MatchString(fields[0]) {
			return fields[0]
		}
	}
	return ""
}
