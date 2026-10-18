package main

import (
	"bytes"
	"encoding/base64"
	"fmt"
	"io"
	"net/http"
	"strconv"
	"strings"
	"time"

	"google.golang.org/api/drive/v3"
	"google.golang.org/api/googleapi"
)

// fileFields/filesListFields request exactly the fields FFI-Contract.md §3.9
// needs for a <FileObject>, per Plan §6.5 "field minimization".
const fileFields = "id,name,mimeType,parents,createdTime,modifiedTime,trashed,webViewLink,size"
const filesListFields = "nextPageToken,files(" + fileFields + ")"
const permissionFields = "permissions(id,type,role,emailAddress)"

const maxRetries = 3

// doWithRetry wraps a single Drive API call with exponential backoff for
// 429/5xx (and 403-with-rate-limit-reason) errors, per Plan §6.5.
func doWithRetry(fn func() error) error {
	backoff := 20 * time.Millisecond
	var err error
	for attempt := 0; attempt <= maxRetries; attempt++ {
		err = fn()
		if err == nil {
			return nil
		}
		appErr := mapError(err)
		if !appErr.Retryable || attempt == maxRetries {
			return err
		}
		time.Sleep(backoff)
		backoff *= 2
	}
	return err
}

// --- data shapes (FFI-Contract.md §3) ---------------------------------

type FileObject struct {
	ID           string   `json:"id"`
	Name         string   `json:"name"`
	MimeType     string   `json:"mimeType"`
	Parents      []string `json:"parents"`
	CreatedTime  string   `json:"createdTime"`
	ModifiedTime string   `json:"modifiedTime"`
	Trashed      bool     `json:"trashed"`
	WebViewLink  *string  `json:"webViewLink"`
	Size         *string  `json:"size"`
}

type FilesPage struct {
	Files         []FileObject `json:"files"`
	NextPageToken *string      `json:"nextPageToken"`
}

type PermissionObject struct {
	ID           string  `json:"id"`
	Type         string  `json:"type"`
	Role         string  `json:"role"`
	EmailAddress *string `json:"emailAddress"`
}

type PermissionsPage struct {
	Permissions []PermissionObject `json:"permissions"`
}

type CommentObject struct {
	ID          string `json:"id"`
	Author      string `json:"author"`
	Content     string `json:"content"`
	CreatedTime string `json:"createdTime"`
}

type ReadFileContentResult struct {
	Content  string          `json:"content"`
	MimeType string          `json:"mimeType"`
	Comments []CommentObject `json:"comments"`
}

type DownloadFileContentResult struct {
	Base64Content string `json:"base64Content"`
	MimeType      string `json:"mimeType"`
	Filename      string `json:"filename"`
}

type CreateFileArgs struct {
	Title                         string
	ParentID                      string
	TextContent                   string
	Base64Content                 string
	ContentMimeType               string
	DisableConversionToGoogleType bool
}

type UpdateFileArgs struct {
	FileID      string
	Title       string
	ParentID    string
	Description string
}

type CopyFileArgs struct {
	FileID   string
	Title    string
	ParentID string
}

type ShareFileArgs struct {
	FileID       string
	Role         string
	EmailAddress string
	ChatSpaceID  string
}

type ShareResult struct {
	ID           string  `json:"id"`
	Type         string  `json:"type"`
	Role         string  `json:"role"`
	EmailAddress *string `json:"emailAddress"`
}

type TrashResult struct {
	ID      string `json:"id"`
	Trashed bool   `json:"trashed"`
}

// --- conversions between drive.* SDK types and the FFI shapes ---------

func fileToObject(f *drive.File) FileObject {
	obj := FileObject{
		ID:           f.Id,
		Name:         f.Name,
		MimeType:     f.MimeType,
		Parents:      f.Parents,
		CreatedTime:  f.CreatedTime,
		ModifiedTime: f.ModifiedTime,
		Trashed:      f.Trashed,
	}
	if f.WebViewLink != "" {
		link := f.WebViewLink
		obj.WebViewLink = &link
	}
	if f.Size > 0 {
		size := strconv.FormatInt(f.Size, 10)
		obj.Size = &size
	}
	return obj
}

