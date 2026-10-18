#include "gda/mcp/jsonrpc.hpp"

#include <utility>

namespace gda::mcp {

namespace {

// MCP `tools/call` success result shape: the dispatcher's JSON-encoded data
// string becomes the one text content block's `text` verbatim -- no need to
// re-parse/re-serialize it.
nlohmann::json BuildToolCallResult(const std::string& dataJson) {
  nlohmann::json content = nlohmann::json::array();
  content.push_back({{"type", "text"}, {"text", dataJson}});
  return {
      {"content", content},
      {"isError", false},
      {"_meta", {{"protocolVersion", kProtocolVersion}}},
  };
}

}  // namespace

const std::unordered_map<std::string, ToolHandler>& ToolRegistry() {
  static const std::unordered_map<std::string, ToolHandler> registry = {
      {"search_files", [](gda::ToolDispatcher& d, const std::string& a) { return d.SearchFiles(a); }},
      {"list_recent_files", [](gda::ToolDispatcher& d, const std::string& a) { return d.ListRecentFiles(a); }},
      {"get_file_metadata", [](gda::ToolDispatcher& d, const std::string& a) { return d.GetFileMetadata(a); }},
      {"get_file_permissions",
       [](gda::ToolDispatcher& d, const std::string& a) { return d.GetFilePermissions(a); }},
      {"read_file_content", [](gda::ToolDispatcher& d, const std::string& a) { return d.ReadFileContent(a); }},
      {"download_file_content",
       [](gda::ToolDispatcher& d, const std::string& a) { return d.DownloadFileContent(a); }},
      {"create_file", [](gda::ToolDispatcher& d, const std::string& a) { return d.CreateFile(a); }},
      {"update_file", [](gda::ToolDispatcher& d, const std::string& a) { return d.UpdateFile(a); }},
      {"copy_file", [](gda::ToolDispatcher& d, const std::string& a) { return d.CopyFile(a); }},
      {"share_file", [](gda::ToolDispatcher& d, const std::string& a) { return d.ShareFile(a); }},
      {"trash_file", [](gda::ToolDispatcher& d, const std::string& a) { return d.TrashFile(a); }},
      // Extra convenience tools (not in the original 11; CLI exposes these
      // as `login`/`logout` commands, see report).
      {"login", [](gda::ToolDispatcher& d, const std::string& a) { return d.Login(a); }},
      {"logout", [](gda::ToolDispatcher& d, const std::string&) { return d.Logout(); }},
  };
  return registry;
}

nlohmann::json MakeSuccessResponse(const nlohmann::json& id, nlohmann::json result) {
  return {
      {"jsonrpc", "2.0"},
      {"id", id},
      {"result", std::move(result)},
  };
}

nlohmann::json MakeErrorResponse(const nlohmann::json& id, int code, const std::string& message,
                                  nlohmann::json data) {
  nlohmann::json error = {{"code", code}, {"message", message}};
  if (!data.is_null()) {
    error["data"] = std::move(data);
  }
  return {
      {"jsonrpc", "2.0"},
      {"id", id},
      {"error", std::move(error)},
  };
}

nlohmann::json MakeToolErrorResponse(const nlohmann::json& id, const gda::ToolError& err) {
  nlohmann::json data = {
      {"code", err.code()},
      {"http_status", err.http_status()},
      {"retryable", err.retryable()},
  };
  return MakeErrorResponse(id, static_cast<int>(JsonRpcErrorCode::kToolError), err.what(), std::move(data));
}

nlohmann::json HandleRequest(gda::ToolDispatcher& dispatcher, const nlohmann::json& request) {
  const nlohmann::json id = (request.is_object() && request.contains("id")) ? request.at("id") : nlohmann::json(nullptr);

  if (!request.is_object() || !request.contains("method") || !request.at("method").is_string()) {
    return MakeErrorResponse(id, static_cast<int>(JsonRpcErrorCode::kInvalidRequest),
                              "invalid JSON-RPC request: missing/non-string \"method\"");
  }
  const std::string method = request.at("method").get<std::string>();
  if (method != "tools/call") {
    // This facade is stateless (no `initialize`/`initialized`); the only
    // method it understands is `tools/call`.
    return MakeErrorResponse(id, static_cast<int>(JsonRpcErrorCode::kMethodNotFound),
                              "method not found: " + method);
  }

  const auto& params = request.contains("params") ? request.at("params") : nlohmann::json(nullptr);
  if (!params.is_object() || !params.contains("name") || !params.at("name").is_string()) {
    return MakeErrorResponse(id, static_cast<int>(JsonRpcErrorCode::kInvalidParams),
                              "params.name (string) is required for tools/call");
  }
  const std::string toolName = params.at("name").get<std::string>();

  const auto& registry = ToolRegistry();
  const auto it = registry.find(toolName);
  if (it == registry.end()) {
    return MakeErrorResponse(id, static_cast<int>(JsonRpcErrorCode::kInvalidParams),
                              "unknown tool: " + toolName);
  }

  const nlohmann::json arguments = params.value("arguments", nlohmann::json::object());
  const std::string argsStr = arguments.dump();

  try {
    const std::string data = it->second(dispatcher, argsStr);
    return MakeSuccessResponse(id, BuildToolCallResult(data));
  } catch (const gda::ToolError& err) {
    return MakeToolErrorResponse(id, err);
  } catch (const std::exception& ex) {
    return MakeErrorResponse(id, static_cast<int>(JsonRpcErrorCode::kInternalError), ex.what());
  }
}

std::string HandleRequestLine(gda::ToolDispatcher& dispatcher, const std::string& rawJson) {
  const nlohmann::json parsed = nlohmann::json::parse(rawJson, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (parsed.is_discarded()) {
    return MakeErrorResponse(nullptr, static_cast<int>(JsonRpcErrorCode::kParseError), "invalid JSON").dump();
  }
  return HandleRequest(dispatcher, parsed).dump();
}

}  // namespace gda::mcp
