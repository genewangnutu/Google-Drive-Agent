cmake_minimum_required(VERSION 3.20)
project(google-drive-agent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(GDA_BUILD_TESTS "Build unit tests (doctest for C++, go test for godrive)" ON)
option(GDA_ENABLE_MCP_SERVER "Build the optional MCP Server Facade (stdio / Streamable HTTP)" ON)

include(FetchContent)

FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE)
FetchContent_Declare(cli11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG        v2.4.2
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(nlohmann_json cli11)

if(GDA_ENABLE_MCP_SERVER)
  # The Streamable HTTP transport only ever serves plain HTTP on
  # 127.0.0.1 (Plan §6.6) -- it needs neither TLS nor gzip. cpp-httplib
  # auto-links system OpenSSL/zlib if it finds them at configure time
  # (HTTPLIB_USE_*_IF_AVAILABLE default ON), which would make the final
  # binary dynamically depend on libssl/libcrypto/libz -- exactly the
  # external runtime dependency this whole project exists to avoid. Force
  # both off before pulling httplib in.
  set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
  set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
  set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.15.3
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(httplib)
endif()

# ---- godrive: Google's official Go Drive SDK, built as a cgo c-archive ----
# Defines the imported target `godrive` + ${GODRIVE_INCLUDE_DIR}.
# See Doc/FFI-Contract.md for the C ABI this archive exposes.
include(cmake/GoDriveCore.cmake)

# ---- gda_tools: Tool Dispatcher. Pure C++, depends only on the
# IGodriveBridge *interface* (a header) -- never on godrive/cgo directly.
# This is what lets it (and anything testing it) build without a Go
# toolchain or the real c-archive. See tests/CMakeLists.txt.
add_library(gda_tools src/tools/tool_dispatcher.cpp)
target_include_directories(gda_tools PUBLIC include)
target_link_libraries(gda_tools PUBLIC nlohmann_json::nlohmann_json)

# ---- gda_ffi: the real FFI Bridge. The ONLY production target that
# actually links against the godrive c-archive.
add_library(gda_ffi src/ffi/godrive_bridge.cpp)
target_include_directories(gda_ffi PUBLIC include)
target_include_directories(gda_ffi PRIVATE ${GODRIVE_INCLUDE_DIR})
target_link_libraries(gda_ffi PUBLIC godrive pthread dl)

# ---- gda_mcp: optional MCP Server Facade (stdio / Streamable HTTP) ----
# Depends on gda_tools (ToolDispatcher) only -- never touches godrive/cgo.
if(GDA_ENABLE_MCP_SERVER)
  add_library(gda_mcp
    src/mcp/jsonrpc.cpp
    src/mcp/transport_stdio.cpp
    src/mcp/transport_http.cpp
  )
  target_include_directories(gda_mcp PUBLIC include)
  target_link_libraries(gda_mcp PUBLIC gda_tools httplib::httplib)
endif()

# ---- gdrive-agent: CLI frontend / final executable ----
add_executable(gdrive-agent src/cli/main.cpp src/cli/commands.cpp)
target_link_libraries(gdrive-agent PRIVATE
  gda_tools gda_ffi CLI11::CLI11
  $<$<BOOL:${GDA_ENABLE_MCP_SERVER}>:gda_mcp>
)
if(GDA_ENABLE_MCP_SERVER)
  target_compile_definitions(gdrive-agent PRIVATE GDA_WITH_MCP_SERVER=1)
endif()

if(GDA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)          # registers the gda_tests (doctest) CTest target
  add_test(
    NAME godrive_go_tests          # registers `go test ./...` as a CTest target
    COMMAND ${GO_EXECUTABLE} test -mod=vendor ./...
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/godrive
  )
  set_tests_properties(godrive_go_tests PROPERTIES ENVIRONMENT "GOFLAGS=-mod=vendor;GOPROXY=off")
endif()

install(TARGETS gdrive-agent RUNTIME DESTINATION bin)
