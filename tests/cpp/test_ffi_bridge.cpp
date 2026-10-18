// Unit tests for src/ffi/godrive_bridge.cpp.
//
// These link against the fake C ABI in fake_godrive_abi.cpp/.h, NOT the
// real godrive Go c-archive (see Doc/FFI-Contract.md §2) -- that archive
// doesn't exist yet and these tests only need to verify GodriveBridge's
// marshalling/ownership logic: argsJson passed through unchanged, the
// fake's returned string copied back out unmodified, and no leak/corrupt
// of the Go-allocated (here: strdup'd) buffer. See this module's final
// report for the tests/CMakeLists.txt wiring this requires.
#include <doctest/doctest.h>
#include "fake_godrive_abi.h"
#include "gda/ffi/godrive_bridge.hpp"

namespace {

// doctest runs TEST_CASEs in one process; reset the fake's recorded-args
// state at the top of each case so assertions don't see a previous case's
// argsJson.
struct ResetFake {
  ResetFake() { fake_godrive_reset(); }
};

}  // namespace

TEST_CASE("SearchFiles passes argsJson through and returns the fake's envelope unmodified") {
  ResetFake reset;
  gda::GodriveBridge bridge;
  const std::string args = R"({"query":"title contains 'Q3'","pageSize":20,"pageToken":null})";

  std::string result = bridge.SearchFiles(args);

  CHECK(fake_godrive_last_args("search_files") != nullptr);
  CHECK(std::string(fake_godrive_last_args("search_files")) == args);
  CHECK(result ==
        R"({"ok":true,"data":{"files":[{"id":"1abc","name":"Q3 Report.pdf","mimeType":"application/pdf"}],"nextPageToken":null}})");
}

TEST_CASE("GetFileMetadata passes argsJson through and returns the fake's envelope unmodified") {
  ResetFake reset;
  gda::GodriveBridge bridge;
  const std::string args = R"({"fileId":"3ghi"})";

  std::string result = bridge.GetFileMetadata(args);

  CHECK(std::string(fake_godrive_last_args("get_file_metadata")) == args);
  CHECK(result ==
        R"({"ok":true,"data":{"id":"3ghi","name":"Budget.xlsx","mimeType":"application/vnd.ms-excel","parents":["root"],"createdTime":"2026-01-01T00:00:00Z","modifiedTime":"2026-01-02T00:00:00Z","trashed":false,"webViewLink":null,"size":"1024"}})");
}

TEST_CASE("TrashFile passes argsJson through and returns the fake's envelope unmodified") {
  ResetFake reset;
  gda::GodriveBridge bridge;
  const std::string args = R"({"fileId":"5mno"})";

  std::string result = bridge.TrashFile(args);

  CHECK(std::string(fake_godrive_last_args("trash_file")) == args);
  CHECK(result == R"({"ok":true,"data":{"id":"5mno","trashed":true}})");
}

TEST_CASE("Logout ignores its argument and still calls the no-arg fake") {
  ResetFake reset;
  gda::GodriveBridge bridge;

  // Doc/FFI-Contract.md §3.2: callers always pass "{}" (never a null
  // pointer); Logout must ignore it and call gda_logout() with no args.
  std::string result = bridge.Logout("{}");

  CHECK(std::string(fake_godrive_last_args("logout")) == "{}");
  CHECK(result == R"({"ok":true,"data":{"status":"logged_out"}})");
}

TEST_CASE("ShareFile round-trips a non-trivial argsJson payload without truncation") {
  ResetFake reset;
  gda::GodriveBridge bridge;
  const std::string args =
      R"({"fileId":"3ghi","role":"writer","emailAddress":"b@example.com","chatSpaceId":null})";

  std::string result = bridge.ShareFile(args);

  CHECK(std::string(fake_godrive_last_args("share_file")) == args);
  CHECK(result == R"({"ok":true,"data":{"id":"perm2","type":"user","role":"writer","emailAddress":"b@example.com"}})");
}

TEST_CASE("repeated calls on one bridge instance do not corrupt or leak prior results") {
  ResetFake reset;
  gda::GodriveBridge bridge;

  std::string first = bridge.SearchFiles(R"({"query":"a"})");
  std::string second = bridge.ListRecentFiles(R"({"orderBy":"recency"})");

  // If CopyAndFree() ever freed before copying, or copied a dangling
  // pointer, these would no longer match their expected canned values by
  // the time we get here (ASan also validated this -- see module report).
  CHECK(first ==
        R"({"ok":true,"data":{"files":[{"id":"1abc","name":"Q3 Report.pdf","mimeType":"application/pdf"}],"nextPageToken":null}})");
  CHECK(second ==
        R"({"ok":true,"data":{"files":[{"id":"2def","name":"Notes.txt","mimeType":"text/plain"}],"nextPageToken":null}})");
}
