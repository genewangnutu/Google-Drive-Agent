#include "fake_godrive_abi.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

namespace {

std::map<std::string, std::string>& LastArgsRegistry() {
  static std::map<std::string, std::string> registry;
  return registry;
}

// Mirrors the real contract's ownership rule: the returned char* is
// "Go-allocated" (here: malloc'd via strdup) and must be released with
// gda_free_string(), never C++ delete.
char* MakeCanned(const char* fnName, const char* argsJson, const char* cannedJson) {
  LastArgsRegistry()[fnName] = argsJson;
  return strdup(cannedJson);
}

}  // namespace

extern "C" {

char* gda_login(const char* argsJson) {
  return MakeCanned("login", argsJson, R"({"ok":true,"data":{"status":"logged_in"}})");
}

char* gda_logout(void) {
  // Per Doc/FFI-Contract.md §3.2, gda_logout takes no argument; record "{}"
  // so fake_godrive_last_args("logout") is still observable in tests.
  LastArgsRegistry()["logout"] = "{}";
  return strdup(R"({"ok":true,"data":{"status":"logged_out"}})");
}

char* gda_search_files(const char* argsJson) {
  return MakeCanned("search_files", argsJson,
                     R"({"ok":true,"data":{"files":[{"id":"1abc","name":"Q3 Report.pdf","mimeType":"application/pdf"}],"nextPageToken":null}})");
}

char* gda_list_recent_files(const char* argsJson) {
  return MakeCanned("list_recent_files", argsJson,
                     R"({"ok":true,"data":{"files":[{"id":"2def","name":"Notes.txt","mimeType":"text/plain"}],"nextPageToken":null}})");
}

char* gda_get_file_metadata(const char* argsJson) {
  return MakeCanned("get_file_metadata", argsJson,
                     R"({"ok":true,"data":{"id":"3ghi","name":"Budget.xlsx","mimeType":"application/vnd.ms-excel","parents":["root"],"createdTime":"2026-01-01T00:00:00Z","modifiedTime":"2026-01-02T00:00:00Z","trashed":false,"webViewLink":null,"size":"1024"}})");
}

char* gda_get_file_permissions(const char* argsJson) {
  return MakeCanned("get_file_permissions", argsJson,
                     R"({"ok":true,"data":{"permissions":[{"id":"perm1","type":"user","role":"reader","emailAddress":"a@example.com"}]}})");
}

char* gda_read_file_content(const char* argsJson) {
  return MakeCanned("read_file_content", argsJson,
                     R"({"ok":true,"data":{"content":"hello world","mimeType":"text/plain","comments":null}})");
}

char* gda_download_file_content(const char* argsJson) {
  return MakeCanned("download_file_content", argsJson,
                     R"({"ok":true,"data":{"base64Content":"aGVsbG8=","mimeType":"text/plain","filename":"hello.txt"}})");
}

char* gda_create_file(const char* argsJson) {
  return MakeCanned("create_file", argsJson,
                     R"({"ok":true,"data":{"id":"4jkl","name":"New Doc","mimeType":"application/vnd.google-apps.document","parents":["root"],"createdTime":"2026-01-03T00:00:00Z","modifiedTime":"2026-01-03T00:00:00Z","trashed":false,"webViewLink":null,"size":null}})");
}

char* gda_update_file(const char* argsJson) {
  return MakeCanned("update_file", argsJson,
                     R"({"ok":true,"data":{"id":"4jkl","name":"Renamed Doc","mimeType":"application/vnd.google-apps.document","parents":["root"],"createdTime":"2026-01-03T00:00:00Z","modifiedTime":"2026-01-04T00:00:00Z","trashed":false,"webViewLink":null,"size":null}})");
}

char* gda_copy_file(const char* argsJson) {
  return MakeCanned("copy_file", argsJson,
                     R"({"ok":true,"data":{"id":"5mno","name":"Renamed Doc - copy","mimeType":"application/vnd.google-apps.document","parents":["root"],"createdTime":"2026-01-05T00:00:00Z","modifiedTime":"2026-01-05T00:00:00Z","trashed":false,"webViewLink":null,"size":null}})");
}

char* gda_share_file(const char* argsJson) {
  return MakeCanned("share_file", argsJson,
                     R"({"ok":true,"data":{"id":"perm2","type":"user","role":"writer","emailAddress":"b@example.com"}})");
}

char* gda_trash_file(const char* argsJson) {
  return MakeCanned("trash_file", argsJson, R"({"ok":true,"data":{"id":"5mno","trashed":true}})");
}

void gda_free_string(char* s) { free(s); }

const char* fake_godrive_last_args(const char* fnName) {
  auto it = LastArgsRegistry().find(fnName);
  if (it == LastArgsRegistry().end()) return nullptr;
  return it->second.c_str();
}

void fake_godrive_reset(void) { LastArgsRegistry().clear(); }

}  // extern "C"
