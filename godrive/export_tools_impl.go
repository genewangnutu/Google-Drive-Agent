package main

import (
	"context"
	"encoding/json"
	"fmt"

	"google.golang.org/api/drive/v3"
)

// This file holds the pure-Go body of every //export function in
// export_tools.go (the cgo shim). Keeping it free of `import "C"` is what
// lets export_tools_impl_test.go call these functions directly — Go's
// toolchain does not allow cgo inside _test.go files at all.

// safeEnvelope runs fn and converts any panic into a failure envelope, so a
// panic can never escape across the FFI boundary (FFI-Contract.md §1).
//
// Per the Go spec, recover() only has an effect when called directly by a
// deferred function — calling it from a function that a deferred function
// calls does NOT stop the panic. So the recover() below must live directly
// inside this defer's closure; it cannot be factored into a separate named
// helper that the closure merely calls.
func safeEnvelope(fn func() string) string {
	result := ""
	func() {
		defer func() {
			if r := recover(); r != nil {
				result = failureEnvelope(newAppError("INTERNAL", 0, fmt.Sprintf("panic: %v", r), false))
			}
		}()
		result = fn()
	}()
	return result
}

// --- login / logout -----------------------------------------------------

type loginArgs struct {
	Mode string `json:"mode"`
}

func loginImpl(argsJson string) string {
	return safeEnvelope(func() string {
		var args loginArgs
		if err := json.Unmarshal([]byte(argsJson), &args); err != nil {
			return errorEnvelope(newAppError("INVALID_ARGUMENT", 0, fmt.Sprintf("invalid argsJson: %v", err), false))
		}
		if err := Login(args.Mode); err != nil {
			return errorEnvelope(err)
		}
		return successEnvelope(map[string]string{"status": "logged_in"})
	})
}

func logoutImpl() string {
	return safeEnvelope(func() string {
		if err := Logout(); err != nil {
			return errorEnvelope(err)
		}
		return successEnvelope(map[string]string{"status": "logged_out"})
	})
}

// --- shared helper ---------------------------------------------------------

// withDriveService parses argsJson into T, obtains an authenticated Drive
// client, and runs fn, converting any error into the failure envelope and
// any success value into the success envelope.
func withDriveService[T any](argsJson string, fn func(ctx context.Context, srv *drive.Service, args T) (any, error)) string {
	var args T
	if err := json.Unmarshal([]byte(argsJson), &args); err != nil {
		return errorEnvelope(newAppError("INVALID_ARGUMENT", 0, fmt.Sprintf("invalid argsJson: %v", err), false))
	}

	ctx := context.Background()
	srv, err := getDriveService(ctx)
	if err != nil {
		return errorEnvelope(err)
	}

	data, err := fn(ctx, srv, args)
	if err != nil {
		return errorEnvelope(err)
	}
	return successEnvelope(data)
}

// --- search_files / list_recent_files -------------------------------------

type searchFilesArgs struct {
	Query     string `json:"query"`
	PageSize  int64  `json:"pageSize"`
	PageToken string `json:"pageToken"`
}

func searchFilesImpl(argsJson string) string {
	return safeEnvelope(func() string {
		return withDriveService(argsJson, func(_ context.Context, srv *drive.Service, args searchFilesArgs) (any, error) {
			return SearchFiles(srv, args.Query, args.PageSize, args.PageToken)
		})
	})
}

type listRecentFilesArgs struct {
	OrderBy   string `json:"orderBy"`
	PageSize  int64  `json:"pageSize"`
	PageToken string `json:"pageToken"`
}

func listRecentFilesImpl(argsJson string) string {
	return safeEnvelope(func() string {
		return withDriveService(argsJson, func(_ context.Context, srv *drive.Service, args listRecentFilesArgs) (any, error) {
			return ListRecentFiles(srv, args.OrderBy, args.PageSize, args.PageToken)
		})
	})
}

// --- get_file_metadata / get_file_permissions -----------------------------

type fileIDArgs struct {
	FileID string `json:"fileId"`
}

func getFileMetadataImpl(argsJson string) string {
	return safeEnvelope(func() string {
		return withDriveService(argsJson, func(_ context.Context, srv *drive.Service, args fileIDArgs) (any, error) {
			return GetFileMetadata(srv, args.FileID)
		})
	})
}

