# Backblaze B2 後端支援 — 設計規劃書

這份文件規劃如何在**不破壞現有 Google Drive 功能**的前提下,讓這個專案在建置期(build-time)
二選一支援 **Backblaze B2 Cloud Storage** 當作另一個雲端儲存後端,並產出一個獨立命名的執行檔。
對應既有文件:`Doc/Google-Drive-Agent-Plan.md`(原始架構)、`Doc/Module-Breakdown.md`(5-module
劃分)、`Doc/FFI-Contract.md`(godrive 的 FFI 契約,本文件沿用同一套慣例設計 B2 版)。

## 目錄

1. 背景與動機
2. 核心限制複述(使用者的硬性要求)
3. 技術選型:B2 側怎麼接
4. 認證設計(對照 Google OAuth,B2 簡單得多)
5. CMakeLists.txt 設計(`GDA_BACKEND` 開關)
6. 目錄結構與新增/修改檔案清單
7. FFI Contract 草案(B2 版)
8. CLI 指令對照表與語意落差
9. MCP Server Facade 的因應(現有 `gda_mcp` 需要一次小改動)
10. 測試策略
11. 已知限制與風險
12. 實作階段規劃
13. 下一步建議

---

## 1. 背景與動機

Google Drive 版本在實際使用中卡在 OAuth 的一連串問題:Desktop app / TVs and Limited Input
devices 兩種 client type 互不相容、device flow 不支援完整 `drive` scope、consent screen /
test users 的設定門檻。調查過 Dropbox、MEGA、Proton Drive、Sync.com、Filen 之後,
**Backblaze B2** 是 auth 最簡單的選項:純 Application Key(key ID + key)的靜態金鑰,沒有
OAuth、沒有 consent screen、沒有 scope 審核,10GB 免費額度、API 立即可用、免信用卡。

這份規劃書的目標**不是**取代 Google Drive 版本,而是讓使用者可以用 `-DGDA_BACKEND=b2` 建出
一個**獨立的 `b2-agent` 執行檔**,兩者並存、互不影響。

## 2. 核心限制複述(使用者的硬性要求)

> `CMakeLists.txt` 要有 if/else 區分 build Google Drive 還是 Backblaze B2,cmake 指令參數要不
> 同,最終 build 出來的檔名也要不同。

具體化成:

- 新增一個 cache 變數 `GDA_BACKEND`,值為 `google_drive`(預設,維持現有行為不變)或 `b2`。
- `cmake -B build -DGDA_BACKEND=google_drive` → 產出 `gdrive-agent`(現有行為,零改動)。
- `cmake -B build -DGDA_BACKEND=b2` → 產出 `b2-agent`(新)。
- 兩者是**互斥**的單一建置產物(同一次 configure/build 只選一邊),不是同一執行檔裡用 runtime
  旗標切換後端。這個前提大幅簡化了設計:**不需要**在 C++ 側做一個橫跨兩個後端的共用抽象介面
  (例如讓 `ToolDispatcher` 變成可插拔的多後端介面)——每個後端各自是一條獨立的垂直切片
  (Go core → FFI Bridge → Tool Dispatcher → CLI),只有真正語意相通、值得重用的小塊(JSON
  envelope 慣例、MCP 傳輸層機制、CMake 的外部相依 fetch)共用。

## 3. 技術選型:B2 側怎麼接

### 3.1 不要手刻 B2 Native API,改用 AWS SDK for Go v2 打 S3-Compatible API

