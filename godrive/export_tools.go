package main

/*
#include <stdlib.h>
*/
import "C"

import "unsafe"

// export_tools.go is intentionally a thin cgo shim: every //export function
// here only converts *C.char <-> string and delegates to the pure-Go
// implementation in export_tools_impl.go. Go's toolchain does not allow
// `import "C"` inside _test.go files at all, so keeping the actual logic
// cgo-free is what lets export_tools_impl_test.go exercise it directly.

func cString(s string) *C.char { return C.CString(s) }

//export gda_free_string
func gda_free_string(s *C.char) {
	C.free(unsafe.Pointer(s))
}

//export gda_login
func gda_login(argsJson *C.char) *C.char {
	return cString(loginImpl(C.GoString(argsJson)))
}

//export gda_logout
func gda_logout() *C.char {
	return cString(logoutImpl())
}

//export gda_search_files
func gda_search_files(argsJson *C.char) *C.char {
	return cString(searchFilesImpl(C.GoString(argsJson)))
}

//export gda_list_recent_files
func gda_list_recent_files(argsJson *C.char) *C.char {
	return cString(listRecentFilesImpl(C.GoString(argsJson)))
}

//export gda_get_file_metadata
func gda_get_file_metadata(argsJson *C.char) *C.char {
	return cString(getFileMetadataImpl(C.GoString(argsJson)))
}

//export gda_get_file_permissions
func gda_get_file_permissions(argsJson *C.char) *C.char {
	return cString(getFilePermissionsImpl(C.GoString(argsJson)))
}

//export gda_read_file_content
func gda_read_file_content(argsJson *C.char) *C.char {
	return cString(readFileContentImpl(C.GoString(argsJson)))
}

//export gda_download_file_content
func gda_download_file_content(argsJson *C.char) *C.char {
	return cString(downloadFileContentImpl(C.GoString(argsJson)))
}

//export gda_create_file
func gda_create_file(argsJson *C.char) *C.char {
	return cString(createFileImpl(C.GoString(argsJson)))
}

//export gda_update_file
func gda_update_file(argsJson *C.char) *C.char {
	return cString(updateFileImpl(C.GoString(argsJson)))
}

//export gda_copy_file
func gda_copy_file(argsJson *C.char) *C.char {
	return cString(copyFileImpl(C.GoString(argsJson)))
}

//export gda_share_file
func gda_share_file(argsJson *C.char) *C.char {
	return cString(shareFileImpl(C.GoString(argsJson)))
}

//export gda_trash_file
func gda_trash_file(argsJson *C.char) *C.char {
	return cString(trashFileImpl(C.GoString(argsJson)))
}
