#include "gda/cli/commands.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace gda::cli {

namespace {

using nlohmann::json;

// Returns `s` as a JSON string, or JSON null when `s` is empty -- every
// optional CLI11 flag in this file defaults to "" when not given, and the
// FFI contract (Doc/FFI-Contract.md §3) represents "not provided" as
// JSON null for these fields.
json OptionalString(const std::string& s) {
  if (s.empty()) return nullptr;
  return s;
}

// --- minimal base64, used only by download/upload (no external dep) ---

std::string Base64Encode(const std::string& data) {
  static const char kTable[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((data.size() + 2) / 3) * 4);
  size_t i = 0;
  for (; i + 2 < data.size(); i += 3) {
    unsigned val = (static_cast<unsigned char>(data[i]) << 16) |
                   (static_cast<unsigned char>(data[i + 1]) << 8) |
                   static_cast<unsigned char>(data[i + 2]);
    out.push_back(kTable[(val >> 18) & 0x3F]);
    out.push_back(kTable[(val >> 12) & 0x3F]);
    out.push_back(kTable[(val >> 6) & 0x3F]);
    out.push_back(kTable[val & 0x3F]);
  }
  size_t remaining = data.size() - i;
  if (remaining == 1) {
    unsigned val = static_cast<unsigned char>(data[i]) << 16;
    out.push_back(kTable[(val >> 18) & 0x3F]);
    out.push_back(kTable[(val >> 12) & 0x3F]);
    out.push_back('=');
    out.push_back('=');
  } else if (remaining == 2) {
    unsigned val = (static_cast<unsigned char>(data[i]) << 16) |
                   (static_cast<unsigned char>(data[i + 1]) << 8);
    out.push_back(kTable[(val >> 18) & 0x3F]);
    out.push_back(kTable[(val >> 12) & 0x3F]);
    out.push_back(kTable[(val >> 6) & 0x3F]);
    out.push_back('=');
  }
  return out;
}

std::string Base64Decode(const std::string& data) {
  auto decodeChar = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::string out;
  out.reserve((data.size() / 4) * 3);
  int buffer = 0;
  int bitsCollected = 0;
  for (char c : data) {
    if (c == '=' || c == '\n' || c == '\r') continue;
    int val = decodeChar(c);
    if (val < 0) continue;
    buffer = (buffer << 6) | val;
    bitsCollected += 6;
    if (bitsCollected >= 8) {
      bitsCollected -= 8;
      out.push_back(static_cast<char>((buffer >> bitsCollected) & 0xFF));
    }
  }
  return out;
}

// Google's native type for `create-doc --type X` -- the FFI contract
// (Doc/FFI-Contract.md 3.9) has no dedicated "native type" field for
// gda_create_file, so this reuses "contentMimeType" (with no content
// bytes attached) to tell godrive which empty Google-native file to
// create. Flagged to the integration lead as a cross-module judgment
// call -- verify it matches what godrive/drive_client.go actually does
// with contentMimeType when textContent/base64Content are both null.
std::string NativeMimeTypeForDocType(const std::string& type) {
  if (type == "doc") return "application/vnd.google-apps.document";
  if (type == "sheet") return "application/vnd.google-apps.spreadsheet";
  if (type == "slide") return "application/vnd.google-apps.presentation";
  if (type == "folder") return "application/vnd.google-apps.folder";
  throw std::invalid_argument("create-doc --type must be one of: doc, sheet, slide, folder");
}

std::string ReadFileBytes(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot open local file: " + path);
  }
  std::ostringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

void WriteFileBytes(const std::string& path, const std::string& bytes) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    throw std::runtime_error("cannot open output path for writing: " + path);
  }
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string BasenameOf(const std::string& path) {
  size_t pos = path.find_last_of("/\\");
  return pos == std::string::npos ? path : path.substr(pos + 1);
}

