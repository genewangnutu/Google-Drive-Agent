// DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN is provided once, centrally, by
// tests/cpp/test_main.cpp -- defining it here too would duplicate main()
// when linked into the shared gda_tests binary.
#include <doctest/doctest.h>

#include <nlohmann/json.hpp>
#include <string>

#include "fake_godrive_bridge.hpp"
#include "gda/tools/tool_dispatcher.hpp"

namespace {

using nlohmann::json;

std::string FixturePath(const std::string& name) {
  return std::string(GDA_TEST_FIXTURES_DIR) + "/ffi_responses/" + name;
}

}  // namespace

TEST_CASE("SearchFiles happy path returns two files") {
  gda::FakeGodriveBridge fake;
  fake.LoadFixture(FixturePath("search_files_basic.json"));
  gda::ToolDispatcher dispatcher(fake);

  std::string result = dispatcher.SearchFiles(R"({"query":"title contains 'Q3'"})");
  json data = json::parse(result);

  REQUIRE(data["files"].size() == 2);
  CHECK(data["files"][0]["id"] == "1abc");
  CHECK(data["files"][1]["id"] == "2def");
  CHECK(data["nextPageToken"].is_null());
}

TEST_CASE("SearchFiles defaults pageSize to 20 and pageToken to null") {
  gda::FakeGodriveBridge fake;
  fake.LoadFixture(FixturePath("search_files_basic.json"));
  gda::ToolDispatcher dispatcher(fake);

  dispatcher.SearchFiles(R"({"query":"title contains 'Q3'"})");
  json sentArgs = json::parse(fake.LastArgsJson());

  CHECK(sentArgs["pageSize"] == 20);
  CHECK(sentArgs["pageToken"].is_null());
  CHECK(sentArgs["query"] == "title contains 'Q3'");
}

TEST_CASE("SearchFiles passes through an explicit pageSize and pageToken") {
  gda::FakeGodriveBridge fake;
  fake.LoadFixture(FixturePath("search_files_basic.json"));
  gda::ToolDispatcher dispatcher(fake);

  dispatcher.SearchFiles(R"({"query":"x","pageSize":5,"pageToken":"tok123"})");
  json sentArgs = json::parse(fake.LastArgsJson());

  CHECK(sentArgs["pageSize"] == 5);
  CHECK(sentArgs["pageToken"] == "tok123");
}

TEST_CASE("SearchFiles empty results") {
  gda::FakeGodriveBridge fake;
  fake.LoadFixture(FixturePath("search_files_empty.json"));
  gda::ToolDispatcher dispatcher(fake);

  std::string result = dispatcher.SearchFiles(R"({"query":"title contains 'nonexistent'"})");
  json data = json::parse(result);

  CHECK(data["files"].empty());
}

TEST_CASE("SearchFiles without a query throws ToolError INVALID_ARGUMENT") {
  gda::FakeGodriveBridge fake;
  gda::ToolDispatcher dispatcher(fake);

  try {
    dispatcher.SearchFiles("{}");
    FAIL("expected gda::ToolError to be thrown");
  } catch (const gda::ToolError& e) {
    CHECK(e.code() == "INVALID_ARGUMENT");
    CHECK_FALSE(e.retryable());
  }
}

TEST_CASE("GetFileMetadata happy path") {
  gda::FakeGodriveBridge fake;
  fake.LoadFixture(FixturePath("get_file_metadata_doc.json"));
  gda::ToolDispatcher dispatcher(fake);

  std::string result = dispatcher.GetFileMetadata(R"({"fileId":"1abc"})");
  json data = json::parse(result);

  CHECK(data["id"] == "1abc");
  CHECK(data["name"] == "Q3 Report.pdf");
  CHECK(data["mimeType"] == "application/pdf");
  CHECK(data["trashed"] == false);
}

TEST_CASE("GetFileMetadata without a fileId throws ToolError INVALID_ARGUMENT") {
  gda::FakeGodriveBridge fake;
  gda::ToolDispatcher dispatcher(fake);

  CHECK_THROWS_AS(dispatcher.GetFileMetadata("{}"), gda::ToolError);
}

TEST_CASE("GetFileMetadata error envelope throws ToolError with NOT_FOUND fields") {
  gda::FakeGodriveBridge fake;
  fake.LoadFixture(FixturePath("error_not_found.json"));
  gda::ToolDispatcher dispatcher(fake);

  try {
    dispatcher.GetFileMetadata(R"({"fileId":"missing"})");
    FAIL("expected gda::ToolError to be thrown");
  } catch (const gda::ToolError& e) {
    CHECK(e.code() == "NOT_FOUND");
    CHECK(e.http_status() == 404);
    CHECK_FALSE(e.retryable());
    CHECK(std::string(e.what()) == "file 1abc not found");
  }
}

