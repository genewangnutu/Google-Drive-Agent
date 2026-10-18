#pragma once

#include <string>

namespace gda {

// Pure interface over the godrive (Go cgo c-archive) FFI boundary.
//
// Every method takes/returns a JSON string using the envelope defined in
// Doc/FFI-Contract.md:
//   {"ok":true,"data":{...}}
//   {"ok":false,"error":{"code":...,"http_status":...,"message":...,"retryable":...}}
//
// Implementations must never throw for expected Drive/network errors --
// those are reported inside the JSON envelope instead. This interface
// exists so ToolDispatcher (module 3) can be unit-tested against a
// FakeGodriveBridge (see tests/cpp/) without building or linking godrive
// at all, and without a real Google account.
class IGodriveBridge {
 public:
  virtual ~IGodriveBridge() = default;

  virtual std::string Login(const std::string& argsJson) = 0;
  virtual std::string Logout(const std::string& argsJson) = 0;
  virtual std::string SearchFiles(const std::string& argsJson) = 0;
  virtual std::string ListRecentFiles(const std::string& argsJson) = 0;
  virtual std::string GetFileMetadata(const std::string& argsJson) = 0;
  virtual std::string GetFilePermissions(const std::string& argsJson) = 0;
  virtual std::string ReadFileContent(const std::string& argsJson) = 0;
  virtual std::string DownloadFileContent(const std::string& argsJson) = 0;
  virtual std::string CreateFile(const std::string& argsJson) = 0;
  virtual std::string UpdateFile(const std::string& argsJson) = 0;
  virtual std::string CopyFile(const std::string& argsJson) = 0;
  virtual std::string ShareFile(const std::string& argsJson) = 0;
  virtual std::string TrashFile(const std::string& argsJson) = 0;
};

// Real implementation: calls the cgo-exported gda_* functions declared in
// the generated libgodrive.h (built by cmake/GoDriveCore.cmake from
// godrive/), and owns the returned string's lifecycle (every gda_* call
// result is freed via gda_free_string() after being copied into a
// std::string). Implemented in src/ffi/godrive_bridge.cpp -- owned by the
// FFI Bridge module (module 2). This class does NOT interpret the
// "ok"/"error" envelope; it passes the JSON through unchanged. That
// interpretation is ToolDispatcher's job (module 3), via ToolError below.
class GodriveBridge final : public IGodriveBridge {
 public:
  std::string Login(const std::string& argsJson) override;
  std::string Logout(const std::string& argsJson) override;
  std::string SearchFiles(const std::string& argsJson) override;
  std::string ListRecentFiles(const std::string& argsJson) override;
  std::string GetFileMetadata(const std::string& argsJson) override;
  std::string GetFilePermissions(const std::string& argsJson) override;
  std::string ReadFileContent(const std::string& argsJson) override;
  std::string DownloadFileContent(const std::string& argsJson) override;
  std::string CreateFile(const std::string& argsJson) override;
  std::string UpdateFile(const std::string& argsJson) override;
  std::string CopyFile(const std::string& argsJson) override;
  std::string ShareFile(const std::string& argsJson) override;
  std::string TrashFile(const std::string& argsJson) override;
};

}  // namespace gda