func filesListToPage(result *drive.FileList) FilesPage {
	files := make([]FileObject, 0, len(result.Files))
	for _, f := range result.Files {
		files = append(files, fileToObject(f))
	}
	page := FilesPage{Files: files}
	if result.NextPageToken != "" {
		token := result.NextPageToken
		page.NextPageToken = &token
	}
	return page
}

func getFile(srv *drive.Service, fileID, fields string) (*drive.File, error) {
	var result *drive.File
	err := doWithRetry(func() error {
		var e error
		result, e = srv.Files.Get(fileID).Fields(googleapi.Field(fields)).Do()
		return e
	})
	return result, err
}

// --- search_files / list_recent_files ----------------------------------

func listFiles(srv *drive.Service, q string, pageSize int64, pageToken string) (FilesPage, error) {
	call := srv.Files.List().Fields(googleapi.Field(filesListFields))
	if q != "" {
		call = call.Q(q)
	}
	if pageSize > 0 {
		call = call.PageSize(pageSize)
	}
	if pageToken != "" {
		call = call.PageToken(pageToken)
	}
	var result *drive.FileList
	err := doWithRetry(func() error {
		var e error
		result, e = call.Do()
		return e
	})
	if err != nil {
		return FilesPage{}, err
	}
	return filesListToPage(result), nil
}

// SearchFiles implements `search_files` (FFI-Contract.md §3.3): translate
// the MCP query DSL, then run it through Files.List.
func SearchFiles(srv *drive.Service, query string, pageSize int64, pageToken string) (FilesPage, error) {
	q, err := TranslateQuery(query)
	if err != nil {
		return FilesPage{}, newAppError("INVALID_ARGUMENT", 0, err.Error(), false)
	}
	return listFiles(srv, q, pageSize, pageToken)
}

func translateOrderBy(orderBy string) (string, error) {
	switch orderBy {
	case "", "recency":
		return "viewedByMeTime desc", nil
	case "lastModified":
		return "modifiedTime desc", nil
	case "lastModifiedByMe":
		return "modifiedByMeTime desc", nil
	default:
		return "", fmt.Errorf("unsupported orderBy: %q", orderBy)
	}
}

// ListRecentFiles implements `list_recent_files` (FFI-Contract.md §3.4).
// Always ANDs trashed=false per Plan §6.3 row 2.
func ListRecentFiles(srv *drive.Service, orderBy string, pageSize int64, pageToken string) (FilesPage, error) {
	sortKey, err := translateOrderBy(orderBy)
	if err != nil {
		return FilesPage{}, newAppError("INVALID_ARGUMENT", 0, err.Error(), false)
	}

	call := srv.Files.List().Q("trashed=false").OrderBy(sortKey).Fields(googleapi.Field(filesListFields))
	if pageSize > 0 {
		call = call.PageSize(pageSize)
	}
	if pageToken != "" {
		call = call.PageToken(pageToken)
	}
	var result *drive.FileList
	err = doWithRetry(func() error {
		var e error
		result, e = call.Do()
		return e
	})
	if err != nil {
		return FilesPage{}, err
	}
	return filesListToPage(result), nil
}

// GetFileMetadata implements `get_file_metadata` (FFI-Contract.md §3.5).
func GetFileMetadata(srv *drive.Service, fileID string) (FileObject, error) {
	if fileID == "" {
		return FileObject{}, newAppError("INVALID_ARGUMENT", 0, "fileId is required", false)
	}
	f, err := getFile(srv, fileID, fileFields)
	if err != nil {
		return FileObject{}, err
	}
	return fileToObject(f), nil
}

// GetFilePermissions implements `get_file_permissions` (FFI-Contract.md §3.6).
func GetFilePermissions(srv *drive.Service, fileID string) (PermissionsPage, error) {
	if fileID == "" {
		return PermissionsPage{}, newAppError("INVALID_ARGUMENT", 0, "fileId is required", false)
	}
	var result *drive.PermissionList
	err := doWithRetry(func() error {
		var e error
		result, e = srv.Permissions.List(fileID).Fields(googleapi.Field(permissionFields)).Do()
		return e
	})
	if err != nil {
		return PermissionsPage{}, err
	}
	perms := make([]PermissionObject, 0, len(result.Permissions))
	for _, p := range result.Permissions {
		po := PermissionObject{ID: p.Id, Type: p.Type, Role: p.Role}
		if p.EmailAddress != "" {
			email := p.EmailAddress
			po.EmailAddress = &email
		}
		perms = append(perms, po)
	}
	return PermissionsPage{Permissions: perms}, nil
}

