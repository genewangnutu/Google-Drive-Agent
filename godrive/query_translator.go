package main

import (
	"fmt"
	"regexp"
	"strings"
)

// clauseSplitRe splits the DSL on top-level "and"/"or" combinators once
// quoted string literals have been masked out (see maskQuotedStrings).
var clauseSplitRe = regexp.MustCompile(`(?i)\s+(and|or)\s+`)

// clausePattern matches a single "<field> <operator> <value>" clause.
var clausePattern = regexp.MustCompile(`^(\w+)\s*(contains|!=|<=|>=|=|<|>)\s*(.+)$`)

// quotedRe matches a single-quoted string literal, e.g. 'Q3 Report'.
var quotedRe = regexp.MustCompile(`'[^']*'`)

// passthroughFields are MCP query DSL fields whose name and comparison
// semantics are identical in the Drive API v3 `q` syntax (Plan §6.3).
var passthroughFields = map[string]bool{
	"name":           true, // already-translated `title` lands here too
	"fullText":       true,
	"mimeType":       true,
	"modifiedTime":   true,
	"createdTime":    true,
	"viewedByMeTime": true,
	"sharedWithMe":   true,
	"starred":        true,
	"trashed":        true,
}

// TranslateQuery converts the MCP query DSL (Plan §6.3) into a Drive API v3
// `q` parameter string. It is a pure function with no network/API
// dependency, which is what lets it be table-driven tested in isolation.
func TranslateQuery(mcpQuery string) (string, error) {
	trimmed := strings.TrimSpace(mcpQuery)
	if trimmed == "" {
		return "", fmt.Errorf("query must not be empty")
	}

	masked, quotes := maskQuotedStrings(trimmed)

	clauses := clauseSplitRe.Split(masked, -1)
	combinators := clauseSplitRe.FindAllStringSubmatch(masked, -1)

	var sb strings.Builder
	for i, maskedClause := range clauses {
		clause := unmaskQuotedStrings(strings.TrimSpace(maskedClause), quotes)
		translated, err := translateClause(clause)
		if err != nil {
			return "", err
		}
		sb.WriteString(translated)
		if i < len(combinators) {
			sb.WriteString(" ")
			sb.WriteString(strings.ToLower(combinators[i][1]))
			sb.WriteString(" ")
		}
	}
	return sb.String(), nil
}

func translateClause(clause string) (string, error) {
	m := clausePattern.FindStringSubmatch(clause)
	if m == nil {
		return "", fmt.Errorf("unrecognized query clause: %q", clause)
	}
	field, op, rawValue := m[1], m[2], strings.TrimSpace(m[3])

	switch field {
	case "title":
		return fmt.Sprintf("name %s %s", op, rawValue), nil
	case "parentId":
		if op != "=" {
			return "", fmt.Errorf("parentId only supports '=' in the query DSL, got %q", op)
		}
		return fmt.Sprintf("%s in parents", rawValue), nil
	case "owner":
		if op != "=" {
			return "", fmt.Errorf("owner only supports '=' in the query DSL, got %q", op)
		}
		return fmt.Sprintf("%s in owners", rawValue), nil
	default:
		if passthroughFields[field] {
			return fmt.Sprintf("%s %s %s", field, op, rawValue), nil
		}
		return "", fmt.Errorf("unsupported query field: %q", field)
	}
}

func maskQuotedStrings(s string) (string, []string) {
	var quotes []string
	masked := quotedRe.ReplaceAllStringFunc(s, func(m string) string {
		quotes = append(quotes, m)
		return fmt.Sprintf("\x00%d\x00", len(quotes)-1)
	})
	return masked, quotes
}

func unmaskQuotedStrings(s string, quotes []string) string {
	for i, q := range quotes {
		placeholder := fmt.Sprintf("\x00%d\x00", i)
		s = strings.ReplaceAll(s, placeholder, q)
	}
	return s
}
