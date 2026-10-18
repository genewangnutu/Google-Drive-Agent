#pragma once

// Test-only fake implementation of the C ABI godrive (Go, module 1) is
// contractually required to export -- see Doc/FFI-Contract.md §2. This
// stands in for the cgo-generated libgodrive.h so godrive_bridge.cpp's
// marshalling/ownership logic (copy char* into std::string, then
// gda_free_string) can be exercised without building or linking the real
// godrive Go c-archive. Canned, deterministic bodies only -- no real
// Drive/network behavior.
//
// NOT part of the FFI contract: the two introspection helpers at the
// bottom exist purely so tests can assert the bridge passed argsJson
// through byte-for-byte.

#ifdef __cplusplus
extern "C" {
#endif

char* gda_login(const char* argsJson);
char* gda_logout(void);
char* gda_search_files(const char* argsJson);
char* gda_list_recent_files(const char* argsJson);
char* gda_get_file_metadata(const char* argsJson);
char* gda_get_file_permissions(const char* argsJson);
char* gda_read_file_content(const char* argsJson);
char* gda_download_file_content(const char* argsJson);
char* gda_create_file(const char* argsJson);
char* gda_update_file(const char* argsJson);
char* gda_copy_file(const char* argsJson);
char* gda_share_file(const char* argsJson);
char* gda_trash_file(const char* argsJson);
void gda_free_string(char* s);

// Returns the argsJson most recently passed to the fake function named
// `fnName` (e.g. "search_files", matching the gda_<fnName> suffix), or
// nullptr if that function hasn't been called yet in this process.
const char* fake_godrive_last_args(const char* fnName);

// Clears all recorded last-args state. Call between TEST_CASEs that rely
// on fake_godrive_last_args so assertions don't leak across cases.
void fake_godrive_reset(void);

#ifdef __cplusplus
}
#endif
