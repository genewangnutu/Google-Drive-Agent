package main

import (
	"encoding/base64"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"google-drive-agent/godrive/internal/testutil"
)

const fxListBasic = "testdata/drive_api_responses/files_list_basic.json"
const fxGetMetadata = "testdata/drive_api_responses/files_get_metadata.json"
const fxPermissions = "testdata/drive_api_responses/permissions_list.json"
const fxCreateResponse = "testdata/drive_api_responses/files_create_response.json"
const fxRateLimit = "testdata/drive_api_responses/error_403_rate_limit.json"

func TestSearchFiles_Basic(t *testing.T) {
	srv := testutil.NewTestDriveService(t, fxListBasic)

	got, err := SearchFiles(srv, "title contains 'report'", 20, "")
	require.NoError(t, err)
	require.Len(t, got.Files, 2)
	assert.Equal(t, "1abcQ3ReportId", got.Files[0].ID)
	assert.Equal(t, "Q3 Report.pdf", got.Files[0].Name)
	require.NotNil(t, got.NextPageToken)
	assert.Equal(t, "page2token", *got.NextPageToken)
}

func TestSearchFiles_InvalidQueryIsRejectedBeforeAnyRequest(t *testing.T) {
	srv, transport := testutil.NewTestDriveServiceWithTransport(t, fxListBasic, 0)

	_, err := SearchFiles(srv, "bogusField = 'x'", 20, "")
	require.Error(t, err)
	assert.Equal(t, "INVALID_ARGUMENT", mapError(err).Code)
	assert.Empty(t, transport.Requests, "an unparseable query must not reach the Drive API")
}

func TestListRecentFiles_OrdersAndExcludesTrashed(t *testing.T) {
	srv, transport := testutil.NewTestDriveServiceWithTransport(t, fxListBasic, 0)

	got, err := ListRecentFiles(srv, "lastModified", 10, "")
	require.NoError(t, err)
	require.Len(t, got.Files, 2)

	require.Len(t, transport.Requests, 1)
	q := transport.Requests[0].URL.Query()
	assert.Contains(t, q.Get("q"), "trashed=false")
	assert.Equal(t, "modifiedTime desc", q.Get("orderBy"))
}

func TestGetFileMetadata_Basic(t *testing.T) {
	srv := testutil.NewTestDriveService(t, fxGetMetadata)

	got, err := GetFileMetadata(srv, "1abcQ3ReportId")
	require.NoError(t, err)
	assert.Equal(t, "1abcQ3ReportId", got.ID)
	assert.Equal(t, "application/pdf", got.MimeType)
	require.NotNil(t, got.Size)
	assert.Equal(t, "204800", *got.Size)
	require.NotNil(t, got.WebViewLink)
}

func TestGetFilePermissions_Basic(t *testing.T) {
	srv := testutil.NewTestDriveService(t, fxPermissions)

	got, err := GetFilePermissions(srv, "1abcQ3ReportId")
	require.NoError(t, err)
	require.Len(t, got.Permissions, 3)
	assert.Equal(t, "owner", got.Permissions[0].Role)
	require.NotNil(t, got.Permissions[0].EmailAddress)
	assert.Equal(t, "owner@example.com", *got.Permissions[0].EmailAddress)
	assert.Nil(t, got.Permissions[2].EmailAddress, "anyone-type permissions have no emailAddress")
}

func TestCreateFile_WithTextContent(t *testing.T) {
	srv := testutil.NewTestDriveService(t, fxCreateResponse)

	got, err := CreateFile(srv, CreateFileArgs{
		Title:       "notes.txt",
		ParentID:    "0AFolderRoot",
		TextContent: "hello world!!",
	})
	require.NoError(t, err)
	assert.Equal(t, "4jklNewFileId", got.ID)
	assert.Equal(t, "notes.txt", got.Name)
}

func TestCreateFile_EmptyTitleIsRejected(t *testing.T) {
	srv, transport := testutil.NewTestDriveServiceWithTransport(t, fxCreateResponse, 0)

	_, err := CreateFile(srv, CreateFileArgs{})
	require.Error(t, err)
	assert.Equal(t, "INVALID_ARGUMENT", mapError(err).Code)
	assert.Empty(t, transport.Requests)
}

