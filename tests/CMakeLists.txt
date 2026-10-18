FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG        v2.4.11
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(doctest)

# ---- gda_ffi_test_double --------------------------------------------------
# Compiles the SAME src/ffi/godrive_bridge.cpp that `gda_ffi` compiles for
# production, but resolves its `#include "libgodrive.h"` to the hand-written
# fake C ABI under cpp/fake_abi_include/ + cpp/fake_godrive_abi.cpp instead
# of the real cgo-generated header + the real `godrive` archive. This lets
# test_ffi_bridge.cpp exercise GodriveBridge's real marshalling/string-
# ownership logic without a Go toolchain or a real account (Plan 8.2's
# intent) -- and avoids linking both the fake ABI and the real `godrive`
# archive into one binary, which would be a duplicate-symbol error (both
# define extern "C" gda_search_files() etc).
add_library(gda_ffi_test_double STATIC
  ${CMAKE_SOURCE_DIR}/src/ffi/godrive_bridge.cpp
  cpp/fake_godrive_abi.cpp
)
target_include_directories(gda_ffi_test_double PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  cpp/fake_abi_include
  cpp
)

set(GDA_TEST_SOURCES
  cpp/test_main.cpp            # sole provider of doctest's main() -- see that file
  cpp/test_tool_dispatcher.cpp
  cpp/test_ffi_bridge.cpp
  cpp/test_cli_formatting.cpp
  # test_cli_formatting.cpp only needs FormatFileTable() from commands.cpp,
  # not CLI11 or main.cpp's wiring -- gdrive-agent is an executable with no
  # intermediate library for src/cli/*, so the only way for gda_tests to
  # see FormatFileTable's definition is to compile this source file again
  # here (mirrors the gda_ffi_test_double pattern above).
  ${CMAKE_SOURCE_DIR}/src/cli/commands.cpp
)
if(GDA_ENABLE_MCP_SERVER)
  list(APPEND GDA_TEST_SOURCES cpp/test_mcp_jsonrpc.cpp)
endif()

add_executable(gda_tests ${GDA_TEST_SOURCES})
target_link_libraries(gda_tests PRIVATE
  gda_tools             # ToolDispatcher -- no godrive dependency at all
  gda_ffi_test_double   # GodriveBridge, linked against the FAKE C ABI only
  doctest::doctest
)
if(GDA_ENABLE_MCP_SERVER)
  target_link_libraries(gda_tests PRIVATE gda_mcp)
endif()
target_compile_definitions(gda_tests PRIVATE
  GDA_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/cpp/fixtures")

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(gda_tests)
