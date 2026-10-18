# Builds godrive/ (a Go module) into a static c-archive and exposes it to
# the rest of the CMake build as the imported target `godrive`.
#
# - Build-time only requirement: a Go >= 1.22 toolchain on PATH.
# - Deps are vendored into godrive/vendor/ (committed to the repo), and the
#   build is forced to use them (-mod=vendor, GOPROXY=off) so this step
#   itself needs no network access.
# - Output: libgodrive.a (static archive, includes the Go runtime) +
#   libgodrive.h (cgo-generated C header declaring every //export function).
#   Neither file is committed -- both are build products (see .gitignore).
#
# See Doc/FFI-Contract.md for the exact exported function signatures and
# JSON argument/return shapes this archive must implement.

find_program(GO_EXECUTABLE go REQUIRED)

set(GODRIVE_DIR ${CMAKE_SOURCE_DIR}/godrive)
set(GODRIVE_BUILD_DIR ${CMAKE_BINARY_DIR}/godrive)
set(GODRIVE_ARCHIVE ${GODRIVE_BUILD_DIR}/libgodrive.a)
set(GODRIVE_HEADER  ${GODRIVE_BUILD_DIR}/libgodrive.h)

file(GLOB_RECURSE GODRIVE_SOURCES CONFIGURE_DEPENDS ${GODRIVE_DIR}/*.go)

add_custom_command(
  OUTPUT ${GODRIVE_ARCHIVE} ${GODRIVE_HEADER}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GODRIVE_BUILD_DIR}
  COMMAND ${CMAKE_COMMAND} -E env
          CGO_ENABLED=1 GOFLAGS=-mod=vendor GOPROXY=off
          ${GO_EXECUTABLE} build -buildmode=c-archive
          -o ${GODRIVE_ARCHIVE} .
  WORKING_DIRECTORY ${GODRIVE_DIR}
  DEPENDS ${GODRIVE_DIR}/go.mod ${GODRIVE_DIR}/go.sum ${GODRIVE_SOURCES}
  COMMENT "Building godrive core via cgo c-archive (vendored deps, no network at build time)"
  VERBATIM
)
add_custom_target(godrive_core DEPENDS ${GODRIVE_ARCHIVE})

add_library(godrive STATIC IMPORTED GLOBAL)
set_target_properties(godrive PROPERTIES IMPORTED_LOCATION ${GODRIVE_ARCHIVE})
add_dependencies(godrive godrive_core)

# libgodrive.h lands in the build dir next to the archive; anything linking
# against `godrive` (currently just gda_core) needs this on its include path.
set(GODRIVE_INCLUDE_DIR ${GODRIVE_BUILD_DIR} CACHE INTERNAL "")
