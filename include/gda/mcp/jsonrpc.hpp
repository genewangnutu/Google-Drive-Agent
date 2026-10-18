#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "gda/tools/tool_dispatcher.hpp"

namespace gda::mcp {

// MCP protocol revision this facade implements (Plan §3/§6.6): stateless,
// no initialize handshake, _meta carried on requests/results instead.
inline constexpr const char* kProtocolVersion = "2026-07-28";

// JSON-RPC 2.0 reserved error codes (spec 5.1) plus the -320xx "server
// error" band this facade uses to report a caught gda::ToolError.
enum class JsonRpcErrorCode : int {
  kParseError = -32700,
  kInvalidRequest = -32600,
  kMethodNotFound = -32601,
  kInvalidParams = -32602,
  kInternalError = -32603,
  kToolError = -32000,
};

// (dispatcher, argsJson) -> JSON-encoded data string, i.e. one ToolDispatcher
// method bound to its MCP tool name.
using ToolHandler = std::function<std::string(gda::ToolDispatcher&, const std::string&)>;

// Tool name -> handler for the 11 Plan-table tools plus the `login`/`logout`
// convenience tools this facade also exposes. Built once, read-only.
const std::unordered_map<std::string, ToolHandler>& ToolRegistry();

// Builds {"jsonrpc":"2.0","id":id,"result":result}.
nlohmann::json MakeSuccessResponse(const nlohmann::json& id, nlohmann::json result);

// Builds {"jsonrpc":"2.0","id":id,"error":{"code":code,"message":message,"data":data?}}.
// `data` is omitted from the error object when left null.
nlohmann::json MakeErrorResponse(const nlohmann::json& id, int code, const std::string& message,
                                  nlohmann::json data = nullptr);

// Translates a caught gda::ToolError into the server-error (-32000..-32099)
// response shape: data = {"code":ToolError::code(), "http_status":...,
// "retryable":...}, message = ToolError::what().
nlohmann::json MakeToolErrorResponse(const nlohmann::json& id, const gda::ToolError& err);

// Handles one already-parsed JSON-RPC request object. Only `tools/call` is
// supported -- this facade is stateless, there is no `initialize`. Never
// throws: any failure (bad shape, unknown tool, ToolError, other exception)
// becomes a JSON-RPC error response object instead.
nlohmann::json HandleRequest(gda::ToolDispatcher& dispatcher, const nlohmann::json& request);

// Parses `rawJson` and dispatches it via HandleRequest, returning the
// serialized response line. An unparseable `rawJson` yields a Parse error
// (-32700) response rather than throwing.
std::string HandleRequestLine(gda::ToolDispatcher& dispatcher, const std::string& rawJson);

}  // namespace gda::mcp
