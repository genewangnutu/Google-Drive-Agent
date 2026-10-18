#include "gda/ffi/godrive_bridge.hpp"

// cgo generates this filename verbatim (see Doc/FFI-Contract.md §2); the
// build wires ${GODRIVE_INCLUDE_DIR} onto this translation unit's include
// path so it resolves to the real generated header.
#include "libgodrive.h"

namespace gda {

namespace {

// Every gda_* call below returns a Go-allocated char* that must be copied
// into a std::string and released via gda_free_string() -- never C++
// free()/delete (Doc/FFI-Contract.md §1). This class passes the envelope
// JSON through unmodified; it does not parse "ok"/"error" (ToolDispatcher's
// job).
std::string CopyAndFree(char* raw) {
  std::string result(raw);
  gda_free_string(raw);
  return result;
}

// cgo's -buildmode=c-archive has no way to emit a `const` qualifier on a Go
// *C.char parameter, so the generated libgodrive.h declares every input as
// plain `char*` (confirmed against the real generated header, not just
// FFI-Contract.md's C ABI doc, which writes `const char*` for readability).
// The Go side only ever reads this pointer (immediately copies it into a Go
// string), never writes through it, so stripping const from a std::string's
// own buffer here is safe in practice.
char* AsMutable(const std::string& s) { return const_cast<char*>(s.c_str()); }

}  // namespace

std::string GodriveBridge::Login(const std::string& argsJson) {
  return CopyAndFree(gda_login(AsMutable(argsJson)));
}

std::string GodriveBridge::Logout(const std::string& argsJson) {
  // gda_logout takes no argument; argsJson is contractually always "{}"
  // (Doc/FFI-Contract.md §3.2) and is ignored here per spec.
  (void)argsJson;
  return CopyAndFree(gda_logout());
}

std::string GodriveBridge::SearchFiles(const std::string& argsJson) {
  return CopyAndFree(gda_search_files(AsMutable(argsJson)));
}

std::string GodriveBridge::ListRecentFiles(const std::string& argsJson) {
  return CopyAndFree(gda_list_recent_files(AsMutable(argsJson)));
}

std::string GodriveBridge::GetFileMetadata(const std::string& argsJson) {
  return CopyAndFree(gda_get_file_metadata(AsMutable(argsJson)));
}

std::string GodriveBridge::GetFilePermissions(const std::string& argsJson) {
  return CopyAndFree(gda_get_file_permissions(AsMutable(argsJson)));
}

std::string GodriveBridge::ReadFileContent(const std::string& argsJson) {
  return CopyAndFree(gda_read_file_content(AsMutable(argsJson)));
}

std::string GodriveBridge::DownloadFileContent(const std::string& argsJson) {
  return CopyAndFree(gda_download_file_content(AsMutable(argsJson)));
}

std::string GodriveBridge::CreateFile(const std::string& argsJson) {
  return CopyAndFree(gda_create_file(AsMutable(argsJson)));
}

std::string GodriveBridge::UpdateFile(const std::string& argsJson) {
  return CopyAndFree(gda_update_file(AsMutable(argsJson)));
}

std::string GodriveBridge::CopyFile(const std::string& argsJson) {
  return CopyAndFree(gda_copy_file(AsMutable(argsJson)));
}

std::string GodriveBridge::ShareFile(const std::string& argsJson) {
  return CopyAndFree(gda_share_file(AsMutable(argsJson)));
}

std::string GodriveBridge::TrashFile(const std::string& argsJson) {
  return CopyAndFree(gda_trash_file(AsMutable(argsJson)));
}

}  // namespace gda
