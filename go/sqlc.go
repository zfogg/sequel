package sequel

import (
	"regexp"
	"strings"
)

var sqlcNamePattern = regexp.MustCompile(`^[A-Za-z][A-Za-z0-9_]*$`)

// SQLCName extracts a sqlc -- name: QueryName directive.
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
