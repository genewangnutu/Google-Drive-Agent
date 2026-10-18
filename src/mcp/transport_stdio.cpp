#include "gda/mcp/transport_stdio.hpp"

#include <iostream>
#include <string>

#include "gda/mcp/jsonrpc.hpp"

namespace gda::mcp {

void RunStdioServer(gda::ToolDispatcher& dispatcher) {
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) {
      continue;  // tolerate blank keep-alive lines between requests
    }
    std::cout << HandleRequestLine(dispatcher, line) << "\n";
    std::cout.flush();
  }
}

}  // namespace gda::mcp