func getFilePermissionsImpl(argsJson string) string {
	return safeEnvelope(func() string {
		return withDriveService(argsJson, func(_ context.Context, srv *drive.Service, args fileIDArgs) (any, error) {
			return GetFilePermissions(srv, args.FileID)
		})
	})
}

// --- read_file_content / download_file_content ----------------------------

type readFileContentArgs struct {
	FileID          string `json:"fileId"`
	IncludeComments bool   `json:"includeComments"`
}

func readFileContentImpl(argsJson string) string {
	return safeEnvelope(func() string {
		return withDriveService(argsJson, func(_ context.Context, srv *drive.Service, args readFileContentArgs) (any, error) {
			return ReadFileContent(srv, args.FileID, args.IncludeComments)
		})
	})
}

type downloadFileContentArgs struct {
	FileID         string `json:"fileId"`
	ExportMimeType string `json:"exportMimeType"`
	RevisionID     string `json:"revisionId"`
}

func downloadFileContentImpl(argsJson string) string {
	return safeEnvelope(func() string {
		return withDriveService(argsJson, func(_ context.Context, srv *drive.Service, args downloadFileContentArgs) (any, error) {
			return DownloadFileContent(srv, args.FileID, args.ExportMimeType, args.RevisionID)
		})
	})
}

// --- create_file / update_file / copy_file ---------------------------------

type createFileArgs struct {
	Title                         string `json:"title"`
	ParentID                      string `json:"parentId"`
	TextContent                   string `json:"textContent"`
	Base64Content                 string `json:"base64Content"`
	ContentMimeType               string `json:"contentMimeType"`
	DisableConversionToGoogleType bool   `json:"disableConversionToGoogleType"`
}

func createFileImpl(argsJson string) string {
	return safeEnvelope(func() string {
		return withDriveService(argsJson, func(_ context.Context, srv *drive.Service, args createFileArgs) (any, error) {
			return CreateFile(srv, CreateFileArgs{
				Title:                         args.Title,
				ParentID:                      args.ParentID,
				TextContent:                   args.TextContent,
				Base64Content:                 args.Base64Content,
				ContentMimeType:               args.ContentMimeType,
				DisableConversionToGoogleType: args.DisableConversionToGoogleType,
			})
		})
	})
}

type updateFileArgs struct {
	FileID      string `json:"fileId"`
	Title       string `json:"title"`
	ParentID    string `json:"parentId"`
	Description string `json:"description"`
}

func updateFileImpl(argsJson string) string {
	return safeEnvelope(func() string {
		return withDriveService(argsJson, func(_ context.Context, srv *drive.Service, args updateFileArgs) (any, error) {
			return UpdateFile(srv, UpdateFileArgs{
				FileID:      args.FileID,
				Title:       args.Title,
				ParentID:    args.ParentID,
				Description: args.Description,
			})
		})
	})
}

type copyFileArgs struct {
	FileID   string `json:"fileId"`
	Title    string `json:"title"`
	ParentID string `json:"parentId"`
}

func copyFileImpl(argsJson string) string {
	return safeEnvelope(func() string {
		return withDriveService(argsJson, func(_ context.Context, srv *drive.Service, args copyFileArgs) (any, error) {
			return CopyFile(srv, CopyFileArgs{
				FileID:   args.FileID,
				Title:    args.Title,
				ParentID: args.ParentID,
			})
		})
	})
}

// --- share_file / trash_file -----------------------------------------------

type shareFileArgs struct {
	FileID       string `json:"fileId"`
	Role         string `json:"role"`
	EmailAddress string `json:"emailAddress"`
	ChatSpaceID  string `json:"chatSpaceId"`
}

func shareFileImpl(argsJson string) string {
	return safeEnvelope(func() string {
		return withDriveService(argsJson, func(_ context.Context, srv *drive.Service, args shareFileArgs) (any, error) {
			return ShareFile(srv, ShareFileArgs{
				FileID:       args.FileID,
				Role:         args.Role,
				EmailAddress: args.EmailAddress,
				ChatSpaceID:  args.ChatSpaceID,
			})
		})
	})
}

func trashFileImpl(argsJson string) string {
	return safeEnvelope(func() string {
		return withDriveService(argsJson, func(_ context.Context, srv *drive.Service, args fileIDArgs) (any, error) {
			return TrashFile(srv, args.FileID)
		})
	})
}
