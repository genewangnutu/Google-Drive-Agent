#pragma once

#include <stdexcept>
#include <string>

#include "gda/ffi/godrive_bridge.hpp"

namespace gda {

// Thrown by ToolDispatcher methods when the underlying IGodriveBridge call
// returns a {"ok":false,"error":{...}} envelope (see Doc/FFI-Contract.md).
// Callers (CLI commands, MCP tool handlers) catch this to format an error
// message / JSON-RPC error response -- they should not need to re-parse
// envelopes themselves.
class ToolError final : public std::runtime_error {
 public:
  ToolError(std::string code, std::string message, int httpStatus, bool retryable)
      : std::runtime_error(message),
        code_(std::move(code)),
        httpStatus_(httpStatus),
        retryable_(retryable) {}

  const std::string& code() const { return code_; }
  int http_status() const { return httpStatus_; }
  bool retryable() const { return retryable_; }

 private:
  std::string code_;
  int httpStatus_;
  bool retryable_;
};

// One method per MCP tool (see Doc/Google-Drive-Agent-Plan.md §6.3),
// plus Login/Logout for the CLI's `login`/`logout` commands. Each method:
//  1. takes a JSON string of arguments shaped like the corresponding
//     mcp__claude_ai_Google_Drive__* tool's input schema (field names such
//     as "query", "fileId", "pageSize" -- see Doc/FFI-Contract.md section 3
//     for the exact shape each underlying bridge call expects/returns)
//  2. delegates to the injected IGodriveBridge
//  3. on a {"ok":false,...} envelope, throws ToolError
//  4. on success, returns the JSON-encoded "data" object as a string
//     (callers parse it with nlohmann::json as needed)
//
// Constructed with any IGodriveBridge implementation: the real
// GodriveBridge in production (wired up in src/cli/main.cpp), or a
// FakeGodriveBridge reading canned JSON from tests/cpp/fixtures/ in tests
// (see tests/cpp/test_tool_dispatcher.cpp). This seam is what lets this
// class be unit-tested without building/linking godrive or touching a
// real Google account.
class ToolDispatcher {
 public:
  explicit ToolDispatcher(IGodriveBridge& bridge) : bridge_(bridge) {}

  std::string Login(const std::string& argsJson);
  std::string Logout();
  std::string SearchFiles(const std::string& argsJson);
  std::string ListRecentFiles(const std::string& argsJson);
  std::string GetFileMetadata(const std::string& argsJson);
  std::string GetFilePermissions(const std::string& argsJson);
  std::string ReadFileContent(const std::string& argsJson);
  std::string DownloadFileContent(const std::string& argsJson);
  std::string CreateFile(const std::string& argsJson);
  std::string UpdateFile(const std::string& argsJson);
  std::string CopyFile(const std::string& argsJson);
  std::string ShareFile(const std::string& argsJson);
  std::string TrashFile(const std::string& argsJson);

 private:
  // Shared helper: call `bridgeFn(argsJson)`, parse the envelope, throw
  // ToolError on failure, otherwise return the "data" member serialized
  // back to a string. Implemented once in tool_dispatcher.cpp and reused
  // by all 13 public methods above.
  std::string UnwrapEnvelope(const std::string& envelopeJson) const;

  IGodriveBridge& bridge_;
};

}  // namespace gda
