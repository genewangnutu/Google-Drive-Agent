#pragma once

#include "gda/tools/tool_dispatcher.hpp"

namespace gda::mcp {

// MCP Streamable HTTP transport (Plan §6.6): a single POST "/" endpoint on
// 127.0.0.1:`port` accepting one JSON-RPC request per call, honoring the
// 2026-07-28 spec's Mcp-Method/Mcp-Name headers (see transport_http.cpp).
// Blocks the calling thread until the server is stopped.
void RunHttpServer(gda::ToolDispatcher& dispatcher, int port);

}  // namespace gda::mcp