func TestUpdateFile_MovesParents(t *testing.T) {
	// files_get_metadata.json's parents=["0AFolderRoot"] serves both the
	// "fetch current parents" call and the Update call's response.
	srv, transport := testutil.NewTestDriveServiceWithTransport(t, fxGetMetadata, 0)

	got, err := UpdateFile(srv, UpdateFileArgs{
		FileID:   "1abcQ3ReportId",
		ParentID: "0BNewFolder",
	})
	require.NoError(t, err)
	assert.Equal(t, "1abcQ3ReportId", got.ID)

	require.Len(t, transport.Requests, 2)
	updateReq := transport.Requests[1]
	assert.Equal(t, "0BNewFolder", updateReq.URL.Query().Get("addParents"))
	assert.Equal(t, "0AFolderRoot", updateReq.URL.Query().Get("removeParents"))
}

func TestCopyFile_Basic(t *testing.T) {
	srv := testutil.NewTestDriveService(t, fxCreateResponse)

	got, err := CopyFile(srv, CopyFileArgs{FileID: "1abcQ3ReportId", Title: "notes-copy.txt"})
	require.NoError(t, err)
	assert.Equal(t, "4jklNewFileId", got.ID)
}

func TestTrashFile_Basic(t *testing.T) {
	srv := testutil.NewTestDriveService(t, fxGetMetadata)

	got, err := TrashFile(srv, "1abcQ3ReportId")
	require.NoError(t, err)
	assert.Equal(t, "1abcQ3ReportId", got.ID)
}

func TestShareFile_ExistingHigherRoleIsKeptNotDowngraded(t *testing.T) {
	// permissions_list.json already has alice@example.com as "writer";
	// requesting "reader" (lower) must not issue a second (Update) request.
	srv, transport := testutil.NewTestDriveServiceWithTransport(t, fxPermissions, 0)

	got, err := ShareFile(srv, ShareFileArgs{
		FileID:       "1abcQ3ReportId",
		Role:         "reader",
		EmailAddress: "alice@example.com",
	})
	require.NoError(t, err)
	assert.Equal(t, "writer", got.Role, "existing higher role must be kept, not downgraded")
	assert.Len(t, transport.Requests, 1, "no Create/Update call should happen when not upgrading")
}

func TestShareFile_ChatSpaceIdIsUnsupported(t *testing.T) {
	srv, transport := testutil.NewTestDriveServiceWithTransport(t, fxPermissions, 0)

	_, err := ShareFile(srv, ShareFileArgs{FileID: "1abcQ3ReportId", Role: "reader", ChatSpaceID: "spaces/AAA"})
	require.Error(t, err)
	assert.Equal(t, "INVALID_ARGUMENT", mapError(err).Code)
	assert.Empty(t, transport.Requests)
}

func TestReadFileContent_PlainText(t *testing.T) {
	// files_get_metadata.json's mimeType is application/pdf which is out of
	// scope for v1 text extraction (Plan §10 Phase 4+); assert the correct
	// INVALID_ARGUMENT failure rather than attempting extraction.
	srv := testutil.NewTestDriveService(t, fxGetMetadata)

	_, err := ReadFileContent(srv, "1abcQ3ReportId", false)
	require.Error(t, err)
	assert.Equal(t, "INVALID_ARGUMENT", mapError(err).Code)
}

func TestDownloadFileContent_Basic(t *testing.T) {
	srv := testutil.NewTestDriveService(t, fxGetMetadata)

	got, err := DownloadFileContent(srv, "1abcQ3ReportId", "", "")
	require.NoError(t, err)
	assert.Equal(t, "Q3 Report.pdf", got.Filename)
	assert.Equal(t, "application/pdf", got.MimeType)

	decoded, err := base64.StdEncoding.DecodeString(got.Base64Content)
	require.NoError(t, err)
	assert.Contains(t, string(decoded), "1abcQ3ReportId")
}

// --- error path (Plan §8.4 / module brief) --------------------------------

func TestGetFileMetadata_RateLimitedErrorIsRetriedThenMapped(t *testing.T) {
	srv, transport := testutil.NewTestDriveServiceWithTransport(t, fxRateLimit, 403)

	_, err := GetFileMetadata(srv, "1abcQ3ReportId")
	require.Error(t, err)

	appErr := mapError(err)
	assert.Equal(t, "RATE_LIMITED", appErr.Code)
	assert.Equal(t, 403, appErr.HTTPStatus)
	assert.True(t, appErr.Retryable)

	assert.Equal(t, maxRetries+1, len(transport.Requests), "a retryable error should be retried up to maxRetries times")
}
