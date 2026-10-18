#pragma once

#include <string>

#include "gda/tools/tool_dispatcher.hpp"
#include "nlohmann/json.hpp"

namespace gda::cli {

// Pure, directly-testable formatter: renders a JSON array of file objects
// (each with at least "id"/"name"/"mimeType") as an aligned plain-text
// table. No I/O, no ToolDispatcher dependency -- see
// tests/cpp/test_cli_formatting.cpp, which calls this directly against a
// canned fixture and compares byte-for-byte against
// tests/cpp/fixtures/cli_golden/search_table_output.txt.
std::string FormatFileTable(const nlohmann::json& filesArray);

// One function per CLI subcommand (Plan §9). Each formats the
// ToolDispatcher's JSON result into human-readable stdout output and
// writes it via std::cout. All argument validation beyond what CLI11
// itself enforces (e.g. required flags) is left to ToolDispatcher /
// godrive, which report it as a ToolError for main() to catch.
void CmdLogin(gda::ToolDispatcher& dispatcher, bool device);
void CmdLogout(gda::ToolDispatcher& dispatcher);
void CmdSearch(gda::ToolDispatcher& dispatcher, const std::string& query);
void CmdListRecent(gda::ToolDispatcher& dispatcher, const std::string& orderBy, int limit);
void CmdMetadata(gda::ToolDispatcher& dispatcher, const std::string& fileId);
void CmdPermissions(gda::ToolDispatcher& dispatcher, const std::string& fileId);
void CmdRead(gda::ToolDispatcher& dispatcher, const std::string& fileId, bool withComments);
void CmdDownload(gda::ToolDispatcher& dispatcher, const std::string& fileId,
                  const std::string& exportMimeType, const std::string& revisionId,
                  const std::string& outPath);
void CmdUpload(gda::ToolDispatcher& dispatcher, const std::string& localPath,
               const std::string& title, const std::string& parentId, bool noConvert);
void CmdCreateDoc(gda::ToolDispatcher& dispatcher, const std::string& title,
                   const std::string& type, const std::string& parentId);
void CmdUpdate(gda::ToolDispatcher& dispatcher, const std::string& fileId,
               const std::string& title, const std::string& parentId,
               const std::string& description);
void CmdCopy(gda::ToolDispatcher& dispatcher, const std::string& fileId,
             const std::string& title, const std::string& parentId);
void CmdShare(gda::ToolDispatcher& dispatcher, const std::string& fileId,
              const std::string& email, const std::string& role);
void CmdTrash(gda::ToolDispatcher& dispatcher, const std::string& fileId);

}  // namespace gda::cli