// Prints a <FileObject> (Doc/FFI-Contract.md 3.9's minimal field set) as a
// simple "key: value" block -- used by metadata/update/copy/create-doc.
void PrintFileObject(const json& file) {
  std::cout << "id: " << file.value("id", "") << "\n";
  std::cout << "name: " << file.value("name", "") << "\n";
  std::cout << "mimeType: " << file.value("mimeType", "") << "\n";
  if (file.contains("parents") && file["parents"].is_array()) {
    std::cout << "parents:";
    for (const auto& p : file["parents"]) std::cout << " " << p.get<std::string>();
    std::cout << "\n";
  }
  std::cout << "createdTime: " << file.value("createdTime", "") << "\n";
  std::cout << "modifiedTime: " << file.value("modifiedTime", "") << "\n";
  std::cout << "trashed: " << (file.value("trashed", false) ? "true" : "false") << "\n";
  if (!file.value("webViewLink", std::string()).empty()) {
    std::cout << "webViewLink: " << file.value("webViewLink", "") << "\n";
  }
  if (file.contains("size") && !file["size"].is_null()) {
    std::cout << "size: " << file.value("size", "") << "\n";
  }
}

}  // namespace

std::string FormatFileTable(const nlohmann::json& filesArray) {
  static const std::array<std::string, 3> kHeaders = {"ID", "NAME", "MIMETYPE"};
  constexpr size_t kGutter = 2;

  if (!filesArray.is_array() || filesArray.empty()) {
    return "(no files)\n";
  }

  std::vector<std::array<std::string, 3>> rows;
  rows.reserve(filesArray.size());
  for (const auto& f : filesArray) {
    rows.push_back({f.value("id", ""), f.value("name", ""), f.value("mimeType", "")});
  }

  std::array<size_t, 3> widths = {kHeaders[0].size(), kHeaders[1].size(), kHeaders[2].size()};
  for (const auto& row : rows) {
    for (size_t i = 0; i < 3; ++i) widths[i] = std::max(widths[i], row[i].size());
  }

  auto writeRow = [&](const std::array<std::string, 3>& cols, std::ostringstream& out) {
    std::string line;
    for (size_t i = 0; i < 3; ++i) {
      line += cols[i];
      if (i + 1 < cols.size()) line += std::string(widths[i] - cols[i].size() + kGutter, ' ');
    }
    // Trim trailing spaces (e.g. a row whose last column is empty) so no
    // line in the output carries invisible trailing whitespace.
    while (!line.empty() && line.back() == ' ') line.pop_back();
    out << line << "\n";
  };

  std::ostringstream out;
  writeRow(kHeaders, out);
  for (const auto& row : rows) writeRow(row, out);
  return out.str();
}

void CmdLogin(gda::ToolDispatcher& dispatcher, bool device) {
  json args = {{"mode", device ? "device" : "browser"}};
  json data = json::parse(dispatcher.Login(args.dump()));
  std::cout << "status: " << data.value("status", "") << "\n";
}

void CmdLogout(gda::ToolDispatcher& dispatcher) {
  json data = json::parse(dispatcher.Logout());
  std::cout << "status: " << data.value("status", "") << "\n";
}

void CmdSearch(gda::ToolDispatcher& dispatcher, const std::string& query) {
  json args = {{"query", query}, {"pageSize", 20}, {"pageToken", nullptr}};
  json data = json::parse(dispatcher.SearchFiles(args.dump()));
  std::cout << FormatFileTable(data.value("files", json::array()));
}

void CmdListRecent(gda::ToolDispatcher& dispatcher, const std::string& orderBy, int limit) {
  json args = {{"orderBy", orderBy}, {"pageSize", limit}, {"pageToken", nullptr}};
  json data = json::parse(dispatcher.ListRecentFiles(args.dump()));
  std::cout << FormatFileTable(data.value("files", json::array()));
}

void CmdMetadata(gda::ToolDispatcher& dispatcher, const std::string& fileId) {
  json args = {{"fileId", fileId}};
  json data = json::parse(dispatcher.GetFileMetadata(args.dump()));
  PrintFileObject(data);
}

void CmdPermissions(gda::ToolDispatcher& dispatcher, const std::string& fileId) {
  json args = {{"fileId", fileId}};
  json data = json::parse(dispatcher.GetFilePermissions(args.dump()));
  for (const auto& p : data.value("permissions", json::array())) {
    std::cout << p.value("id", "") << "  " << p.value("type", "") << "  " << p.value("role", "");
    if (p.contains("emailAddress") && !p["emailAddress"].is_null()) {
      std::cout << "  " << p.value("emailAddress", "");
    }
    std::cout << "\n";
  }
}

