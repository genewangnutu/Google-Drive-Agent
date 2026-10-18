#pragma once

// Umbrella header for the MCP Server Facade's two entry points. The CLI
// module (src/cli/main.cpp) includes only this header; the actual
// declarations live in transport_stdio.hpp / transport_http.hpp, which this
// re-exports:
//   void gda::mcp::RunStdioServer(gda::ToolDispatcher& dispatcher);
//   void gda::mcp::RunHttpServer(gda::ToolDispatcher& dispatcher, int port);
#include "gda/mcp/transport_http.hpp"
#include "gda/mcp/transport_stdio.hpp"
