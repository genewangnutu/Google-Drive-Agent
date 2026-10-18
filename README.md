# Google Drive Agent

A self-contained native Linux command-line tool: a C++ shell (CLI /
argument parsing / optional MCP Server Facade) directly linked against
Google's official Go Drive client library (`google.golang.org/api/drive/v3`),
compiled via `cgo` into a static archive and linked into the **same**
executable. No Node.js / Python / external MCP server process is required
at runtime, and there is no external package or service dependency at
runtime. It provides the same operations as the Google Drive MCP tools
available in a Claude session: search, list recent, read, download, get
metadata, get permissions, create, update, copy, share, trash.

Design background: see [`Doc/Google-Drive-Agent-Plan.md`](Doc/Google-Drive-Agent-Plan.md)
(feasibility study + software architecture + project architecture) and
[`Doc/Module-Breakdown.md`](Doc/Module-Breakdown.md) (the 5-module split and
which files belong to which module). The exact C ABI between the Go core
(`godrive/`) and the C++ shell is specified in
[`Doc/FFI-Contract.md`](Doc/FFI-Contract.md).

## 1. Requirements (build-time only — **not** required at runtime)

- Linux (x86_64 / aarch64), any glibc-based distro
- CMake >= 3.20
- A C++20 compiler (gcc >= 11 or clang >= 14)
- Go toolchain >= 1.22 (used to compile `godrive/` into a static archive)
- A Google Cloud project with the Drive API enabled, and an OAuth 2.0
  "Desktop app" Client ID (see [section 3](#3-first-time-setup-google-cloud-oauth-client))

## 2. Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build
```

If `go` isn't on `PATH`, point CMake at it directly:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DGO_EXECUTABLE=/path/to/go
```

### Running the unit tests

Unit tests are on by default (`GDA_BUILD_TESTS=ON`). The C++ side (doctest)
and the Go side (`go test`, for `godrive/`) both run under one `ctest`
invocation — see [`Doc/Google-Drive-Agent-Plan.md` §8](Doc/Google-Drive-Agent-Plan.md)
for the full testing strategy, and [`Doc/Module-Breakdown.md`](Doc/Module-Breakdown.md)
for where each module's tests/fixtures live.

```sh
cmake -B build -DGDA_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The C++ tests (`gda_tests`) do **not** need a Go toolchain or a real Google
account to run — `ToolDispatcher` is tested against a fake `IGodriveBridge`,
and `GodriveBridge` itself is tested against a hand-written fake C ABI
(`tests/cpp/fake_godrive_abi.*`), not the real `godrive` archive. Only the
`godrive_go_tests` CTest entry needs the Go toolchain, and it doesn't need a
real account either (it mocks the Drive API's HTTP layer — see
`godrive/internal/testutil/fixture_transport.go`).

### Build-time only, no network required

`godrive/vendor/` already contains the real source of every Go dependency
(`google.golang.org/api`, `golang.org/x/oauth2`, `github.com/stretchr/testify`)
committed into this repo. The build forces `-mod=vendor GOPROXY=off`, so
compiling `godrive/` needs no network access — "download it into the
project, compile it into the binary at build time" rather than relying on
anything fetched from the internet at build *or* run time. (The C++-side
header-only dependencies — nlohmann/json, CLI11, and optionally cpp-httplib
— are fetched once at CMake configure time via `FetchContent`; only the Go
side is pre-vendored into the repo today.)

## 3. First-time setup (Google Cloud OAuth client)

1. In [Google Cloud Console](https://console.cloud.google.com/): create or
   select a project, then enable the Drive API.
2. Create an OAuth 2.0 Client ID of type **Desktop app**.
3. Save the client ID/secret to `~/.config/google-drive-agent/client.json`:

   ```json
   {"client_id": "...", "client_secret": "..."}
   ```

## 4. Login

```sh
gdrive-agent login
```

On a headless machine with no browser, use `--device` for the device-code
flow instead:

```sh
gdrive-agent login --device
```

## 5. Usage

| Command | Description |
|---|---|
| `gdrive-agent login [--device]` | OAuth2 login |
| `gdrive-agent logout` | Clear the local token |
| `gdrive-agent search "<query>"` | Search files (MCP-style query DSL, see §6.3 of the plan) |
| `gdrive-agent list-recent [--order-by X] [--limit N]` | List recently used files |
| `gdrive-agent metadata <fileId>` | Get file metadata |
| `gdrive-agent permissions <fileId>` | Get file permissions |
| `gdrive-agent read <fileId> [--with-comments]` | Read file content as text |
| `gdrive-agent download <fileId> [--export-mime-type X] [--revision ID] -o <path>` | Download raw file bytes |
| `gdrive-agent upload <local_path> [--title X] [--parent ID] [--no-convert]` | Create/upload a file |
| `gdrive-agent create-doc --title X --type doc\|sheet\|slide\|folder [--parent ID]` | Create an empty Google-native file |
| `gdrive-agent update <fileId> [--title X] [--parent ID] [--description X]` | Update metadata / move a file |
| `gdrive-agent copy <fileId> [--title X] [--parent ID]` | Copy a file |
| `gdrive-agent share <fileId> --email X --role writer\|commenter\|reader` | Share a file |
| `gdrive-agent trash <fileId>` | Move a file to trash |
| `gdrive-agent serve-mcp [--transport stdio\|http] [--port N]` | Run as an MCP server (optional, see below) |

Examples:

```sh
gdrive-agent search "title contains 'invoice' and starred = true"
gdrive-agent list-recent --limit 20
gdrive-agent download <fileId> -o out.pdf
gdrive-agent upload ./report.pdf --parent <folderId>
gdrive-agent share <fileId> --email alice@example.com --role writer
gdrive-agent trash <fileId>
```

## 6. Optional: run as an MCP server

```sh
gdrive-agent serve-mcp --transport stdio
```

Lets another MCP-compatible host (e.g. a local Claude Desktop install) use
this executable as its own Google Drive MCP server, over `stdio` or
Streamable HTTP (`--transport http --port N`). Build with
`-DGDA_ENABLE_MCP_SERVER=OFF` to leave this out entirely — it's optional,
the CLI commands above are fully self-sufficient without it.

## 7. Security notes

- The OAuth token is stored at `~/.config/google-drive-agent/token.json`
  with file mode `600`. `gdrive-agent logout` only clears this local file;
  revoking the grant for real still requires visiting the "third-party
  access" page of your Google account.
- This tool requests the full `drive` scope (not `drive.file`), because it
  needs to search/manage pre-existing files, not just files it created
  itself. Guard the local token file accordingly — it is equivalent to full
  read/write access to your Drive.

## 8. Third-party code

- `google.golang.org/api` (BSD-3-Clause) and `golang.org/x/oauth2`
  (BSD-3-Clause) are vendored as source under `godrive/vendor/`.
- `github.com/stretchr/testify` (MIT) is vendored the same way, for the
  Go-side unit tests only.
- `nlohmann/json` and `CLI11` (the optional MCP facade also pulls in
  `cpp-httplib`) are fetched at configure time via CMake `FetchContent`.

## 9. License

See [`LICENSE`](LICENSE) (MIT; vendored third-party code keeps its own
license — see `LICENSE` for details).
