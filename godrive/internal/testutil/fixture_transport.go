// Package testutil provides a fake http.RoundTripper so drive_client.go can
// be unit tested against canned Drive API v3 JSON responses, with no real
// network call and no real Google account (Plan §8.4).
package testutil

import (
	"bytes"
	"context"
	"io"
	"net/http"
	"os"
	"testing"

	"google.golang.org/api/drive/v3"
	"google.golang.org/api/option"
)

// FixtureTransport is a fake http.RoundTripper that always answers with the
// contents of Fixture, regardless of the request it receives. It also
// records every request it saw, so tests can assert on call count (e.g.
// retry behavior) or request parameters (e.g. addParents/removeParents).
type FixtureTransport struct {
	T        *testing.T
	Fixture  string // e.g. "testdata/drive_api_responses/files_list_basic.json"
	Status   int    // 0 means 200
	Requests []*http.Request
}

func (f *FixtureTransport) RoundTrip(req *http.Request) (*http.Response, error) {
	f.Requests = append(f.Requests, req)

	body, err := os.ReadFile(f.Fixture)
	if err != nil {
		f.T.Fatalf("missing fixture %s: %v", f.Fixture, err)
	}
	status := f.Status
	if status == 0 {
		status = 200
	}
	return &http.Response{
		StatusCode: status,
		Status:     http.StatusText(status),
		Body:       io.NopCloser(bytes.NewReader(body)),
		Header:     http.Header{"Content-Type": []string{"application/json"}},
		Request:    req,
	}, nil
}

// NewTestDriveService builds a *drive.Service whose HTTP transport is a
// FixtureTransport reading from fixture, so callers never touch the real
// Drive API.
func NewTestDriveService(t *testing.T, fixture string) *drive.Service {
	t.Helper()
	client := &http.Client{Transport: &FixtureTransport{T: t, Fixture: fixture}}
	srv, err := drive.NewService(context.Background(), option.WithHTTPClient(client), option.WithoutAuthentication())
	if err != nil {
		t.Fatal(err)
	}
	return srv
}

// NewTestDriveServiceWithStatus is like NewTestDriveService but lets the
// caller set a non-200 status (e.g. 429/403) to drive error-path tests.
func NewTestDriveServiceWithStatus(t *testing.T, fixture string, status int) *drive.Service {
	t.Helper()
	srv, _ := NewTestDriveServiceWithTransport(t, fixture, status)
	return srv
}

// NewTestDriveServiceWithTransport is like NewTestDriveService but also
// returns the FixtureTransport, so a test can inspect recorded requests
// (retry counts, query parameters) after the call under test.
func NewTestDriveServiceWithTransport(t *testing.T, fixture string, status int) (*drive.Service, *FixtureTransport) {
	t.Helper()
	transport := &FixtureTransport{T: t, Fixture: fixture, Status: status}
	client := &http.Client{Transport: transport}
	srv, err := drive.NewService(context.Background(), option.WithHTTPClient(client), option.WithoutAuthentication())
	if err != nil {
		t.Fatal(err)
	}
	return srv, transport
}
