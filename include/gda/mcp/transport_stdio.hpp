#pragma once

#include "gda/tools/tool_dispatcher.hpp"

namespace gda::mcp {

// MCP `stdio` transport (Plan §6.6): reads newline-delimited JSON-RPC
// request lines from stdin, writes newline-delimited JSON-RPC response
// lines to stdout, until stdin reaches EOF. Blocks the calling thread.
void RunStdioServer(gda::ToolDispatcher& dispatcher);

}  // namespace gda::mcp
