#include <cstdlib>
#include <iostream>

#include "CLI/CLI.hpp"
#include "gda/cli/commands.hpp"
#include "gda/ffi/godrive_bridge.hpp"
#include "gda/tools/tool_dispatcher.hpp"

#ifdef GDA_WITH_MCP_SERVER
#include "gda/mcp/server.hpp"
#endif

namespace {

// Exit code convention (no POSIX/BSD standard covers this, so we pick our
// own and document it here): 1 for a non-retryable ToolError, 75 for a
// retryable one. 75 is borrowed from BSD sysexits.h's EX_TEMPFAIL ("temporary
// failure; user is invited to retry"), which matches ToolError::retryable()
// (rate limiting / transient network errors) well enough to reuse rather
// than invent an unrelated number.
constexpr int kExitError = 1;
constexpr int kExitRetryable = 75;

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"gdrive-agent -- self-contained native Google Drive CLI"};
  app.require_subcommand(1);

  gda::GodriveBridge bridge;
  gda::ToolDispatcher dispatcher(bridge);

  // login
  bool loginDevice = false;
  auto* login = app.add_subcommand("login", "OAuth2 login");
  login->add_flag("--device", loginDevice, "Use headless device-code flow");
  login->callback([&]() { gda::cli::CmdLogin(dispatcher, loginDevice); });

  // logout
  auto* logout = app.add_subcommand("logout", "Clear the local token");
  logout->callback([&]() { gda::cli::CmdLogout(dispatcher); });

  // search
  std::string searchQuery;
  auto* search = app.add_subcommand("search", "Search files (search_files)");
  search->add_option("query", searchQuery, "MCP query DSL string")->required();
  search->callback([&]() { gda::cli::CmdSearch(dispatcher, searchQuery); });

  // list-recent
  std::string orderBy = "recency";
  int limit = 20;
  auto* listRecent = app.add_subcommand("list-recent", "List recently used files");
  listRecent->add_option("--order-by", orderBy, "recency|lastModified|lastModifiedByMe")
      ->default_val("recency");
  listRecent->add_option("--limit", limit, "Max results")->default_val(20);
  listRecent->callback([&]() { gda::cli::CmdListRecent(dispatcher, orderBy, limit); });

  // metadata
  std::string metadataFileId;
  auto* metadata = app.add_subcommand("metadata", "Get file metadata");
  metadata->add_option("fileId", metadataFileId)->required();
  metadata->callback([&]() { gda::cli::CmdMetadata(dispatcher, metadataFileId); });

  // permissions
  std::string permissionsFileId;
  auto* permissions = app.add_subcommand("permissions", "Get file permissions");
  permissions->add_option("fileId", permissionsFileId)->required();
  permissions->callback([&]() { gda::cli::CmdPermissions(dispatcher, permissionsFileId); });

  // read
  std::string readFileId;
  bool withComments = false;
  auto* read = app.add_subcommand("read", "Read file content as text");
  read->add_option("fileId", readFileId)->required();
  read->add_flag("--with-comments", withComments);
  read->callback([&]() { gda::cli::CmdRead(dispatcher, readFileId, withComments); });

  // download
  std::string downloadFileId, exportMimeType, revisionId, downloadOut;
  auto* download = app.add_subcommand("download", "Download raw file bytes");
  download->add_option("fileId", downloadFileId)->required();
  download->add_option("--export-mime-type", exportMimeType);
  download->add_option("--revision", revisionId);
  download->add_option("-o,--output", downloadOut, "Local output path")->required();
  download->callback([&]() {
    gda::cli::CmdDownload(dispatcher, downloadFileId, exportMimeType, revisionId, downloadOut);
  });

  // upload
  std::string uploadLocalPath, uploadTitle, uploadParent;
  bool uploadNoConvert = false;
  auto* upload = app.add_subcommand("upload", "Upload a local file (create_file)");
  upload->add_option("local_path", uploadLocalPath)->required();
  upload->add_option("--title", uploadTitle);
  upload->add_option("--parent", uploadParent);
  upload->add_flag("--no-convert", uploadNoConvert, "Disable conversion to a Google-native type");
  upload->callback([&]() {
    gda::cli::CmdUpload(dispatcher, uploadLocalPath, uploadTitle, uploadParent, uploadNoConvert);
  });

  // create-doc
  std::string createDocTitle, createDocType, createDocParent;
  auto* createDoc = app.add_subcommand("create-doc", "Create an empty Google-native file");
  createDoc->add_option("--title", createDocTitle)->required();
  createDoc->add_option("--type", createDocType, "doc|sheet|slide|folder")
      ->required()
      ->check(CLI::IsMember({"doc", "sheet", "slide", "folder"}));
  createDoc->add_option("--parent", createDocParent);
  createDoc->callback([&]() {
    gda::cli::CmdCreateDoc(dispatcher, createDocTitle, createDocType, createDocParent);
  });

  // update
  std::string updateFileId, updateTitle, updateParent, updateDescription;
  auto* update = app.add_subcommand("update", "Update file metadata / move");
  update->add_option("fileId", updateFileId)->required();
  update->add_option("--title", updateTitle);
  update->add_option("--parent", updateParent);
  update->add_option("--description", updateDescription);
  update->callback([&]() {
    gda::cli::CmdUpdate(dispatcher, updateFileId, updateTitle, updateParent, updateDescription);
  });

  // copy
  std::string copyFileId, copyTitle, copyParent;
  auto* copy = app.add_subcommand("copy", "Copy a file");
  copy->add_option("fileId", copyFileId)->required();
  copy->add_option("--title", copyTitle);
  copy->add_option("--parent", copyParent);
  copy->callback([&]() { gda::cli::CmdCopy(dispatcher, copyFileId, copyTitle, copyParent); });

  // share
  std::string shareFileId, shareEmail, shareRole;
  auto* share = app.add_subcommand("share", "Share a file with an email address");
  share->add_option("fileId", shareFileId)->required();
  share->add_option("--email", shareEmail)->required();
  share->add_option("--role", shareRole, "writer|commenter|reader")
      ->required()
      ->check(CLI::IsMember({"writer", "commenter", "reader"}));
  share->callback([&]() { gda::cli::CmdShare(dispatcher, shareFileId, shareEmail, shareRole); });

  // trash
  std::string trashFileId;
  auto* trash = app.add_subcommand("trash", "Move a file to the trash");
  trash->add_option("fileId", trashFileId)->required();
  trash->callback([&]() { gda::cli::CmdTrash(dispatcher, trashFileId); });

  // serve-mcp
  std::string transport = "stdio";
  int port = 8080;
  auto* serveMcp = app.add_subcommand("serve-mcp", "Run as a local MCP server");
  serveMcp->add_option("--transport", transport, "stdio|http")
      ->default_val("stdio")
      ->check(CLI::IsMember({"stdio", "http"}));
  serveMcp->add_option("--port", port)->default_val(8080);
  serveMcp->callback([&]() {
#ifdef GDA_WITH_MCP_SERVER
    if (transport == "http") {
      gda::mcp::RunHttpServer(dispatcher, port);
    } else {
      gda::mcp::RunStdioServer(dispatcher);
    }
#else
    std::cerr << "serve-mcp: this build was compiled without the optional MCP "
                 "Server Facade (GDA_ENABLE_MCP_SERVER=OFF)\n";
    std::exit(kExitError);
#endif
  });

  // Not using the CLI11_PARSE() convenience macro: it only catches
  // CLI::ParseError, but app.parse() also runs the matched subcommand's
  // callback synchronously, and that callback can throw gda::ToolError
  // (via ToolDispatcher). Both need to be caught here in one place.
  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    return app.exit(e);
  } catch (const gda::ToolError& e) {
    std::cerr << e.what() << "\n";
    // See kExitError/kExitRetryable above for why retryable errors get a
    // distinct exit code instead of sharing exit code 1.
    return e.retryable() ? kExitRetryable : kExitError;
  } catch (const std::exception& e) {
    // Local, non-ToolError failures (bad --type value caught before the
    // dispatcher call, local file I/O errors in upload/download, etc.).
    std::cerr << e.what() << "\n";
    return kExitError;
  }

  return 0;
}