// --- read_file_content / download_file_content -------------------------

// nativeExportMimeTypes picks a default text export for Google-native
// types. PDF/Office/image "text-ify" is explicitly out of scope for v1
// (Plan §10 Phase 4+); unsupported mimeTypes get INVALID_ARGUMENT.
var nativeExportMimeTypes = map[string]string{
	"application/vnd.google-apps.document":     "text/plain",
	"application/vnd.google-apps.spreadsheet":  "text/csv",
	"application/vnd.google-apps.presentation": "text/plain",
}

func isPlainTextMimeType(mimeType string) bool {
	return strings.HasPrefix(mimeType, "text/") || mimeType == "application/json" || mimeType == "application/xml"
}

// ReadFileContent implements `read_file_content` (FFI-Contract.md §3.7).
func ReadFileContent(srv *drive.Service, fileID string, includeComments bool) (ReadFileContentResult, error) {
	if fileID == "" {
		return ReadFileContentResult{}, newAppError("INVALID_ARGUMENT", 0, "fileId is required", false)
	}

	meta, err := getFile(srv, fileID, "id,mimeType,name")
	if err != nil {
		return ReadFileContentResult{}, err
	}

	var body []byte
	mimeType := meta.MimeType

	switch {
	case strings.HasPrefix(meta.MimeType, "application/vnd.google-apps."):
		exportMime, ok := nativeExportMimeTypes[meta.MimeType]
		if !ok {
			return ReadFileContentResult{}, newAppError("INVALID_ARGUMENT", 0,
				fmt.Sprintf("unsupported mimeType for text extraction yet: %s", meta.MimeType), false)
		}
		mimeType = exportMime
		var resp *http.Response
		err = doWithRetry(func() error {
			var e error
			resp, e = srv.Files.Export(fileID, exportMime).Download()
			return e
		})
		if err != nil {
			return ReadFileContentResult{}, err
		}
		defer resp.Body.Close()
		body, err = io.ReadAll(resp.Body)
	case isPlainTextMimeType(meta.MimeType):
		var resp *http.Response
		err = doWithRetry(func() error {
			var e error
			resp, e = srv.Files.Get(fileID).Download()
			return e
		})
		if err != nil {
			return ReadFileContentResult{}, err
		}
		defer resp.Body.Close()
		body, err = io.ReadAll(resp.Body)
	default:
		return ReadFileContentResult{}, newAppError("INVALID_ARGUMENT", 0,
			fmt.Sprintf("unsupported mimeType for text extraction yet: %s", meta.MimeType), false)
	}
	if err != nil {
		return ReadFileContentResult{}, newAppError("INTERNAL", 0, fmt.Sprintf("failed reading content: %v", err), false)
	}

	result := ReadFileContentResult{Content: string(body), MimeType: mimeType}

	if includeComments {
		var commentList *drive.CommentList
		err = doWithRetry(func() error {
			var e error
			commentList, e = srv.Comments.List(fileID).Fields("comments(id,author,content,createdTime)").Do()
			return e
		})
		if err != nil {
			return ReadFileContentResult{}, err
		}
		comments := make([]CommentObject, 0, len(commentList.Comments))
		for _, c := range commentList.Comments {
			author := ""
			if c.Author != nil {
				author = c.Author.DisplayName
			}
			comments = append(comments, CommentObject{ID: c.Id, Author: author, Content: c.Content, CreatedTime: c.CreatedTime})
		}
		result.Comments = comments
	}

	return result, nil
}

