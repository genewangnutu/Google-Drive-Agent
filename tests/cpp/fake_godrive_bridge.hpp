#pragma once

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "gda/ffi/godrive_bridge.hpp"

namespace gda {

// Test-only IGodriveBridge: every method returns whatever was last loaded
// via LoadFixture()/SetNextResponse(), regardless of which method was
// called or what args it was given. That is enough for ToolDispatcher
// tests, which exercise one dispatcher call per test case. lastArgsJson_ is
// recorded so tests can also assert on the defaulting/validation that
// ToolDispatcher is responsible for before it ever reaches the bridge.
class FakeGodriveBridge final : public IGodriveBridge {
 public:
  void SetNextResponse(std::string json) { nextResponse_ = std::move(json); }

  void LoadFixture(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
      throw std::runtime_error("FakeGodriveBridge: cannot open fixture " + path);
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    nextResponse_ = buffer.str();
  }

  const std::string& LastArgsJson() const { return lastArgsJson_; }

  std::string Login(const std::string& argsJson) override { return Record(argsJson); }
  std::string Logout(const std::string& argsJson) override { return Record(argsJson); }
  std::string SearchFiles(const std::string& argsJson) override { return Record(argsJson); }
  std::string ListRecentFiles(const std::string& argsJson) override { return Record(argsJson); }
  std::string GetFileMetadata(const std::string& argsJson) override { return Record(argsJson); }
  std::string GetFilePermissions(const std::string& argsJson) override { return Record(argsJson); }
  std::string ReadFileContent(const std::string& argsJson) override { return Record(argsJson); }
  std::string DownloadFileContent(const std::string& argsJson) override { return Record(argsJson); }
  std::string CreateFile(const std::string& argsJson) override { return Record(argsJson); }
  std::string UpdateFile(const std::string& argsJson) override { return Record(argsJson); }
  std::string CopyFile(const std::string& argsJson) override { return Record(argsJson); }
  std::string ShareFile(const std::string& argsJson) override { return Record(argsJson); }
  std::string TrashFile(const std::string& argsJson) override { return Record(argsJson); }

 private:
  std::string Record(const std::string& argsJson) {
    lastArgsJson_ = argsJson;
    return nextResponse_;
  }

  std::string nextResponse_;
  std::string lastArgsJson_;
};

}  // namespace gda