Backblaze B2 官方文件明確支援、並示範了用 AWS SDK 存取 B2(["Use the AWS SDK for Go with
Backblaze B2"](https://www.backblaze.com/docs/cloud-storage-use-the-aws-sdk-for-go-with-backblaze-b2)),
只需三個設定:

- 自訂 endpoint:`https://s3.<region>.backblazeb2.com`(每個帳號不同,在 Buckets 頁面上看,
  例如 `us-west-004`)
- Region:同上,例如 `us-west-004`
- Path-style addressing:B2 的 S3-compatible API 要求 path-style(AWS SDK v2 裡是
  `s3.Options{ UsePathStyle: true }`),不是 virtual-hosted-style

這完全符合規劃書一路以來「重用現成函式庫、vendor 進專案、不要重新發明」的原則(`godrive/` 用
Google 官方 SDK 的理由一樣)。`github.com/aws/aws-sdk-go-v2` 系列是 AWS 官方維護、Apache-2.0
授權、可以 `go mod vendor` 整包進 repo,跟 `godrive/vendor/` 的作法一致。

**不採用** B2 Native API(`b2_authorize_account`/`b2_upload_file`/... 這組 Backblaze 自家
REST API)——沒有官方 Go SDK,要自己刻 HTTP 呼叫、自己管 `authorizationToken` 過期重新授權,
等於重新發明 AWS SDK 已經做好的事。

### 3.2 需要 vendor 的 Go 套件(初步估計)

- `github.com/aws/aws-sdk-go-v2`
- `github.com/aws/aws-sdk-go-v2/config`
- `github.com/aws/aws-sdk-go-v2/credentials`
- `github.com/aws/aws-sdk-go-v2/service/s3`
- `github.com/aws/aws-sdk-go-v2/feature/s3/manager`(分段上傳大檔用,可選,視檔案大小需求決定
  是否第一版就要)
- `github.com/aws/smithy-go`(上述套件的 transitive 依賴)

實作時第一步先 `go get` 到暫存環境確認完整相依樹,再 `go mod vendor` 進 `b2core/vendor/`,
跟 `godrive/vendor/` 的流程完全一樣(見 README.md §2「Build-time only, no network required」)。

## 4. 認證設計(對照 Google OAuth,B2 簡單得多)

沒有 login/logout 指令,沒有 token 刷新,沒有瀏覽器/device flow。只有一個**靜態憑證檔**：

```
~/.config/b2-agent/credentials.json
```

```json
{
  "application_key_id": "0012345678abcdef0000000001",
  "application_key": "K001abcdefghijklmnopqrstuvwxyz1234",
  "bucket_name": "my-personal-bucket",
  "endpoint": "https://s3.us-west-004.backblazeb2.com",
  "region": "us-west-004"
}
```

對照 `client.json.example`,新增一個 `b2-credentials.json.example`(見第 6 節)。

**兩個使用者設定時容易踩到的坑,必須寫進 README**(查證過官方文件):

1. **Master Application Key 不能用在 S3-Compatible API**——必須在 B2 後台另外建一個「限定某
   bucket」的 Application Key,不能偷懶直接複製帳號主 key。
2. 如果這個 key 限定了單一 bucket,**必須額外勾選 `listAllBucketNames` 權限**,否則 AWS SDK
   做某些初始化呼叫(例如驗證 bucket 存在)會失敗。

`b2-agent` 不需要 `login`/`logout` 指令;取而代之是一個 `status`(或 `whoami`)指令,單純呼叫
一次 `HeadBucket`/`ListObjectsV2(MaxKeys=1)` 驗證憑證跟 bucket 設定正確,對應舊版 OAuth 流程
裡「確認登入成功」的角色,但不涉及任何互動式授權。

## 5. CMakeLists.txt 設計(`GDA_BACKEND` 開關)

```cmake
cmake_minimum_required(VERSION 3.20)
project(google-drive-agent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(GDA_BUILD_TESTS "Build unit tests (doctest for C++, go test for the selected backend)" ON)
option(GDA_ENABLE_MCP_SERVER "Build the optional MCP Server Facade (stdio / Streamable HTTP)" ON)

set(GDA_BACKEND "google_drive" CACHE STRING "Which cloud backend to build: google_drive | b2")
set_property(CACHE GDA_BACKEND PROPERTY STRINGS google_drive b2)
if(NOT GDA_BACKEND STREQUAL "google_drive" AND NOT GDA_BACKEND STREQUAL "b2")
  message(FATAL_ERROR "GDA_BACKEND must be 'google_drive' or 'b2', got: '${GDA_BACKEND}'")
endif()

include(FetchContent)
# ... nlohmann_json / cli11 / (可選) httplib 的 FetchContent_Declare,跟現在完全一樣,
#     兩個後端共用,不受 GDA_BACKEND 影響 ...

if(GDA_BACKEND STREQUAL "google_drive")
  set(GDA_EXECUTABLE_NAME gdrive-agent)

  include(cmake/GoDriveCore.cmake)        # 現有,原封不動:定義 `godrive` target
  add_library(gda_tools src/tools/tool_dispatcher.cpp)
  target_include_directories(gda_tools PUBLIC include)
  target_link_libraries(gda_tools PUBLIC nlohmann_json::nlohmann_json)

  add_library(gda_ffi src/ffi/godrive_bridge.cpp)
  target_include_directories(gda_ffi PUBLIC include)
  target_include_directories(gda_ffi PRIVATE ${GODRIVE_INCLUDE_DIR})
  target_link_libraries(gda_ffi PUBLIC godrive pthread dl)

  set(GDA_CLI_SOURCES src/cli/main.cpp src/cli/commands.cpp)
  set(GDA_APP_LIBS gda_tools gda_ffi)

elseif(GDA_BACKEND STREQUAL "b2")
  set(GDA_EXECUTABLE_NAME b2-agent)

  include(cmake/B2Core.cmake)             # 新增:定義 `b2core` target(見第 6 節)
  add_library(gda_tools_b2 src/tools/b2_tool_dispatcher.cpp)
  target_include_directories(gda_tools_b2 PUBLIC include)
  target_link_libraries(gda_tools_b2 PUBLIC nlohmann_json::nlohmann_json)

  add_library(gda_ffi_b2 src/ffi/b2_bridge.cpp)
  target_include_directories(gda_ffi_b2 PUBLIC include)
  target_include_directories(gda_ffi_b2 PRIVATE ${B2CORE_INCLUDE_DIR})
  target_link_libraries(gda_ffi_b2 PUBLIC b2core pthread dl)

  set(GDA_CLI_SOURCES src/cli/main_b2.cpp src/cli/commands_b2.cpp)
  set(GDA_APP_LIBS gda_tools_b2 gda_ffi_b2)
endif()

if(GDA_ENABLE_MCP_SERVER)
  # gda_mcp 本身是後端無關的(見第 9 節的小改動),兩個後端共用同一個 library target。
  add_library(gda_mcp src/mcp/jsonrpc.cpp src/mcp/transport_stdio.cpp src/mcp/transport_http.cpp)
  target_include_directories(gda_mcp PUBLIC include)
  target_link_libraries(gda_mcp PUBLIC nlohmann_json::nlohmann_json httplib::httplib)
endif()

add_executable(${GDA_EXECUTABLE_NAME} ${GDA_CLI_SOURCES})
target_link_libraries(${GDA_EXECUTABLE_NAME} PRIVATE
  ${GDA_APP_LIBS} CLI11::CLI11
  $<$<BOOL:${GDA_ENABLE_MCP_SERVER}>:gda_mcp>
)
if(GDA_ENABLE_MCP_SERVER)
  target_compile_definitions(${GDA_EXECUTABLE_NAME} PRIVATE GDA_WITH_MCP_SERVER=1)
endif()

if(GDA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)   # tests/CMakeLists.txt 內部也依 GDA_BACKEND 分流,見第 10 節
endif()

install(TARGETS ${GDA_EXECUTABLE_NAME} RUNTIME DESTINATION bin)
```

對應的建置指令(README.md 之後要補一節,跟現有「## 2. Build」並列)：

```sh
# Google Drive 後端(預設,等同現在的行為)
cmake -B build-gdrive -DGDA_BACKEND=google_drive
cmake --build build-gdrive -j
# -> build-gdrive/gdrive-agent

# Backblaze B2 後端
cmake -B build-b2 -DGDA_BACKEND=b2
cmake --build build-b2 -j
# -> build-b2/b2-agent
```

**刻意用不同的 build 目錄**(`build-gdrive`/`build-b2`),不是共用一個 `build/`。原因:cgo
c-archive(`godrive`/`b2core`)跟各自的 CMake cache 變數(`GO_EXECUTABLE`、
`GODRIVE_INCLUDE_DIR`/`B2CORE_INCLUDE_DIR`)綁在同一個 build tree 裡,同一個目錄換
`-DGDA_BACKEND` 重新 configure 雖然理論上 CMake 會重新跑 if/else 分支,但沒必要冒殘留
stale object file / cache 變數的風險——分開目錄更乾淨,也方便兩個後端的執行檔同時存在、互相
比較測試。

## 6. 目錄結構與新增/修改檔案清單

```
Google-Drive-Agent/
├── CMakeLists.txt                      [修改] 本文件第 5 節的 GDA_BACKEND if/elseif
├── cmake/
│   ├── GoDriveCore.cmake                (現有,不動)
│   └── B2Core.cmake                     [新增] 建 b2core/ 的 cgo c-archive,鏡射 GoDriveCore.cmake
├── godrive/                              (現有,完全不動)
├── b2core/                                [新增] Go + cgo,跟 godrive/ 平行
│   ├── go.mod / go.sum
│   ├── main.go                           (package main; 空的 func main(){})
│   ├── client_config.go                  (LoadCredentials,讀 ~/.config/b2-agent/credentials.json)
│   ├── s3_client.go                      (建立 aws-sdk-go-v2 S3 client:自訂 endpoint/region/path-style)
│   ├── envelope.go                       (跟 godrive/envelope.go 同一套 {"ok":...}/{"error":...} 慣例)
│   ├── export_tools.go / export_tools_impl.go
│   ├── internal/testutil/fixture_transport.go  (假 HTTP transport,鏡射 godrive 的做法)
│   ├── testdata/
│   └── vendor/                           (vendor aws-sdk-go-v2 系列,见第 3.2 節)
├── include/gda/
│   ├── ffi/
│   │   ├── godrive_bridge.hpp            (現有,不動)
│   │   └── b2_bridge.hpp                 [新增] IB2Bridge + B2Bridge
│   ├── tools/
│   │   ├── tool_dispatcher.hpp           (現有,不動)
│   │   └── b2_tool_dispatcher.hpp        [新增] B2ToolDispatcher + B2ToolError
│   ├── cli/
│   │   ├── commands.hpp                  (現有,不動)
│   │   └── commands_b2.hpp               [新增]
│   └── mcp/                               [修改,見第 9 節] jsonrpc.hpp / transport_stdio.hpp / transport_http.hpp
├── src/
│   ├── ffi/
│   │   ├── godrive_bridge.cpp             (現有,不動)
│   │   └── b2_bridge.cpp                  [新增]
│   ├── tools/
│   │   ├── tool_dispatcher.cpp            (現有,不動)
│   │   └── b2_tool_dispatcher.cpp         [新增]
│   ├── cli/
│   │   ├── main.cpp / commands.cpp        (現有,不動)
│   │   └── main_b2.cpp / commands_b2.cpp  [新增]
│   └── mcp/                                [修改,見第 9 節]
├── tests/
│   ├── CMakeLists.txt                     [修改] 依 GDA_BACKEND 分流測試來源
│   └── cpp/
│       ├── fake_b2_abi.h / fake_b2_abi.cpp  [新增] 鏡射 fake_godrive_abi.*
│       ├── test_b2_bridge.cpp               [新增]
│       └── test_b2_tool_dispatcher.cpp      [新增]
├── Doc/
│   ├── B2-Backend-Plan.md                 [新增] 本文件
│   └── FFI-Contract-B2.md                 [實作階段才產生,見第 13 節]
├── client.json.example                     (現有,Google Drive 專用,不動)
└── b2-credentials.json.example             [新增]
```

**刻意不動既有 Drive 檔案的命名**(不把 `commands.cpp` 改名成 `commands_gdrive.cpp` 之類)——
新檔案一律用 `_b2`/`b2_` 當區分前後綴,把所有改動侵入面限制在「新增檔案」,既有、已測試過的
Drive 程式碼零異動。

## 7. FFI Contract 草案(B2 版)

延續 `Doc/FFI-Contract.md` 的規則:每個函式都是 `extern "C"`,JSON 字串進出,統一 envelope,
回傳字串用 `gda_free_string()` 釋放。**函式名稱改用 `gda_b2_` 前綴**(現有 Drive 那組維持
`gda_login`/`gda_search_files`/... 不變),純粹是為了兩套契約並列時一眼能分清楚,不是因為會有
符號衝突的風險(兩個後端從不會連到同一個執行檔裡)。

| # | 函式簽章 | 對應 CLI 指令(第 8 節) |
|---|---|---|
| 1 | `char* gda_b2_status(char* argsJson)` | `status` |
| 2 | `char* gda_b2_list_files(char* argsJson)` | `list` |
| 3 | `char* gda_b2_get_file_info(char* argsJson)` | `info` |
| 4 | `char* gda_b2_read_file_content(char* argsJson)` | `read` |
| 5 | `char* gda_b2_download_file(char* argsJson)` | `download` |
| 6 | `char* gda_b2_upload_file(char* argsJson)` | `upload` |
| 7 | `char* gda_b2_copy_file(char* argsJson)` | `copy` |
| 8 | `char* gda_b2_delete_file(char* argsJson)` | `delete` |
| 9 | `char* gda_b2_get_download_url(char* argsJson)` | `share` |
| 10 | `void gda_free_string(char* s)` | (記憶體管理,跟 Drive 版共用同一個符號名——反正不會連在一起) |

(`char*` 不是 `const char*`——跟 `Doc/FFI-Contract.md` §1 提過的原因一樣,cgo 產生的 header
沒有 `const`,C++ 端一樣需要 `AsMutable()` helper。)

每個函式 `argsJson`/`data` 形狀草案(實作前應該展開成獨立的 `Doc/FFI-Contract-B2.md`,這裡先
定調):

- `gda_b2_status`:args `{}` → data `{"bucket":"...", "endpoint":"...", "ok":true}`
- `gda_b2_list_files`:args `{"prefix":"string|null","limit":100,"startAfter":"string|null"}`
  → data `{"files":[{"key":"...","size":123,"uploadTimestamp":"...","contentType":"..."}],"nextStartAfter":"string|null"}`
- `gda_b2_get_file_info`:args `{"key":"..."}` → data `{"key":"...","size":123,"contentType":"...","uploadTimestamp":"...","contentSha1":"..."}`
- `gda_b2_read_file_content`:args `{"key":"..."}` → data `{"content":"...(UTF-8 文字)"}`
- `gda_b2_download_file`:args `{"key":"...","destPath":"..."}` → data `{"bytesWritten":123}`
- `gda_b2_upload_file`:args `{"localPath":"...","key":"...","contentType":"string|null"}`
  → data `{"key":"...","size":123,"uploadTimestamp":"..."}`
- `gda_b2_copy_file`:args `{"sourceKey":"...","destKey":"..."}` → data `{"key":"..."}`
- `gda_b2_delete_file`:args `{"key":"..."}` → data `{"key":"...","deleted":true}`
- `gda_b2_get_download_url`:args `{"key":"...","expiresInSeconds":3600}` → data `{"url":"...","expiresAt":"..."}`

## 8. CLI 指令對照表與語意落差

| Google Drive(`gdrive-agent`) | Backblaze B2(`b2-agent`) | 備註 |
|---|---|---|
| `login [--device]` / `logout` | `status` | B2 用靜態 key,沒有互動授權;`status` 只驗證憑證/bucket 可用 |
| `search "<query>"` | `list [--prefix X] [--limit N]` | **語意落差**:B2 沒有 Drive 的 query DSL(title contains/starred=true...),只有 prefix 前綴比對,不支援全文/屬性搜尋 |
| `list-recent [--order-by] [--limit]` | `list [--limit N]` | B2 的 list 結果可依 `uploadTimestamp` 排序,但沒有 Drive「最近使用」那種以使用者操作歷史為準的概念 |
| `metadata <fileId>` | `info <key>` | 對應關係清楚,欄位不同(B2 沒有 `parents`/`webViewLink`,但有 `contentSha1`) |
| `permissions <fileId>` | *(不提供)* | B2 沒有物件層級的權限/共享對象清單,只有 bucket 層級 public/private;直接不實作這個指令 |
| `read <fileId>` | `read <key>` | 對應關係清楚 |
| `download <fileId> -o <path>` | `download <key> -o <path>` | 對應關係清楚 |
| `upload <path> [--parent]` | `upload <path> [--key X]` | B2 沒有「資料夾」,`--key` 直接決定完整物件鍵名(可以用 `/` 模擬路徑) |
| `create-doc --type doc\|sheet\|...` | *(不提供)* | Google 原生文件類型在 B2 沒有對應概念,直接不實作 |
| `update <fileId> [--title] [--parent]` | *(第一版不提供,見下)* | B2 物件上傳後不能「就地改中繼資料」,只能整個重新上傳或用 `CopyObject` 搭配 metadata-replace;第一版先不做,列入第 13 節後續階段 |
| `copy <fileId>` | `copy <key> --dest <newKey>` | 對應關係清楚,底層是 S3 `CopyObject` |
| `share <fileId> --email --role` | `share <key> [--expires-in N]` | **語意落差**:B2 沒有「分享給某個 email、設權限層級」這種對象式分享,只能產生一個時限內可下載的 pre-signed URL(或者 bucket 設成 public 後給直接網址) |
| `trash <fileId>` | `delete <key>` | **語意落差**:Drive 的 trash 可還原,B2 的「刪除」預設是把目前版本標記成已刪除、舊版本仍保留(取決於 bucket 的 lifecycle/versioning 設定),不是真正可逆的「資源回收桶」;README 要講清楚 |
| `serve-mcp [--transport] [--port]` | `serve-mcp [--transport] [--port]` | 機制相同(見第 9 節),底下註冊的 tool 清單不同 |

## 9. MCP Server Facade 的因應(現有 `gda_mcp` 需要一次小改動)

檢查過現有程式碼(`include/gda/mcp/jsonrpc.hpp`):`HandleRequest`/`RunStdioServer`/
`RunHttpServer` 目前的參數簽章直接寫死 `gda::ToolDispatcher&`(具體類別,不是介面),
`ToolHandler` 的型態是 `std::function<std::string(gda::ToolDispatcher&, const std::string&)>`。
這代表 `gda_mcp` **目前是跟 Drive 的 `ToolDispatcher` 綁死的**,不能直接拿給 B2 用。

兩個選項：

- **(不建議)整包複製一份 `gda_mcp_b2`**:複製 `jsonrpc.cpp`/`transport_stdio.cpp`/
  `transport_http.cpp`,把型態換成 `B2ToolDispatcher&`。缺點:JSON-RPC 協定邏輯(parse
  request、組 success/error response、stdio/http 傳輸細節)跟後端完全無關,複製一份等於兩份
  要同步維護,之後協定本身要是要改(例如升級 MCP 版本)兩邊都要改,容易漂移。
- **(建議)把 `gda_mcp` 一般化成後端無關**:把 `ToolHandler` 改成
  `std::function<std::string(const std::string&)>`(呼叫端用 lambda 把自己的 dispatcher 捕捉
  進去,而不是讓 `gda_mcp` 知道 dispatcher 的具體型態),`HandleRequest`/`RunStdioServer`/
  `RunHttpServer` 的參數從 `gda::ToolDispatcher&` 改成
  `const std::unordered_map<std::string, ToolHandler>&`(一個「工具名稱 → 處理函式」的表,由
  呼叫端——`main.cpp`/`main_b2.cpp`——各自建表)。這是**簽章層面的小改動**,`ToolRegistry()`
  原本回傳固定的 11+2 個工具表,改成由 `main.cpp` 呼叫一個 `BuildDriveToolRegistry(ToolDispatcher&)`
  函式在呼叫端組出同一張表,行為完全不變。`gda_mcp` 這個 library target 本身維持單一一份,兩個
  後端共用,只是誰來餵「工具名稱對應表」不同。

本規劃書採用**第二個方案**:實作階段先把 `jsonrpc.hpp`/`.cpp`、`transport_stdio.hpp`/`.cpp`、
`transport_http.hpp`/`.cpp` 這個簽章改掉(連帶 `tests/cpp/test_mcp_jsonrpc.cpp` 要跟著更新呼叫
方式),這是唯一一處**既有、已測試過的程式碼需要被動到**的地方,改動本身是機械式的(把具體型
態換成一個 map 參數),不影響現有行為跟測試覆蓋的斷言內容。

## 10. 測試策略

延續現有慣例(Task 7 定的方向:暫不優先做更多單元測試,但既有測試不能破),新增的部分一樣套用
現有 pattern,不重新發明：

- **Go 側**(`b2core/`):`go test ./...`,用 `internal/testutil/fixture_transport.go`
  (鏡射 `godrive/internal/testutil/fixture_transport.go`)假造 AWS SDK 的 HTTP transport
  (SDK v2 的 `http.Client`/middleware 層可以注入自訂 transport,不需要真的連 B2),不打真實
  網路、不需要真的帳號。
- **C++ 側**(`gda_tests`):`fake_b2_abi.h/.cpp` 鏡射 `fake_godrive_abi.h/.cpp` 的做法——手寫一
  個假的 `extern "C"` ABI,`gda_ffi_b2_test_double` library(鏡射 `gda_ffi_test_double`)拿同一份
  `src/ffi/b2_bridge.cpp` 對著假 header 編譯,測 `B2Bridge` 的 marshalling 邏輯不需要 Go 工具鏈。
- `tests/CMakeLists.txt` 依 `GDA_BACKEND` 決定要編譯哪一組測試來源(`test_ffi_bridge.cpp` +
  `test_tool_dispatcher.cpp` vs `test_b2_bridge.cpp` + `test_b2_tool_dispatcher.cpp`),`ctest`
  一樣是一個指令跑完全部。

## 11. 已知限制與風險

- **搜尋能力大幅縮水**:B2 沒有 Drive 那種 query DSL,只有 prefix/limit——如果使用者重度依賴
  `search "title contains 'x' and starred = true"` 這類複雜查詢,`b2-agent` 做不到對等體驗。
- **沒有真正的資料夾/parent-child 關係**:完全靠檔名前綴模擬,`upload --key "folderA/folderB/file.txt"`
  這種用法使用者要自己維護命名慣例。
- **trash 不是真的回收桶**:見第 8 節,刪除行為跟 bucket 的版本控制設定連動,要在 README 講清楚
  避免誤會成跟 Drive trash 一樣可安全復原。
- **沒有物件層級權限/協作者清單**:`permissions`/`share --email --role` 這兩個指令在 B2 做不
  到對等功能,只能退化成「產生一個有效期限內的下載連結」。
- **Master Key 陷阱**:第 4 節提過,憑證設定錯誤(用了 master key,或忘記勾
  `listAllBucketNames`)會在呼叫 S3 API 時出現不直觀的權限錯誤,這個坑要在 README 的設定章節
  明確預警,不要等使用者自己撞到才發現(複習 Google Drive 版本的教訓:事先把已知的認證陷阱寫
  清楚,比事後除錯省時間)。
- **AWS SDK v2 的 vendor 體積**:初步預期 `b2core/vendor/` 會比 `godrive/vendor/` 大(AWS SDK
  v2 的 transitive 依賴通常不少),需要在實作階段實際量一次,確認不會讓 build 時間/repo 大小
  超出可接受範圍。

## 12. 實作階段規劃

1. **Phase 0**:`b2core/` go module 初始化,`go mod vendor` 把 aws-sdk-go-v2 系列進倉,手動寫
   一個最小 `main.go` 驗證 cgo c-archive 能編出來(不含任何真正業務邏輯),確認 `cmake/B2Core.cmake`
   能照抄 `cmake/GoDriveCore.cmake` 的模式跑起來。
2. **Phase 1(核心,對齊第 7 節 FFI 清單的前 6 個)**:`status`、`list`、`info`、`read`、
   `download`、`upload`——這組是「讓 b2-agent 真的能用來存取一個 bucket」的最小可用集合。
3. **Phase 2**:`copy`、`delete`、`share`——補齊剩下的操作型指令。
4. **Phase 3**:`serve-mcp`——先完成第 9 節 `gda_mcp` 的一般化改動(连带跑一次既有 Drive 測試
   確認沒有行為改變),再接上 B2 的工具註冊表。
5. **Phase 4(可選,視需求決定要不要做)**:`update` 的 metadata-replace 版本、分段上傳大檔
   (`feature/s3/manager`)、bucket lifecycle/versioning 相關的進階指令。

每個 Phase 結束都照現有慣例整合驗證一次(`cmake --build` 全建、`ctest` 全跑、`ldd` 確認沒有多
連到非預期的動態函式庫),不要等全部做完才第一次整合。

## 13. 下一步建議

- 這份文件定調完整架構跟 CMake 機制;**實作前**建議先把第 7 節的 FFI 草案展開成正式的
  `Doc/FFI-Contract-B2.md`(格式照抄 `Doc/FFI-Contract.md`),跟當初 Google Drive 版本一樣先把
  契約釘死,再分工實作,避免兩邊(Go core / FFI Bridge)各自實作出對不起來的介面。
- 建議先做 Phase 0 + Phase 1,實際跑出一個能 `list`/`upload`/`download` 真實 bucket 的
  `b2-agent`,驗證「AWS SDK v2 + B2 S3-compatible endpoint」這個技術選型在真實帳號上真的如預
  期運作,再決定要不要繼續 Phase 2 以後——這塊目前只查了官方文件,還沒有實際跑過一次真實呼叫。