void CmdRead(gda::ToolDispatcher& dispatcher, const std::string& fileId, bool withComments) {
  json args = {{"fileId", fileId}, {"includeComments", withComments}};
  json data = json::parse(dispatcher.ReadFileContent(args.dump()));
  std::cout << data.value("content", "") << "\n";
  if (withComments && data.contains("comments") && data["comments"].is_array()) {
    std::cout << "\n--- comments ---\n";
    for (const auto& c : data["comments"]) {
      std::cout << "[" << c.value("createdTime", "") << "] " << c.value("author", "") << ": "
                << c.value("content", "") << "\n";
    }
  }
}

void CmdDownload(gda::ToolDispatcher& dispatcher, const std::string& fileId,
                  const std::string& exportMimeType, const std::string& revisionId,
                  const std::string& outPath) {
  json args = {{"fileId", fileId},
               {"exportMimeType", OptionalString(exportMimeType)},
               {"revisionId", OptionalString(revisionId)}};
  json data = json::parse(dispatcher.DownloadFileContent(args.dump()));
  std::string bytes = Base64Decode(data.value("base64Content", ""));
  WriteFileBytes(outPath, bytes);
  std::cout << "downloaded " << bytes.size() << " bytes (" << data.value("mimeType", "")
            << ") to " << outPath << "\n";
}

void CmdUpload(gda::ToolDispatcher& dispatcher, const std::string& localPath,
               const std::string& title, const std::string& parentId, bool noConvert) {
  std::string bytes = ReadFileBytes(localPath);
  std::string effectiveTitle = title.empty() ? BasenameOf(localPath) : title;
  json args = {{"title", effectiveTitle},
               {"parentId", OptionalString(parentId)},
               {"textContent", nullptr},
               {"base64Content", Base64Encode(bytes)},
               {"contentMimeType", "application/octet-stream"},
               {"disableConversionToGoogleType", noConvert}};
  json data = json::parse(dispatcher.CreateFile(args.dump()));
  PrintFileObject(data);
}

void CmdCreateDoc(gda::ToolDispatcher& dispatcher, const std::string& title,
                   const std::string& type, const std::string& parentId) {
  json args = {{"title", title},
               {"parentId", OptionalString(parentId)},
               {"textContent", nullptr},
               {"base64Content", nullptr},
               {"contentMimeType", NativeMimeTypeForDocType(type)},
               {"disableConversionToGoogleType", false}};
  json data = json::parse(dispatcher.CreateFile(args.dump()));
  PrintFileObject(data);
}

void CmdUpdate(gda::ToolDispatcher& dispatcher, const std::string& fileId,
               const std::string& title, const std::string& parentId,
               const std::string& description) {
  json args = {{"fileId", fileId},
               {"title", OptionalString(title)},
               {"parentId", OptionalString(parentId)},
               {"description", OptionalString(description)}};
  json data = json::parse(dispatcher.UpdateFile(args.dump()));
  PrintFileObject(data);
}

void CmdCopy(gda::ToolDispatcher& dispatcher, const std::string& fileId,
             const std::string& title, const std::string& parentId) {
  json args = {{"fileId", fileId}, {"title", OptionalString(title)}, {"parentId", OptionalString(parentId)}};
  json data = json::parse(dispatcher.CopyFile(args.dump()));
  PrintFileObject(data);
}

void CmdShare(gda::ToolDispatcher& dispatcher, const std::string& fileId, const std::string& email,
              const std::string& role) {
  json args = {{"fileId", fileId},
               {"role", role},
               {"emailAddress", OptionalString(email)},
               {"chatSpaceId", nullptr}};
  json data = json::parse(dispatcher.ShareFile(args.dump()));
  std::cout << "id: " << data.value("id", "") << "\n";
  std::cout << "type: " << data.value("type", "") << "\n";
  std::cout << "role: " << data.value("role", "") << "\n";
  if (data.contains("emailAddress") && !data["emailAddress"].is_null()) {
    std::cout << "emailAddress: " << data.value("emailAddress", "") << "\n";
  }
}

void CmdTrash(gda::ToolDispatcher& dispatcher, const std::string& fileId) {
  json args = {{"fileId", fileId}};
  json data = json::parse(dispatcher.TrashFile(args.dump()));
  std::cout << "id: " << data.value("id", "") << "\n";
  std::cout << "trashed: " << (data.value("trashed", false) ? "true" : "false") << "\n";
}

}  // namespace gda::cli
