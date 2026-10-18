#include "gda/mcp/transport_http.hpp"

#include <httplib.h>

#include <nlohmann/json.hpp>

#include "gda/mcp/jsonrpc.hpp"

namespace gda::mcp {

namespace {

// 2026-07-28 Streamable HTTP transport: a client may carry the JSON-RPC
// `method` / `params.name` out-of-band in these headers instead of putting
// them in the body, so a thin body like {"id":1,"params":{"arguments":{}}}
// is valid as long as the headers fill in what the body omits. Body values
// win when both are present.
void ApplyMcpHeaders(const httplib::Request& req, nlohmann::json& request) {
  if (!request.is_object()) {
    request = nlohmann::json::object();
  }
  if (const auto it = req.headers.find("Mcp-Method"); it != req.headers.end() && !request.contains("method")) {
    request["method"] = it->second;
  }
  if (const auto it = req.headers.find("Mcp-Name"); it != req.headers.end()) {
    if (!request.contains("params") || !request["params"].is_object()) {
      request["params"] = nlohmann::json::object();
    }
    if (!request["params"].contains("name")) {
      request["params"]["name"] = it->second;
    }
  }
}

}  // namespace

void RunHttpServer(gda::ToolDispatcher& dispatcher, int port) {
  httplib::Server svr;
  svr.Post("/", [&dispatcher](const httplib::Request& req, httplib::Response& res) {
    nlohmann::json request = nlohmann::json::parse(req.body, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (request.is_discarded()) {
      request = nlohmann::json::object();
    }
    ApplyMcpHeaders(req, request);
    res.set_content(HandleRequest(dispatcher, request).dump(), "application/json");
  });
  svr.listen("127.0.0.1", port);
}

}  // namespace gda::mcp