TEST_CASE("TrashFile error envelope also surfaces as ToolError") {
  gda::FakeGodriveBridge fake;
  fake.LoadFixture(FixturePath("error_not_found.json"));
  gda::ToolDispatcher dispatcher(fake);

  CHECK_THROWS_AS(dispatcher.TrashFile(R"({"fileId":"missing"})"), gda::ToolError);
}

TEST_CASE("ListRecentFiles defaults orderBy to recency") {
  gda::FakeGodriveBridge fake;
  fake.LoadFixture(FixturePath("search_files_basic.json"));
  gda::ToolDispatcher dispatcher(fake);

  dispatcher.ListRecentFiles("{}");
  json sentArgs = json::parse(fake.LastArgsJson());

  CHECK(sentArgs["orderBy"] == "recency");
  CHECK(sentArgs["pageSize"] == 20);
}

TEST_CASE("ListRecentFiles rejects an invalid orderBy") {
  gda::FakeGodriveBridge fake;
  gda::ToolDispatcher dispatcher(fake);

  CHECK_THROWS_AS(dispatcher.ListRecentFiles(R"({"orderBy":"bogus"})"), gda::ToolError);
}

TEST_CASE("ShareFile rejects an invalid role") {
  gda::FakeGodriveBridge fake;
  gda::ToolDispatcher dispatcher(fake);

  CHECK_THROWS_AS(dispatcher.ShareFile(R"({"fileId":"1abc","role":"owner"})"), gda::ToolError);
}

TEST_CASE("ShareFile passes through emailAddress and defaults chatSpaceId to null") {
  gda::FakeGodriveBridge fake;
  fake.SetNextResponse(R"({"ok":true,"data":{"id":"p1","type":"user","role":"writer","emailAddress":"a@b.com"}})");
  gda::ToolDispatcher dispatcher(fake);

  std::string result = dispatcher.ShareFile(R"({"fileId":"1abc","role":"writer","emailAddress":"a@b.com"})");
  json sentArgs = json::parse(fake.LastArgsJson());
  json data = json::parse(result);

  CHECK(sentArgs["emailAddress"] == "a@b.com");
  CHECK(sentArgs["chatSpaceId"].is_null());
  CHECK(data["role"] == "writer");
}

TEST_CASE("CreateFile requires a title") {
  gda::FakeGodriveBridge fake;
  gda::ToolDispatcher dispatcher(fake);

  CHECK_THROWS_AS(dispatcher.CreateFile(R"({"textContent":"hello"})"), gda::ToolError);
}

TEST_CASE("CreateFile defaults optional fields to null/false") {
  gda::FakeGodriveBridge fake;
  fake.LoadFixture(FixturePath("get_file_metadata_doc.json"));
  gda::ToolDispatcher dispatcher(fake);

  dispatcher.CreateFile(R"({"title":"New Doc"})");
  json sentArgs = json::parse(fake.LastArgsJson());

  CHECK(sentArgs["title"] == "New Doc");
  CHECK(sentArgs["parentId"].is_null());
  CHECK(sentArgs["textContent"].is_null());
  CHECK(sentArgs["base64Content"].is_null());
  CHECK(sentArgs["contentMimeType"].is_null());
  CHECK(sentArgs["disableConversionToGoogleType"] == false);
}

TEST_CASE("Login defaults mode to browser and rejects an invalid mode") {
  gda::FakeGodriveBridge fake;
  fake.SetNextResponse(R"({"ok":true,"data":{"status":"logged_in"}})");
  gda::ToolDispatcher dispatcher(fake);

  dispatcher.Login("{}");
  json sentArgs = json::parse(fake.LastArgsJson());
  CHECK(sentArgs["mode"] == "browser");

  CHECK_THROWS_AS(dispatcher.Login(R"({"mode":"carrier-pigeon"})"), gda::ToolError);
}

TEST_CASE("Logout calls through with an empty object and unwraps the envelope") {
  gda::FakeGodriveBridge fake;
  fake.SetNextResponse(R"({"ok":true,"data":{"status":"logged_out"}})");
  gda::ToolDispatcher dispatcher(fake);

  std::string result = dispatcher.Logout();
  json data = json::parse(result);

  CHECK(fake.LastArgsJson() == "{}");
  CHECK(data["status"] == "logged_out");
}