// DownloadFileContent implements `download_file_content` (FFI-Contract.md §3.8).
func DownloadFileContent(srv *drive.Service, fileID, exportMimeType, revisionID string) (DownloadFileContentResult, error) {
	if fileID == "" {
		return DownloadFileContentResult{}, newAppError("INVALID_ARGUMENT", 0, "fileId is required", false)
	}

	meta, err := getFile(srv, fileID, "id,name,mimeType")
	if err != nil {
		return DownloadFileContentResult{}, err
	}

	var resp *http.Response
	mimeType := meta.MimeType

	switch {
	case revisionID != "":
		err = doWithRetry(func() error {
			var e error
			resp, e = srv.Revisions.Get(fileID, revisionID).Download()
			return e
		})
	case exportMimeType != "":
		mimeType = exportMimeType
		err = doWithRetry(func() error {
			var e error
			resp, e = srv.Files.Export(fileID, exportMimeType).Download()
			return e
		})
	default:
		err = doWithRetry(func() error {
			var e error
			resp, e = srv.Files.Get(fileID).Download()
			return e
		})
	}
	if err != nil {
		return DownloadFileContentResult{}, err
	}
	defer resp.Body.Close()
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return DownloadFileContentResult{}, newAppError("INTERNAL", 0, fmt.Sprintf("failed reading download body: %v", err), false)
	}

	return DownloadFileContentResult{
		Base64Content: base64.StdEncoding.EncodeToString(body),
		MimeType:      mimeType,
		Filename:      meta.Name,
	}, nil
}

// --- create_file / update_file / copy_file ------------------------------

// convertibleToGoogleType names the source content types Drive can convert
// to a Google-native type on upload (Plan §6.3 row 7 / §10 Phase 2).
var convertibleToGoogleType = map[string]string{
	"text/plain": "application/vnd.google-apps.document",
	"text/csv":   "application/vnd.google-apps.spreadsheet",
}

// CreateFile implements `create_file` (FFI-Contract.md §3.9). When no
// content is supplied, contentMimeType is used directly as the target
// mimeType (e.g. "application/vnd.google-apps.folder" for a folder, or a
// Google-native type for an empty Doc/Sheet/Slide) since the contract has
// no separate "type" field for the no-content case.
func CreateFile(srv *drive.Service, args CreateFileArgs) (FileObject, error) {
	if args.Title == "" {
		return FileObject{}, newAppError("INVALID_ARGUMENT", 0, "title is required", false)
	}

	file := &drive.File{Name: args.Title}
	if args.ParentID != "" {
		file.Parents = []string{args.ParentID}
	}

	var media io.Reader
	switch {
	case args.Base64Content != "":
		raw, decodeErr := base64.StdEncoding.DecodeString(args.Base64Content)
		if decodeErr != nil {
			return FileObject{}, newAppError("INVALID_ARGUMENT", 0, fmt.Sprintf("base64Content is not valid base64: %v", decodeErr), false)
		}
		media = bytes.NewReader(raw)
	case args.TextContent != "":
		media = strings.NewReader(args.TextContent)
	}

	if media != nil {
		if target, ok := convertibleToGoogleType[args.ContentMimeType]; ok && !args.DisableConversionToGoogleType {
			file.MimeType = target
		}
	} else if args.ContentMimeType != "" {
		file.MimeType = args.ContentMimeType
	}

	call := srv.Files.Create(file).Fields(googleapi.Field(fileFields))
	if media != nil {
		call = call.Media(media)
	}

	var created *drive.File
	err := doWithRetry(func() error {
		var e error
		created, e = call.Do()
		return e
	})
	if err != nil {
		return FileObject{}, err
	}
	return fileToObject(created), nil
}

// UpdateFile implements `update_file` (FFI-Contract.md §3.10): fetch the
// current parents first so the move can be expressed as AddParents/
// RemoveParents, per Plan §6.3 row 8.
func UpdateFile(srv *drive.Service, args UpdateFileArgs) (FileObject, error) {
	if args.FileID == "" {
		return FileObject{}, newAppError("INVALID_ARGUMENT", 0, "fileId is required", false)
	}

	update := &drive.File{}
	if args.Title != "" {
		update.Name = args.Title
	}
	if args.Description != "" {
		update.Description = args.Description
	}

	call := srv.Files.Update(args.FileID, update).Fields(googleapi.Field(fileFields))

	if args.ParentID != "" {
		current, err := getFile(srv, args.FileID, "parents")
		if err != nil {
			return FileObject{}, err
		}
		if len(current.Parents) > 0 {
			call = call.RemoveParents(strings.Join(current.Parents, ","))
		}
		call = call.AddParents(args.ParentID)
	}

	var updated *drive.File
	err := doWithRetry(func() error {
		var e error
		updated, e = call.Do()
		return e
	})
	if err != nil {
		return FileObject{}, err
	}
	return fileToObject(updated), nil
}

