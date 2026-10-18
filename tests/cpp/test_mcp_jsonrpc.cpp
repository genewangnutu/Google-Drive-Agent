// Tests the JSON-RPC handler logic directly (gda::mcp::HandleRequest /
// HandleRequestLine) against a minimal fake IGodriveBridge/ToolDispatcher
// pair -- no real httplib socket, no real godrive. See module brief in
// Doc/Module-Breakdown.md (Agent-MCP's file list).
//
// DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN is intentionally NOT defined here:
// tests/CMakeLists.txt links this file into the single gda_tests binary
// together with the other modules' test files, so exactly one of those
// translation units must provide doctest's main. For the standalone
// verification build (plain g++, this file + jsonrpc.cpp only) pass
// -DDOCTEST_CONFIG_IMPLEMENT_WITH_MAIN on the command line instead.
#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "gda/ffi/godrive_bridge.hpp"
#include "gda/mcp/jsonrpc.hpp"
#include "gda/tools/tool_dispatcher.hpp"

namespace {

// Canned envelopes (see Doc/FFI-Contract.md §2) for exactly the two calls
// these tests exercise; every other IGodriveBridge method just needs to
// compile (pure virtual), so they return an arbitrary ok envelope.
class FakeBridge final : public gda::IGodriveBridge {
 public:
  std::string Login(const std::string&) override { return R"({"ok":true,"data":{"status":"logged_in"}})"; }
  std::string Logout(const std::string&) override { return R"({"ok":true,"data":{"status":"logged_out"}})"; }
  std::string SearchFiles(const std::string&) override {
    return R"({"ok":true,"data":{"files":[{"id":"abc123","name":"foo.txt"}],"nextPageToken":null}})";
  }
  std::string ListRecentFiles(const std::string&) override { return R"({"ok":true,"data":{"files":[]}})"; }
  std::string GetFileMetadata(const std::string&) override {
    return R"({"ok":false,"error":{"code":"NOT_FOUND","http_status":404,)"
           R"("message":"file not found","retryable":false}})";
  }
  std::string GetFilePermissions(const std::string&) override { return R"({"ok":true,"data":{"permissions":[]}})"; }
  std::string ReadFileContent(const std::string&) override { return R"({"ok":true,"data":{"content":""}})"; }
  std::string DownloadFileContent(const std::string&) override {
    return R"({"ok":true,"data":{"base64Content":""}})";
  }
  std::string CreateFile(const std::string&) override { return R"({"ok":true,"data":{"id":"new1"}})"; }
  std::string UpdateFile(const std::string&) override { return R"({"ok":true,"data":{"id":"upd1"}})"; }
  std::string CopyFile(const std::string&) override { return R"({"ok":true,"data":{"id":"copy1"}})"; }
  std::string ShareFile(const std::string&) override { return R"({"ok":true,"data":{"id":"share1"}})"; }
  std::string TrashFile(const std::string&) override { return R"({"ok":true,"data":{"id":"trash1"}})"; }
};

}  // namespace

TEST_CASE("tools/call search_files round-trips through the JSON-RPC handler") {
  FakeBridge bridge;
  gda::ToolDispatcher dispatcher(bridge);

  const nlohmann::json request = {
      {"jsonrpc", "2.0"},
      {"id", 1},
      {"method", "tools/call"},
      {"params", {{"name", "search_files"}, {"arguments", {{"query", "title contains 'foo'"}, {"pageSize", 10}}}}},
  };

  const nlohmann::json response = gda::mcp::HandleRequest(dispatcher, request);

  CHECK(response.at("jsonrpc") == "2.0");
  CHECK(response.at("id") == 1);
  REQUIRE(response.contains("result"));
  CHECK_FALSE(response.contains("error"));

  const auto& result = response.at("result");
  CHECK(result.at("isError") == false);
  REQUIRE(result.at("content").is_array());
  REQUIRE(result.at("content").size() == 1);
  CHECK(result.at("content")[0].at("type") == "text");

  const nlohmann::json data = nlohmann::json::parse(result.at("content")[0].at("text").get<std::string>());
  REQUIRE(data.at("files").size() == 1);
  CHECK(data.at("files")[0].at("id") == "abc123");
}

TEST_CASE("tools/call with an unknown tool name is a JSON-RPC error") {
  FakeBridge bridge;
  gda::ToolDispatcher dispatcher(bridge);

  const nlohmann::json request = {
      {"jsonrpc", "2.0"},
      {"id", 2},
      {"method", "tools/call"},
      {"params", {{"name", "not_a_real_tool"}, {"arguments", nlohmann::json::object()}}},
  };

  const nlohmann::json response = gda::mcp::HandleRequest(dispatcher, request);

  CHECK_FALSE(response.contains("result"));
  REQUIRE(response.contains("error"));
  CHECK(response.at("error").at("code") == static_cast<int>(gda::mcp::JsonRpcErrorCode::kInvalidParams));
}

TEST_CASE("malformed (non-JSON) request line produces a JSON-RPC parse error") {
  FakeBridge bridge;
  gda::ToolDispatcher dispatcher(bridge);

  const std::string responseLine = gda::mcp::HandleRequestLine(dispatcher, "{not valid json");
  const nlohmann::json response = nlohmann::json::parse(responseLine);

  REQUIRE(response.contains("error"));
  CHECK(response.at("error").at("code") == static_cast<int>(gda::mcp::JsonRpcErrorCode::kParseError));
  CHECK(response.at("id") == nullptr);
}

TEST_CASE("a ToolError thrown by the dispatcher becomes a server-error JSON-RPC response") {
  FakeBridge bridge;
  gda::ToolDispatcher dispatcher(bridge);

  const nlohmann::json request = {
      {"jsonrpc", "2.0"},
      {"id", "req-3"},
      {"method", "tools/call"},
      {"params", {{"name", "get_file_metadata"}, {"arguments", {{"fileId", "missing-id"}}}}},
  };

  const nlohmann::json response = gda::mcp::HandleRequest(dispatcher, request);

  CHECK(response.at("id") == "req-3");
  CHECK_FALSE(response.contains("result"));
  REQUIRE(response.contains("error"));

  const auto& error = response.at("error");
  CHECK(error.at("code") == static_cast<int>(gda::mcp::JsonRpcErrorCode::kToolError));
  CHECK(error.at("message") == "file not found");
  REQUIRE(error.contains("data"));
  CHECK(error.at("data").at("code") == "NOT_FOUND");
  CHECK(error.at("data").at("http_status") == 404);
  CHECK(error.at("data").at("retryable") == false);
}
