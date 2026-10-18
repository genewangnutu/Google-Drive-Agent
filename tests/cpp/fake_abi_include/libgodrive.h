#pragma once

// Self-verification-only stand-in for the real cgo-generated libgodrive.h.
// godrive_bridge.cpp has a hardcoded `#include "libgodrive.h"` (the exact
// filename cgo produces); this file lets that line resolve to the fake ABI
// in fake_godrive_abi.h when this directory (tests/cpp/fake_abi_include/)
// is added to the include path alongside tests/cpp/ itself. Used ONLY for
// the FFI Bridge module's own standalone compile-and-run check outside the
// real CMake build -- never consumed by tests/CMakeLists.txt or the real
// build, which gets the genuine generated header from ${GODRIVE_INCLUDE_DIR}.
#include "fake_godrive_abi.h"