// CopyFile implements `copy_file` (FFI-Contract.md §3.11).
func CopyFile(srv *drive.Service, args CopyFileArgs) (FileObject, error) {
	if args.FileID == "" {
		return FileObject{}, newAppError("INVALID_ARGUMENT", 0, "fileId is required", false)
	}

	copyMeta := &drive.File{}
	if args.Title != "" {
		copyMeta.Name = args.Title
	}
	if args.ParentID != "" {
		copyMeta.Parents = []string{args.ParentID}
	}

	var copied *drive.File
	err := doWithRetry(func() error {
		var e error
		copied, e = srv.Files.Copy(args.FileID, copyMeta).Fields(googleapi.Field(fileFields)).Do()
		return e
	})
	if err != nil {
		return FileObject{}, err
	}
	return fileToObject(copied), nil
}

// --- share_file / trash_file ---------------------------------------------

var roleRank = map[string]int{
	"reader":    1,
	"commenter": 2,
	"writer":    3,
	"owner":     4,
}

// ShareFile implements `share_file` (FFI-Contract.md §3.12). The
// chatSpaceId branch is explicitly unresolved (Plan §12 item 3): v1
// returns INVALID_ARGUMENT for it rather than guessing at an unconfirmed
// API shape.
func ShareFile(srv *drive.Service, args ShareFileArgs) (ShareResult, error) {
	if args.ChatSpaceID != "" {
		return ShareResult{}, newAppError("INVALID_ARGUMENT", 0, "sharing to a chatSpaceId is not supported in this version", false)
	}
	if args.FileID == "" || args.EmailAddress == "" || args.Role == "" {
		return ShareResult{}, newAppError("INVALID_ARGUMENT", 0, "fileId, role and emailAddress are required", false)
	}
	if _, ok := roleRank[args.Role]; !ok {
		return ShareResult{}, newAppError("INVALID_ARGUMENT", 0, fmt.Sprintf("unsupported role %q", args.Role), false)
	}

	var existing *drive.PermissionList
	err := doWithRetry(func() error {
		var e error
		existing, e = srv.Permissions.List(args.FileID).Fields(googleapi.Field(permissionFields)).Do()
		return e
	})
	if err != nil {
		return ShareResult{}, err
	}

	var current *drive.Permission
	for _, p := range existing.Permissions {
		if strings.EqualFold(p.EmailAddress, args.EmailAddress) {
			current = p
			break
		}
	}

	var result *drive.Permission
	switch {
	case current == nil:
		perm := &drive.Permission{Type: "user", Role: args.Role, EmailAddress: args.EmailAddress}
		err = doWithRetry(func() error {
			var e error
			result, e = srv.Permissions.Create(args.FileID, perm).Fields("id,type,role,emailAddress").Do()
			return e
		})
	case roleRank[args.Role] > roleRank[current.Role]:
		perm := &drive.Permission{Role: args.Role}
		err = doWithRetry(func() error {
			var e error
			result, e = srv.Permissions.Update(args.FileID, current.Id, perm).Fields("id,type,role,emailAddress").Do()
			return e
		})
	default:
		result = current
	}
	if err != nil {
		return ShareResult{}, err
	}

	var email *string
	if result.EmailAddress != "" {
		email = &result.EmailAddress
	}
	return ShareResult{ID: result.Id, Type: result.Type, Role: result.Role, EmailAddress: email}, nil
}

// TrashFile implements `trash_file` (FFI-Contract.md §3.13).
func TrashFile(srv *drive.Service, fileID string) (TrashResult, error) {
	if fileID == "" {
		return TrashResult{}, newAppError("INVALID_ARGUMENT", 0, "fileId is required", false)
	}
	var updated *drive.File
	err := doWithRetry(func() error {
		var e error
		updated, e = srv.Files.Update(fileID, &drive.File{Trashed: true}).Fields("id,trashed").Do()
		return e
	})
	if err != nil {
		return TrashResult{}, err
	}
	return TrashResult{ID: updated.Id, Trashed: updated.Trashed}, nil
}
