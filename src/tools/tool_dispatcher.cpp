#include "gda/tools/tool_dispatcher.hpp"

#include <initializer_list>
#include <utility>

#include <nlohmann/json.hpp>

namespace gda {

namespace {

using nlohmann::json;

// Parses argsJson into a JSON object, defaulting an empty string to `{}`
// (callers that have no arguments, e.g. Logout, still pass "{}"). Any
// malformed JSON or a non-object top level is reported as the same
// INVALID_ARGUMENT ToolError that field-level validation below uses, so
// callers only ever need to catch one exception type from this class.
json ParseArgs(const std::string& argsJson) {
  if (argsJson.empty()) return json::object();
  json parsed = json::parse(argsJson);
  if (!parsed.is_object()) {
    throw ToolError("INVALID_ARGUMENT", "arguments must be a JSON object", 400, false);
  }
  return parsed;
}

std::string RequireString(const json& args, const char* key) {
  auto it = args.find(key);
  if (it == args.end() || !it->is_string() || it->get<std::string>().empty()) {
    throw ToolError("INVALID_ARGUMENT", std::string(key) + " is required", 400, false);
  }
  return it->get<std::string>();
}

std::string RequireEnum(const json& args, const char* key, std::initializer_list<const char*> allowed) {
  std::string value = RequireString(args, key);
  for (const char* option : allowed) {
    if (value == option) return value;
  }
  throw ToolError("INVALID_ARGUMENT", std::string(key) + " has an unsupported value", 400, false);
}

// Optional "string|null" fields (per FFI-Contract.md section 3): absent and
// explicit null both become JSON null in the args handed to the bridge.
json OptionalOrNull(const json& args, const char* key) {
  if (args.contains(key) && !args.at(key).is_null()) return args.at(key);
  return nullptr;
}

// Runs one dispatcher method's body, converting any stray nlohmann::json
// exception (malformed/mistyped fields the per-field helpers above didn't
// already catch) into the same ToolError the rest of this class throws, so
// CLI/MCP callers only ever need to catch gda::ToolError.
template <typename Fn>
std::string Dispatch(Fn&& fn) {
  try {
    return fn();
  } catch (const ToolError&) {
    throw;
  } catch (const json::exception& e) {
    throw ToolError("INVALID_ARGUMENT", std::string("malformed arguments: ") + e.what(), 400, false);
  }
}

}  // namespace

std::string ToolDispatcher::UnwrapEnvelope(const std::string& envelopeJson) const {
  json envelope;
  try {
    envelope = json::parse(envelopeJson);
  } catch (const json::parse_error& e) {
    throw ToolError("INTERNAL", std::string("malformed response from bridge: ") + e.what(), 0, false);
  }
  if (envelope.value("ok", false)) {
    return envelope.value("data", json::object()).dump();
  }
  json error = envelope.contains("error") && envelope.at("error").is_object() ? envelope.at("error") : json::object();
  throw ToolError(error.value("code", std::string("INTERNAL")),
                   error.value("message", std::string("bridge returned a failure envelope with no message")),
                   error.value("http_status", 0),
                   error.value("retryable", false));
}

std::string ToolDispatcher::Login(const std::string& argsJson) {
  return Dispatch([&] {
    json args = ParseArgs(argsJson);
    std::string mode = args.value("mode", std::string("browser"));
    if (mode != "browser" && mode != "device") {
      throw ToolError("INVALID_ARGUMENT", "mode must be 'browser' or 'device'", 400, false);
    }
    json out = json::object();
    out["mode"] = mode;
    return UnwrapEnvelope(bridge_.Login(out.dump()));
  });
}

std::string ToolDispatcher::Logout() {
  return Dispatch([&] { return UnwrapEnvelope(bridge_.Logout("{}")); });
}

std::string ToolDispatcher::SearchFiles(const std::string& argsJson) {
  return Dispatch([&] {
    json args = ParseArgs(argsJson);
    json out = json::object();
    out["query"] = RequireString(args, "query");
    out["pageSize"] = args.value("pageSize", 20);
    out["pageToken"] = OptionalOrNull(args, "pageToken");
    return UnwrapEnvelope(bridge_.SearchFiles(out.dump()));
  });
}

std::string ToolDispatcher::ListRecentFiles(const std::string& argsJson) {
  return Dispatch([&] {
    json args = ParseArgs(argsJson);
    json out = json::object();
    out["orderBy"] = args.contains("orderBy")
                          ? RequireEnum(args, "orderBy", {"recency", "lastModified", "lastModifiedByMe"})
                          : std::string("recency");
    out["pageSize"] = args.value("pageSize", 20);
    out["pageToken"] = OptionalOrNull(args, "pageToken");
    return UnwrapEnvelope(bridge_.ListRecentFiles(out.dump()));
  });
}

std::string ToolDispatcher::GetFileMetadata(const std::string& argsJson) {
  return Dispatch([&] {
    json args = ParseArgs(argsJson);
    json out = json::object();
    out["fileId"] = RequireString(args, "fileId");
    return UnwrapEnvelope(bridge_.GetFileMetadata(out.dump()));
  });
}

std::string ToolDispatcher::GetFilePermissions(const std::string& argsJson) {
  return Dispatch([&] {
    json args = ParseArgs(argsJson);
    json out = json::object();
    out["fileId"] = RequireString(args, "fileId");
    return UnwrapEnvelope(bridge_.GetFilePermissions(out.dump()));
  });
}

std::string ToolDispatcher::ReadFileContent(const std::string& argsJson) {
  return Dispatch([&] {
    json args = ParseArgs(argsJson);
    json out = json::object();
    out["fileId"] = RequireString(args, "fileId");
    out["includeComments"] = args.value("includeComments", false);
    return UnwrapEnvelope(bridge_.ReadFileContent(out.dump()));
  });
}

std::string ToolDispatcher::DownloadFileContent(const std::string& argsJson) {
  return Dispatch([&] {
    json args = ParseArgs(argsJson);
    json out = json::object();
    out["fileId"] = RequireString(args, "fileId");
    out["exportMimeType"] = OptionalOrNull(args, "exportMimeType");
    out["revisionId"] = OptionalOrNull(args, "revisionId");
    return UnwrapEnvelope(bridge_.DownloadFileContent(out.dump()));
  });
}

std::string ToolDispatcher::CreateFile(const std::string& argsJson) {
  return Dispatch([&] {
    json args = ParseArgs(argsJson);
    json out = json::object();
    out["title"] = RequireString(args, "title");
    out["parentId"] = OptionalOrNull(args, "parentId");
    out["textContent"] = OptionalOrNull(args, "textContent");
    out["base64Content"] = OptionalOrNull(args, "base64Content");
    out["contentMimeType"] = OptionalOrNull(args, "contentMimeType");
    out["disableConversionToGoogleType"] = args.value("disableConversionToGoogleType", false);
    return UnwrapEnvelope(bridge_.CreateFile(out.dump()));
  });
}

std::string ToolDispatcher::UpdateFile(const std::string& argsJson) {
  return Dispatch([&] {
    json args = ParseArgs(argsJson);
    json out = json::object();
    out["fileId"] = RequireString(args, "fileId");
    out["title"] = OptionalOrNull(args, "title");
    out["parentId"] = OptionalOrNull(args, "parentId");
    out["description"] = OptionalOrNull(args, "description");
    return UnwrapEnvelope(bridge_.UpdateFile(out.dump()));
  });
}

std::string ToolDispatcher::CopyFile(const std::string& argsJson) {
  return Dispatch([&] {
    json args = ParseArgs(argsJson);
    json out = json::object();
    out["fileId"] = RequireString(args, "fileId");
    out["title"] = OptionalOrNull(args, "title");
    out["parentId"] = OptionalOrNull(args, "parentId");
    return UnwrapEnvelope(bridge_.CopyFile(out.dump()));
  });
}

std::string ToolDispatcher::ShareFile(const std::string& argsJson) {
  return Dispatch([&] {
    json args = ParseArgs(argsJson);
    json out = json::object();
    out["fileId"] = RequireString(args, "fileId");
    out["role"] = RequireEnum(args, "role", {"writer", "commenter", "reader"});
    out["emailAddress"] = OptionalOrNull(args, "emailAddress");
    out["chatSpaceId"] = OptionalOrNull(args, "chatSpaceId");
    return UnwrapEnvelope(bridge_.ShareFile(out.dump()));
  });
}

std::string ToolDispatcher::TrashFile(const std::string& argsJson) {
  return Dispatch([&] {
    json args = ParseArgs(argsJson);
    json out = json::object();
    out["fileId"] = RequireString(args, "fileId");
    return UnwrapEnvelope(bridge_.TrashFile(out.dump()));
  });
}

}  // namespace gda
