package main

import (
	"encoding/json"
	"os"
	"testing"
)

type queryCase struct {
	Name      string `json:"name"`
	MCPQuery  string `json:"mcp_query"`
	ExpectedQ string `json:"expected_q"`
}

// TestTranslateQuery_TableDriven drives TranslateQuery from
// testdata/query_translation_cases.json (Plan §8.3): adding a case only
// requires adding a line to that file, no code change.
func TestTranslateQuery_TableDriven(t *testing.T) {
	data, err := os.ReadFile("testdata/query_translation_cases.json")
	if err != nil {
		t.Fatal(err)
	}
	var cases []queryCase
	if err := json.Unmarshal(data, &cases); err != nil {
		t.Fatal(err)
	}
	if len(cases) == 0 {
		t.Fatal("no query translation cases loaded")
	}
	for _, c := range cases {
		t.Run(c.Name, func(t *testing.T) {
			got, err := TranslateQuery(c.MCPQuery)
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if got != c.ExpectedQ {
				t.Errorf("TranslateQuery(%q) = %q, want %q", c.MCPQuery, got, c.ExpectedQ)
			}
		})
	}
}

func TestTranslateQuery_EmptyQueryIsError(t *testing.T) {
	if _, err := TranslateQuery("   "); err == nil {
		t.Fatal("expected an error for an empty query")
	}
}

func TestTranslateQuery_UnknownFieldIsError(t *testing.T) {
	if _, err := TranslateQuery("bogusField = 'x'"); err == nil {
		t.Fatal("expected an error for an unsupported field")
	}
}

func TestTranslateQuery_ParentIdRejectsNonEqualityOperator(t *testing.T) {
	if _, err := TranslateQuery("parentId contains '123'"); err == nil {
		t.Fatal("expected an error: parentId only supports '='")
	}
}
