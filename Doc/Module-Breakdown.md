# 模組劃分與 Sub-agent 分工

對應 `Doc/task.md` 的 Task 1~3。方案 = 規劃書 `Doc/Google-Drive-Agent-Plan.md` 第 4.4 節的方案 C
(Go + 官方 `google.golang.org/api/drive/v3`,cgo c-archive 連結進 C++ 執行檔)。

## Task 1:採用方案 C,專案分幾個 module?

**5 個 module**,直接對應規劃書第 6.1 節分層架構圖裡的 5 個方框,不多不少:

| # | Module | 語言 | 職責 |
|---|---|---|---|
| 1 | godrive | Go(cgo c-archive) | Drive API 存取、OAuth、token 持久化、query DSL 翻譯 |
| 2 | FFI Bridge | C++ | 包 cgo 產生的 C header,實作 `IGodriveBridge` |
| 3 | Tool Dispatcher | C++ | 11 個工具 + login/logout 的驗證/轉換邏輯,丟 `ToolError` |
| 4 | CLI Frontend | C++ | 參數解析、指令分派、輸出格式化 |
| 5 | MCP Server Facade(選配) | C++ | JSON-RPC 2.0,stdio / Streamable HTTP transport |

**不算 module、由主 agent 直接維護**的是建置系統本身(根目錄 `CMakeLists.txt`、
`cmake/GoDriveCore.cmake`、`tests/CMakeLists.txt`)——這是跨模組共用的基礎設施,交給任何一個
sub-agent都會造成「5 個 agent 同時改同一批檔案」的衝突風險,所以由主 agent 預先寫好、事後統一維護。

## Task 2:Sub-agent 指派

| Module | Sub-agent 名稱 | 負責檔案(只能動這些) |
|---|---|---|
| 1. godrive | Agent-Godrive | `godrive/**`(整個目錄都是它的) |
| 2. FFI Bridge | Agent-FFIBridge | `src/ffi/godrive_bridge.cpp`、`tests/cpp/test_ffi_bridge.cpp` |
| 3. Tool Dispatcher | Agent-ToolDispatcher | `src/tools/tool_dispatcher.cpp`、`tests/cpp/test_tool_dispatcher.cpp`、`tests/cpp/fixtures/ffi_responses/*.json` |
| 4. CLI Frontend | Agent-CLI | `src/cli/main.cpp`、`src/cli/commands.cpp`、`tests/cpp/test_cli_formatting.cpp`、`tests/cpp/fixtures/cli_golden/*` |
| 5. MCP Facade | Agent-MCP | `include/gda/mcp/*.hpp`、`src/mcp/*.cpp`、`tests/cpp/test_mcp_jsonrpc.cpp` |

## Task 3:整合邊界(主 agent 負責仲裁)

共用契約由主 agent 預先寫好,所有 sub-agent 只能**實作/消費**,不能修改:

- `Doc/FFI-Contract.md` — godrive ↔ FFI Bridge 的 C ABI 契約(14 個 `//export` 函式的簽章與
  JSON 形狀)
- `include/gda/ffi/godrive_bridge.hpp` — `IGodriveBridge`(介面)/`GodriveBridge`(宣告)
- `include/gda/tools/tool_dispatcher.hpp` — `ToolDispatcher`/`ToolError`(宣告)
- 根目錄 `CMakeLists.txt` / `cmake/GoDriveCore.cmake` / `tests/CMakeLists.txt` — target 名稱已固定:
  `godrive`、`gda_core`、`gda_mcp`、`gdrive-agent`、`gda_tests`

任何 sub-agent 若發現契約不夠用(例如少一個欄位、少一個函式),**回報整合負責人(主 agent)**
裁決後統一修改共用契約檔案,不要自己在自己的模組裡繞過去,否則會出現「各自測試都過、整合對不起來」
的狀況。

主 agent 在全部 sub-agent 回報後執行:跨模組 code review、建置驗證(CMake + Go,經由本機
WSL Ubuntu-24.04,因為 Windows 原生沒有 cmake/g++/go)、修正整合問題、跑 `ctest`,並回寫
`Doc/task.md` 的勾選狀態。

## 環境備忘(每個 sub-agent 都需要知道)

本機是 Windows,原生沒有 `cmake`/`g++`/`go`。可用的是 WSL 裡的 `Ubuntu-24.04` distro
(`cmake 3.28.3`、`g++ 13.3.0`;Go 1.27.2 已手動裝在 `/home/user/go-sdk/bin/go`,刻意**不**寫入
`~/.profile`/`~/.bashrc`,要用時用 `PATH=/home/user/go-sdk/bin:$PATH` 內嵌指定,或直接打完整路徑)。
`D:\Google-Drive-Agent` 在 WSL 裡對應 `/mnt/d/Google-Drive-Agent`(同一份檔案系統)。執行 Linux
指令一律包成一整個字串丟給 `wsl.exe -d Ubuntu-24.04 -- bash -lc "..."`,不要把 `/...` 開頭的路徑
當作獨立參數直接傳給 `wsl.exe`——Git Bash 的 MSYS 路徑轉換會把它誤判成 Windows 路徑而弄壞
(這份專案在設置時就踩到過這個坑)。
