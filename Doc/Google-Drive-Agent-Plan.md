# Google Drive Agent — C/C++ Linux 原生應用程式規劃書

- 文件類型:技術規劃書(Feasibility Study + Software Architecture + Project Architecture)
- 版本:v3 draft
- 日期:2026-10-18
- 範圍:評估「用 C/C++ 開發一個 Linux app,直接操作 Google Drive(不透過 Claude session)」的可行性,並給出軟體架構與專案架構設計。

> **v2 修訂說明**:v1 曾寫「不透過任何現成的 Node.js / Python Google Drive MCP Server 當子行程」,這個講法不準確——現成的 Google Drive MCP 實作(TypeScript、Python)確實存在。真正的問題不是「能不能用現成的」,而是你的需求本來就是:**把現成的東西下載進專案,cmake 建置時一起編譯進主程式,執行期的主程式不依賴任何外部套件、process 或 service**。v2 針對這個需求,重新分析了四種「重用現成程式碼」的具體做法(第 4 節),選出可行性與風險評估後最適合的方案,並據此改寫了第 5、6 節的架構設計。
>
> **v3 修訂說明**:v1/v2 只在依賴套件表裡用一行提到測試框架(「doctest 或 Catch2」),沒有明確選定工具,也沒有講測試案例/fixture 檔案該放哪裡。v3 新增第 8 節「測試策略」,明確選定 C++ 與 Go 兩側各自的測試工具、mock Google API 的方式,以及 test case / fixture 檔案的存放位置與命名慣例,方便你之後直接把測試案例加進專案。

---

## 目錄

1. 背景與動機
2. 結論先講:可行嗎?
3. 關鍵澄清:「連上這個 session 的 Google Drive MCP」是什麼意思
4. 重用現成函式庫/實作的可行性分析
5. 技術選型
6. 軟體架構(Software Architecture)
7. 專案架構(Project Architecture)
8. 測試策略(Unit Testing)
9. CLI 指令設計
10. 開發階段與里程碑
11. 安全與隱私考量
12. 風險、未知與待驗證事項
13. 下一步建議
14. 附錄:參考資料

---

## 1. 背景與動機

目前你操作 Google Drive(搜尋、讀取、上傳、分享、丟垃圾桶...)都是透過 Claude session 裡的 Google Drive MCP 工具間接完成的。這份規劃書回答的問題是:

> 能不能自己寫一個 C/C++ 的 Linux app,直接具備這些能力,以後就不需要開 Claude 才能操作 Drive?

目標是做出一個**單一、自含(self-contained)的原生執行檔**,在 Linux 終端機 / shell script / cron job 裡就能直接呼叫,涵蓋以下能力(對齊目前這個 session 可用的 11 個 Google Drive MCP 工具,完整 schema 已在規劃過程中實際讀出,整理於第 6.3 節):

| # | 能力 | 對應這個 session 的 MCP 工具 |
|---|------|------------------------------|
| 1 | 搜尋檔案 | `search_files` |
| 2 | 列出最近檔案 | `list_recent_files` |
| 3 | 取得檔案 metadata | `get_file_metadata` |
| 4 | 取得檔案權限 | `get_file_permissions` |
| 5 | 讀取檔案內容(文字化) | `read_file_content` |
| 6 | 下載檔案內容(原始 bytes) | `download_file_content` |
| 7 | 建立/上傳檔案 | `create_file` |
| 8 | 更新檔案 metadata / 搬移 | `update_file` |
| 9 | 複製檔案 | `copy_file` |
| 10 | 分享檔案 | `share_file` |
| 11 | 丟到垃圾桶 | `trash_file` |

---

## 2. 結論先講:可行嗎?

**可行。** 更新後的做法是:C/C++ 執行檔的「外殼」(CLI、參數解析、輸出格式化,選配的 MCP Server Facade)維持用 C/C++ 寫;Google Drive 的實際存取邏輯,改成重用 **Google 官方維護的 Go 語言 Drive client library**(`google.golang.org/api/drive/v3`),用 Go 官方支援的 `-buildmode=c-archive` 編譯成靜態函式庫(`.a` + `.h`),在 CMake 建置流程中直接連結進同一個執行檔。

核心判斷:

- MCP(Model Context Protocol)本質上就是 JSON-RPC 2.0,對實作語言沒有限制——這部分維持用 C/C++ 手刻一個輕量 facade(選配,見第 6.6 節)。
- Google Drive 真正的能力來源是 **Google Drive REST API v3**。這一層不必從零手刻 HTTP/JSON——Google 自己就有官方維護的 Go client library 可以重用,且 Go 的編譯模型(靜態編譯、`c-archive` 輸出模式)剛好非常適合「下載進專案、建置時編譯進主程式、執行期零外部相依」這個目標,比起其他語言的現成實作反而更好嵌入。
- 第 3 節提到的、這個 session 用的那組 `mcp__claude_ai_Google_Drive__*` 工具,依然是 claude.ai 私有的 connector,外部程式連不上——這點 v1、v2 都一樣,我們是重用「Google 官方的 Drive API client」,不是重用「Anthropic 私有的 connector」。
- 結果是:做完以後,你確實「不用透過 Claude session」就能在同一台 Linux 機器上用這個 app 做到 search / list / read / download / metadata / permissions / create / update / copy / share / trash,而且 Drive 存取邏輯的大部分程式碼是 Google 官方寫的、維護的,不是我們從零重新發明的。

---

## 3. 關鍵澄清:「連上這個 session 的 Google Drive MCP」是什麼意思

這個 session 裡你能看到的 11 個工具,名稱都是 `mcp__claude_ai_Google_Drive__*`。這組前綴代表:

- 它們是 **claude.ai 的第一方 Google Drive connector**,實際跑在 Anthropic 的伺服器端基礎設施裡,綁定你 claude.ai 帳號的 OAuth 授權。
- 它沒有對外公開、文件化的「連線端點」給第三方 client(例如我們要寫的 C/C++ app)直接連線呼叫——這不是一個你可以拿著 API Key 或 OAuth token 直接打的公開服務。
- 所以「直接連上這個(claude.ai 內部的)MCP」在字面意義上**不可行**,原因不是技術難度,而是這組伺服器本來就不是設計給外部獨立程式連線的。

這個限制跟第 4 節要討論的「能不能重用現成的 Google Drive MCP / library 實作」是兩件獨立的事:我們連不上 Anthropic 私有的那一組,但**可以**重用別的、公開存在的現成實作或官方函式庫——這正是 v2 修訂要處理的問題。

我已經把這個 session 裡這 11 個工具的完整 input schema 讀出來,整理成第 6.3 節的對照表——這是「參考這個 session 的 MCP 了解我們程式要怎麼呼叫」最直接的具體依據,v1、v2 都延用。

另一個背景事實(v1 就提過,這裡保留):MCP 官方規格在 **2026-07-28** 有一次較大改版(拿掉 `initialize` 握手、整個協定改成 stateless、`stdio`/`Streamable HTTP` 兩種 transport、`roots`/`sampling`/`logging` 進入 deprecation)。這點只影響第 6.6 節「選配 MCP Server Facade」的設計,跟第 4 節的重用策略無關。

---

## 4. 重用現成函式庫/實作的可行性分析(v2 修訂重點)

你的需求釐清為:**"Google mcp, Dependence library 都能 inside program" = 把現成的東西下載進專案 repo,cmake 建置時把它編譯/連結進主程式,最終的主程式執行期不依賴任何外部套件、process 或 service。** 這一節針對市面上現成的 Google Drive MCP / client library 實作,逐一分析「能不能、怎麼做到這件事」。

### 4.1 現成實作的語言現況

市面上找得到的「Google Drive MCP Server」現成實作,全部是 **TypeScript/Node.js** 或 **Python**:

| 實作 | 語言 | 性質 |
|---|---|---|
| 官方 `modelcontextprotocol/servers` 裡的 gdrive reference server | TypeScript(Node.js) | 官方自述為教學範例,非生產等級 |
| `piotr-agier/google-drive-mcp` | TypeScript(Node.js) | 社群維護,涵蓋 Drive/Docs/Sheets/Slides/Calendar |
| `isaacphi/mcp-gdrive` | TypeScript(Node.js) | 社群維護 |
| `adexltd/mcp-google-suite` | Python | 社群維護,涵蓋 Drive/Docs/Sheets |

C/C++ 本身**沒有**對應的現成「Google Drive library」或「Google Drive MCP server」——官方從未提供 Drive API 的 C++ client library(舊的 `google-api-cpp-client` 專案早已停止維護),這點 v1、v2 的研究結論一致。所以「下載現成的、編譯進主程式」這件事,必然要面對「跨語言整合」的問題:要嵌入別的語言的 runtime,或是改找一個編譯模型天生適合跟 C/C++ 靜態整合的語言實作。以下針對四個具體選項做可行性分析。

### 4.2 選項 A:嵌入 Node.js(libnode)+ 現成 TypeScript Google Drive MCP Server

把上面任一個 TypeScript 實作的原始碼下載進專案,連同 Node.js 本身,一起編譯/連結進最終執行檔,讓 C++ `main()` 內部啟動一個嵌入式 Node.js 執行環境來跑這段 TypeScript(編譯後的 JS)。

- Node.js 官方確實提供 embedder API(`node.h`/`node_api.h`,參考實作在 Node 原始碼的 `test/embedding/embedtest.cc`),技術上可行。
- **但實測驗證結果不理想**:有開發者依照官方文件嘗試 `--enable-static --fully-static` 編出 `libnode.a` 後,與自己的程式連結時出現大量對 V8 symbol 的 undefined reference,且該討論串在我查到的範圍內沒有看到解法。另一個可行的替代是用 shared library(`--shared`)模式嵌入,但這樣會多一個 `libnode.so` 執行期檔案——雖然不是「外部 process」,但也不是真正單一執行檔的「完全靜態」。
- Node.js 官方文件明確警告:embedder API **不遵守一般的 deprecation 流程**,每個 semver-major 版本都可能直接 break,沒有事先警告——這代表長期維護風險高,升級 Node 版本可能隨時讓這段整合碼壞掉。
- 額外成本:V8 + Node core 本身就是數十 MB 到上百 MB 的量級,建置時間會從「幾分鐘」變成「要先編完整個 Node.js from source」。
- 好消息(但不足以抵銷上述風險):`googleapis`、`@modelcontextprotocol/sdk` 這兩個 TS 實作的核心依賴,就我查到的範圍看是純 JavaScript/TypeScript,沒有 node-gyp 原生擴充套件,所以至少不會有「native addon 要跨平台重新編譯」的額外麻煩。

**結論:技術上可行,但靜態嵌入的風險、複雜度、維護成本都偏高,不建議採用。**

### 4.3 選項 B:嵌入 CPython + 現成 Python Google Drive MCP Server

把 `adexltd/mcp-google-suite` 這類 Python 實作下載進專案,連同 CPython 本身一起靜態編譯進執行檔,C++ `main()` 用 Python/C API(或 pybind11 的 `embed` 模組)啟動內嵌的直譯器來跑它。

- CPython 的 embedding API(`Py_Initialize` 等)比 Node 的 embedder API 成熟、穩定得多,pybind11 甚至有現成的 `pybind11::embed` CMake target,幾行就能接上。
- 針對「嵌入進單一自含執行檔」這個特定情境,有專案(`python-build-standalone`)專門提供預先建置好的、適合這種用途的靜態/可攜 CPython,且**明確指出 Linux 是比較容易的平台**(Windows 需要額外 patch)——剛好符合我們只打 Linux 的範圍。
- **主要障礙是 transitive 依賴**:這類 Python 實作會依賴 `mcp`(Python SDK)、`google-api-python-client`、`google-auth`/`google-auth-oauthlib`、`httplib2` 等一整串套件,其中像 `google-auth` 常見會拉進 `cryptography`——這個套件本身帶有編譯過的原生擴充模組(Rust/C,連結 OpenSSL)。要做到「完全沒有外部相依」,這類原生擴充模組也必須一起靜態編譯、用 `PyImport_AppendInittab()` 註冊成內建模組,而不能依賴系統上現成的 `.so`。這是可行的作法,但工程複雜度明顯比純 pure-Python 套件高。

**結論:在 Linux 上比選項 A 可行,但仍需要處理至少一個原生擴充模組的靜態化,加上一整個 Python 套件依賴樹要 vendor 進專案,複雜度中高。**

### 4.4 選項 C:Go + Google 官方 `google.golang.org/api/drive/v3`(採用)

Google 官方自己維護一套 Go 語言的 API client library(`google.golang.org/api`,其中包含 `drive/v3`),目前狀態是「功能完整、進入 maintenance mode」——這不是棄用,是指功能穩定不再新增,但仍持續修安全性與重大問題,對 Drive API v3 這種本來就很穩定的介面來說完全夠用。

Go 的編譯模型剛好非常適合我們的需求:

- 用 `go build -buildmode=c-archive` 可以把一個 Go package 編譯成一個靜態函式庫(`.a` 檔)加一份 C header(`.h`),**這本來就是 Go 官方設計給 C/C++ 程式直接連結用的輸出格式**——不需要嵌入任何直譯器或 VM,Go 自己的 runtime(goroutine scheduler、GC)是靜態編譯進這個 `.a` 檔裡的,執行期不需要另外安裝 Go。
- 這條路徑有現成、可參考的工作範例(`go-interlang` 專案),作法就是:CMake 執行 `go build -buildmode=c-archive`,產出的 `.a`/`.h` 當一般靜態函式庫,用 `target_link_libraries` 連進 C++ 執行檔。
- **已知的坑**:cgo c-archive 跟 host 程式的 `main()` 整合,如果用法不對(例如同時連結兩個不同的 Go c-archive,或是搞混 cgo "main package" 的語意)會出現 Go runtime symbol(如 `_cgo_topofstack`、`_crosscall2`)重複定義的連結錯誤。但「一個 C++ `main()` + 一個 Go c-archive 函式庫」正是 Go 官方支援良好的標準用法,只要照這個模式做(Go 那邊的 package 仍需要是 `package main` 並保留一個空的 `func main() {}`,但程序真正的進入點是 C++ 的 `main()`),不是結構性問題。
- `golang.org/x/oauth2`(含 `golang.org/x/oauth2/google`)是 Go 生態裡處理 OAuth2(含 installed-app/loopback flow)最成熟的套件之一,OAuth 登入流程改放到 Go 層其實比原本 v1 規劃用 C++ 手刻更省工。
- 授權條款:`google.golang.org/api`、`golang.org/x/oauth2` 都是 BSD-3-Clause,對個人/封閉專案沒有限制。
- 「下載進專案」的具體做法:用 `go mod vendor` 把這些套件的原始碼真的下載、存進專案目錄(例如 `godrive/vendor/`),建置時用 `-mod=vendor` 強制只用本地已下載的原始碼,**建置過程本身也不需要連網**。

**結論:這是四個選項裡「現成程度」最高(官方維護,不是第三方包裝)、嵌入複雜度最低(靜態編譯語言,無 VM/直譯器要打包)、長期維護風險最小的做法。本規劃書採用此方案。**

### 4.5 選項 D(v1 原本的做法):純 C++ 原生實作,不嵌入任何現成實作

完全用 C++ 手刻 HTTP + JSON 呼叫 Drive REST API v3,現成的 MCP server 原始碼只當設計參考,不實際編譯進專案。

**結論:這條路徑最輕量、建置最快、沒有任何跨語言嵌入風險,但完全沒有重用到任何現成的 Google 官方或社群程式碼,跟你「希望使用現成 lib/mcp」的需求不符,v2 不採用,改用選項 C。**

### 4.6 小結比較表

| 選項 | 現成程度 | 嵌入複雜度 | 執行期依賴 | 長期維護風險 | 採用? |
|---|---|---|---|---|---|
| A. 嵌入 Node.js + TS 實作 | 高(重用整套 server) | 高(V8 靜態連結已知會出錯) | 可能需 `libnode.so` | 高(embedder ABI 不保證相容) | 否 |
| B. 嵌入 CPython + Python 實作 | 高(重用整套 server) | 中高(原生擴充模組需靜態化) | 無(可完全靜態) | 中 | 否 |
| C. Go + 官方 Drive client(cgo c-archive) | 高(官方函式庫) | 中(已知坑但有標準解法) | 無(靜態編譯) | 低(官方維護、Go 編譯穩定) | **採用** |
| D. 純 C++ 手刻 | 低(只當參考) | 低 | 無 | 低 | 否(v1 做法,已取代) |

---

## 5. 技術選型

### 5.1 設計原則(對應你的 Spec,v2 更新)

> 「希望使用的 Google mcp, Dependence library 都能 inside program,我不希望外部 call」

對應到:

1. **Drive 存取邏輯重用 Google 官方的 Go client library**,不手刻 REST/HTTP/JSON——這是「使用現成 lib」的具體落實。
2. **該函式庫以原始碼形式下載進專案(`go mod vendor`),build 時由 CMake 呼叫 Go 工具鏈編譯成 `.a` 靜態函式庫、連結進最終的單一執行檔**——這是「下載進專案、cmake 包進主程式」的具體落實。
3. **最終執行檔執行期沒有外部 process、沒有另外安裝的 runtime、沒有動態尋找的 service**——Go 的 c-archive 輸出本身就是靜態的,不需要另外安裝 Go 或任何 `.so`。
4. 唯一無法避免、也不該避免的「外部」行為,是對 Google 伺服器(`*.googleapis.com`、`accounts.google.com`)的 HTTPS 呼叫本身——這是 Drive 功能的本質,跟「不依賴外部 process/service」是两件事。OAuth 登入時用 `xdg-open` 開瀏覽器,屬於所有桌面 OAuth 流程的標準作法,也不算違反此原則;headless 環境的替代方案見第 6.4 節。

### 5.2 建置期(build-time)工具鏈

| 用途 | 選擇 | 理由 |
|---|---|---|
| Go 工具鏈 | Go >= 1.22(建置期需要,執行期不需要) | 編譯 `godrive` 模組成 c-archive |
| C++ 編譯器 | gcc >= 11 或 clang >= 14,C++20 | 建置 CLI/facade 外殼 |
| 建置系統 | CMake >= 3.20 | 統一驅動 C++ 編譯與 Go c-archive 編譯 |

### 5.3 執行期(runtime)依賴套件清單

| 用途 | 選擇 | 型態 | 理由 |
|---|---|---|---|
| Google Drive 存取 | `google.golang.org/api/drive/v3` | Go 原始碼,`go mod vendor` 進專案,編譯進 c-archive | 官方維護,取代手刻 REST/HTTP/JSON |
| OAuth2(含 PKCE/loopback) | `golang.org/x/oauth2`、`golang.org/x/oauth2/google` | 同上 | Go 生態最成熟的 OAuth2 實作 |
| C++ 端 JSON(解析 FFI 回傳值、選配 MCP envelope) | nlohmann/json | header-only,FetchContent 進專案 | C++ 事實標準,單檔引入 |
| CLI 參數解析 | CLI11 | header-only,FetchContent 進專案 | 輕量、型別安全 |
| 選配:本機 MCP Server 的 Streamable HTTP transport | cpp-httplib | header-only,FetchContent 進專案 | 只有做第 6.6 節選配功能時才需要 |
| C++ 單元測試 | doctest(確定選用,理由見第 8.1 節) | header-only,FetchContent 進專案 | 比 Catch2 輕量、編譯快,功能仍足夠 |
| Go 單元測試 | 標準庫 `testing` + `testify`(確定選用,理由見第 8.1 節) | `testify` 原始碼 vendor 進 `godrive/vendor/` | Go 生態標準做法 |

> 相較 v1:C++ 端不再需要 libcurl / OpenSSL——HTTPS、TLS、OAuth2 都由 Go 那層的標準函式庫(`net/http`、`crypto/tls`)處理,C++ 端只剩 CLI 解析、JSON 格式化、選配的 MCP transport。整體依賴數量比 v1 更少。

---

## 6. 軟體架構(Software Architecture)

### 6.1 分層架構圖

```
+------------------------------+      +----------------------------------+
|        CLI Frontend          |      |   MCP Server Facade (optional)    |
|   (CLI11 argv / REPL shell)  |      |   stdio or Streamable HTTP        |
+---------------+--------------+      +-----------------+------------------+
                 |                                       |
                 +-------------------+-------------------+
                                     |
                                     v
                    +---------------------------------+
                    |   Tool Dispatcher / Facade API    |
                    |   (C++) 11 functions, 1:1 對齊     |
                    |   第 6.3 節的 MCP 工具集            |
                    +------------------+----------------+
                                       |  FFI (C ABI, JSON-in / JSON-out)
                                       v
                    =====================================
                    |     godrive — cgo c-archive (.a)   |   <- Go 編譯、靜態連結進執行檔
                    |-------------------------------------|
                    |  export_tools.go  (//export 函式)   |
                    |  drive_client.go  (業務邏輯)         |
                    |  oauth.go         (登入流程)         |
                    |  token_store.go   (token 持久化)     |
                    |-------------------------------------|
                    |  google.golang.org/api/drive/v3     |  <- Google 官方函式庫
                    |  golang.org/x/oauth2 (+ /google)    |  <- Google 官方函式庫
                    =====================================
                                       |
                                       v
                         Google Drive REST API v3
                 (www.googleapis.com / accounts.google.com)
```

C++ 與 Go 之間不是「呼叫外部服務」,是**同一個作業系統 process 內的函式呼叫**(cgo c-archive 連結進同一個執行檔,FFI 邊界用純記憶體內的 C 字串/JSON 傳遞),所以完全符合「執行期沒有外部 process/call」的要求。

### 6.2 各層職責

| 層 | 語言 | 職責 | 關鍵檔案/模組 |
|---|---|---|---|
| CLI Frontend | C++ | 解析命令列參數,轉成 Tool Dispatcher 呼叫,格式化輸出 | `main.cpp`, `commands.cpp` |
| MCP Server Facade(選配) | C++ | 用 JSON-RPC 2.0 把 Tool Dispatcher 暴露給其他 MCP host | `jsonrpc.hpp`, `transport_stdio.hpp`, `transport_http.hpp` |
| Tool Dispatcher | C++ | 11 個函式,對應第 6.3 節的 MCP 工具;驗證參數、組 JSON、呼叫 FFI、解析回傳 | `tool_dispatcher.hpp/.cpp` |
| FFI Bridge | C++ | 包一層 cgo 產生的 C header,處理字串記憶體生命週期(呼叫 Go 端的 `gda_free_string`) | `godrive_bridge.hpp/.cpp` |
| godrive 業務邏輯 | Go | 11 個工具對應的實際 Drive 操作、query DSL 翻譯、角色比較、addParents/removeParents 等邏輯 | `export_tools.go`, `drive_client.go` |
| godrive 認證 | Go | OAuth2 + PKCE / device flow,token 持久化與自動刷新 | `oauth.go`, `token_store.go` |
| Google 官方函式庫 | Go | 實際的 Drive REST 呼叫、HTTPS/TLS、OAuth2 token 交換 | `google.golang.org/api/drive/v3`, `golang.org/x/oauth2` |

### 6.3 MCP Tool ↔ Google Drive API 對照表(語意對照,底層改走 Go SDK)

這張表來自這個 session 裡 11 個 `mcp__claude_ai_Google_Drive__*` 工具的真實 input schema,說明每個工具要呼叫 Go 官方 SDK 裡的哪個方法(取代 v1「手刻 REST 呼叫」的寫法,邏輯語意不變,只是改由官方 SDK 執行實際的 HTTP/JSON)。

**1. `search_files`** — 輸入:`query`(結構化查詢字串)、`pageSize`、`pageToken`
- Go 端:`drive.NewFilesService(srv).List().Q(翻譯後的查詢).PageSize(...).PageToken(...).Do()`
- `query` DSL 翻譯對照(C++ Tool Dispatcher 或 Go 端皆可做,建議放 Go 端,跟 `Q()` 呼叫放一起):

  | MCP query 寫法 | Drive API v3 `q` 等效寫法 |
  |---|---|
  | `title contains 'x'` | `name contains 'x'` |
  | `title = 'x'` / `title != 'x'` | `name = 'x'` / `name != 'x'` |
  | `fullText contains 'x'` | 相同 |
  | `mimeType = /!= /contains 'x'` | 相同 |
  | `modifiedTime`/`createdTime`/`viewedByMeTime` 比較運算 | 同名欄位,SDK 原生支援 |
  | `parentId = 'X'` | `'X' in parents` |
  | `owner = 'me'` 或 email | `'me' in owners` 或 `'email' in owners` |
  | `sharedWithMe = true/false`、`starred = true/false` | 相同 |

**2. `list_recent_files`** — `orderBy`(`recency`/`lastModified`/`lastModifiedByMe`)
- Go 端:`.List().OrderBy(...)`;`lastModified` → `modifiedTime desc`,`lastModifiedByMe` → `modifiedByMeTime desc`,`recency`(預設,自訂語意)→ `viewedByMeTime desc`。建議永遠帶 `Q("trashed=false")`。

**3. `get_file_metadata`** — `drive.NewFilesService(srv).Get(fileId).Fields(...).Do()`

**4. `get_file_permissions`** — `drive.NewPermissionsService(srv).List(fileId).Fields(...).Do()`

**5. `read_file_content`**(最重的一個工具)
- Google 原生類型 → `drive.NewFilesService(srv).Export(fileId, mimeType)`,文字化由 Google 伺服器端完成。
- 純文字檔 → `drive.NewFilesService(srv).Get(fileId).Download()`。
- PDF/Office/圖片的「自然語言文字化」→ 需要額外本地 parsing/OCR,Go 生態一樣沒有現成的一站式方案,建議跟 v1 一樣排入 Phase 2+(見第 10 節),不卡 v1 範圍。
- `includeComments` → `drive.NewCommentsService(srv).List(fileId)`。

**6. `download_file_content`** — `exportMimeType`/`revisionId`
- 原生類型 → `Export()`;其他類型 → `Get().Download()`;指定 `revisionId` → `drive.NewRevisionsService(srv).Get(fileId, revisionId).Download()`。
- Go 端 base64 encode 後,透過 FFI 以字串回傳給 C++。

**7. `create_file`** — `title`/`parentId`/`textContent`/`base64Content`/`contentMimeType`/`disableConversionToGoogleType`
- `drive.NewFilesService(srv).Create(&drive.File{...}).Media(reader).Do()`;SDK 內建會依檔案大小自動選擇 simple 或 resumable upload(`google.golang.org/api` 的 media upload 機制本身就處理這塊,比 v1 自己刻快很多)。

**8. `update_file`** — `title`/`parentId`/`description`
- `drive.NewFilesService(srv).Update(fileId, &drive.File{Name:..., Description:...}).AddParents(newId).RemoveParents(oldId).Do()`——SDK 的 `Update()` builder 直接支援 `AddParents`/`RemoveParents`,跟 v1 一樣需要先 `Get(fields=parents)` 拿舊的 parent。

**9. `copy_file`** — `drive.NewFilesService(srv).Copy(fileId, &drive.File{Name:..., Parents: [...]}).Do()`

**10. `share_file`** — `role`/`emailAddress`/`chatSpaceId`
- `drive.NewPermissionsService(srv).Create(fileId, &drive.Permission{Type:"user", Role:role, EmailAddress:email}).Do()`。
- 「已有權限只在角色更高時更新」的邏輯 SDK 沒有內建,仍要自己在 Go 端寫:先 `Permissions.List()` 查現有角色,比較高低,決定 `Create()` 還是 `Update()`。
- `chatSpaceId` 分享對象在公開 API/SDK 文件裡的對應方式,v1、v2 都**沒能完全確認**,列入第 12 節待驗證事項。

**11. `trash_file`** — `drive.NewFilesService(srv).Update(fileId, &drive.File{Trashed: true}).Do()`

### 6.4 登入與授權流程設計(改由 Go 層實作)

流程跟 v1 的設計邏輯相同,差異是**整個流程改在 Go 層用 `golang.org/x/oauth2`/`golang.org/x/oauth2/google` 實作**,C++ 端只呼叫一個 `gda_login()` FFI 函式:

1. C++ 呼叫 `gda_login(mode)`(`mode` 為 `"browser"` 或 `"device"`)。
2. Go 端產生 PKCE `code_verifier`/`code_challenge`,用 Go 標準庫 `net/http` 在 `127.0.0.1` 起一個暫時、單次連線的 HTTP server 接 OAuth callback(等同 v1 的 cpp-httplib 角色,現在用 Go stdlib,不必再額外引入 cpp-httplib 到 C++ 端)。
3. Go 端組好 Google 授權 URL,透過呼叫 `xdg-open`(或 C++ 端先行呼叫亦可,依實作分工決定)開瀏覽器;失敗則把網址透過 FFI 回傳給 C++ 印出來讓使用者手動開啟。
4. 使用者完成 Google 登入同意後,本機 server 收到 `code`,Go 端用 `code_verifier` 向 `https://oauth2.googleapis.com/token` 換取 `access_token`/`refresh_token`。
5. token 寫入 `~/.config/google-drive-agent/token.json`,權限 `0600`(Go 標準庫 `os.OpenFile` 搭配 `0600` mode)。
6. Headless 情境(`mode="device"`):Go 端呼叫 Google device code endpoint,印出 `user_code` 與驗證網址,背景輪詢直到授權完成。

**關於 refresh token 過期**:跟 v1 一樣——Google Cloud Console 的 OAuth consent screen 設為 "Testing" 狀態下 refresh token 約 7 天失效,切到 "Production" 狀態(不須完整送審)可避開這個限制;無論如何,Go 端對 refresh 失敗(`invalid_grant`)要有清楚的 fallback,引導使用者重新 `login`,不要假設 token 永久有效。

### 6.5 跨層關鍵機制

- **分頁**:Go SDK 的 `List()` builder 回傳的 `NextPageToken` 直接對應回 Tool Dispatcher 的 `pageToken` 參數。
- **錯誤與重試**:`google.golang.org/api` 底層的 HTTP transport 本身對部分錯誤已有重試機制,但建議仍在 `drive_client.go` 包一層針對 429/5xx 的 exponential backoff,並區分 403 是權限不足還是 rate limit(看 `googleapi.Error.Errors[].Reason`)。
- **大檔案上傳**:SDK 的 `Media()`/`ResumableMedia()` 已經處理 resumable upload 的分段與續傳,不需要像 v1 一樣自己刻 `Content-Range` 邏輯。
- **欄位最小化**:所有 SDK 呼叫鏈上 `.Fields(googleapi.Field(...))`,只回真正需要的欄位。

### 6.6 選配:本機 MCP Server Facade

跟 v1 一樣**不是必需品**,核心需求光靠前面幾層就完全達成。價值在於讓其他 MCP host(例如本機 Claude Desktop)把這個執行檔設成它們自己的 Google Drive MCP server。

設計要點(與 v1 相同,語言仍是 C++,只是現在呼叫的是 FFI 進 godrive,而不是直接呼叫 C++ 寫的 Drive 邏輯):
- 薄 adapter 把 Tool Dispatcher 的 11 個函式包成符合 MCP 2026-07-28 規格(stateless,`_meta` 帶版本/能力,無 `initialize` 握手)的 `tools/call` JSON-RPC handler。
- Transport 支援 `stdio` 與 `Streamable HTTP`(`Mcp-Method`/`Mcp-Name` header)。
- 因為官方規格變動頻繁,這層程式碼完全隔離成獨立模組,跟核心邏輯(無論是 C++ 或 Go 那一層)零耦合。
- 排入第 10 節路線圖的最後一個 Phase(可選)。

---

## 7. 專案架構(Project Architecture)

### 7.1 目錄結構

```
google-drive-agent/
├── CMakeLists.txt
├── README.txt
├── LICENSE
├── .gitignore
├── cmake/
│   └── GoDriveCore.cmake              (驅動 go build -buildmode=c-archive 的 CMake 模組)
├── godrive/                            (Go module,編譯成 c-archive,不是獨立執行檔)
│   ├── go.mod
│   ├── go.sum
│   ├── vendor/                         (`go mod vendor` 輸出,整個 commit 進 repo = "下載進專案")
│   ├── main.go                         (package main;c-archive 模式要求的空 func main(){})
│   ├── export_tools.go                 (//export 標註的 11 個工具 + login/logout FFI 函式)
│   ├── export_tools_test.go            (Go 慣例:_test.go 跟被測檔案放同一層,見第 8.3 節)
│   ├── drive_client.go                 (包 google.golang.org/api/drive/v3 的業務邏輯)
│   ├── drive_client_test.go
│   ├── query_translator.go
│   ├── query_translator_test.go
│   ├── oauth.go                        (golang.org/x/oauth2 登入流程)
│   ├── oauth_test.go
│   ├── token_store.go
│   ├── token_store_test.go
│   ├── internal/
│   │   └── testutil/
│   │       └── fixture_transport.go    (假的 http.RoundTripper,見第 8.4 節)
│   └── testdata/                       (Go 慣例目錄名,build 工具自動忽略,見第 8.3 節)
│       ├── drive_api_responses/
│       │   ├── files_list_basic.json
│       │   ├── files_list_page2.json
│       │   ├── files_get_metadata.json
│       │   ├── permissions_list.json
│       │   ├── files_create_response.json
│       │   └── error_403_rate_limit.json
│       ├── oauth_responses/
│       │   ├── token_exchange_success.json
│       │   └── token_refresh_invalid_grant.json
│       └── query_translation_cases.json (table-driven 測試案例,見第 8.3 節範例)
├── include/
│   └── gda/
│       ├── ffi/
│       │   └── godrive_bridge.hpp      (包一層 cgo 產生的 libgodrive.h)
│       ├── tools/
│       │   └── tool_dispatcher.hpp
│       └── mcp/                        (選配)
│           ├── jsonrpc.hpp
│           ├── transport_stdio.hpp
│           └── transport_http.hpp
├── src/
│   ├── ffi/godrive_bridge.cpp
│   ├── tools/tool_dispatcher.cpp
│   ├── mcp/                            (選配,對應 include/gda/mcp)
│   └── cli/
│       ├── main.cpp
│       └── commands.cpp
├── tests/                               (C++ 側測試,見第 8.2 節;Go 側測試留在 godrive/ 內,是 Go 慣例)
│   ├── CMakeLists.txt
│   └── cpp/
│       ├── test_tool_dispatcher.cpp
│       ├── test_ffi_bridge.cpp
│       ├── test_mcp_jsonrpc.cpp         (選配)
│       └── fixtures/
│           ├── ffi_responses/           (假的 IGodriveBridge 回傳值,JSON 格式)
│           │   ├── search_files_basic.json
│           │   ├── search_files_empty.json
│           │   └── get_file_metadata_doc.json
│           └── cli_golden/              (CLI 輸出格式的 golden file)
│               └── search_table_output.txt
└── third_party/                        (C++ 端 header-only 依賴的 FetchContent 快取)
```

### 7.2 CMakeLists.txt 草稿

> 版本號為示意草稿,實作時請查詢當下各 library 最新的 release/版本再填入。

```cmake
cmake_minimum_required(VERSION 3.20)
project(google-drive-agent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(GDA_BUILD_TESTS "建置單元測試" ON)
option(GDA_ENABLE_MCP_SERVER "建置選配的 MCP Server Facade(stdio/Streamable HTTP)" ON)

include(FetchContent)
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3)
FetchContent_Declare(cli11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG        v2.4.2)
FetchContent_MakeAvailable(nlohmann_json cli11)

if(GDA_ENABLE_MCP_SERVER)
  FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.15.3)
  FetchContent_MakeAvailable(httplib)
endif()

# ---- godrive: Go 官方 Drive SDK,編譯成 c-archive 靜態函式庫 ----
find_program(GO_EXECUTABLE go REQUIRED)

set(GODRIVE_DIR ${CMAKE_SOURCE_DIR}/godrive)
set(GODRIVE_ARCHIVE ${CMAKE_BINARY_DIR}/godrive/libgodrive.a)
set(GODRIVE_HEADER  ${CMAKE_BINARY_DIR}/godrive/libgodrive.h)

file(GLOB GODRIVE_SOURCES ${GODRIVE_DIR}/*.go)

add_custom_command(
  OUTPUT ${GODRIVE_ARCHIVE} ${GODRIVE_HEADER}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/godrive
  COMMAND ${CMAKE_COMMAND} -E env
          CGO_ENABLED=1 GOFLAGS=-mod=vendor GOPROXY=off
          ${GO_EXECUTABLE} build -buildmode=c-archive
          -o ${GODRIVE_ARCHIVE} .
  WORKING_DIRECTORY ${GODRIVE_DIR}
  DEPENDS ${GODRIVE_DIR}/go.mod ${GODRIVE_DIR}/go.sum ${GODRIVE_SOURCES}
  COMMENT "Building godrive core via cgo c-archive (vendored deps, no network at build time)"
)
add_custom_target(godrive_core DEPENDS ${GODRIVE_ARCHIVE})

add_library(godrive STATIC IMPORTED GLOBAL)
set_target_properties(godrive PROPERTIES IMPORTED_LOCATION ${GODRIVE_ARCHIVE})
add_dependencies(godrive godrive_core)

add_library(gda_core
  src/tools/tool_dispatcher.cpp
  src/ffi/godrive_bridge.cpp
)
target_include_directories(gda_core PUBLIC include ${CMAKE_BINARY_DIR}/godrive)
target_link_libraries(gda_core PUBLIC nlohmann_json::nlohmann_json godrive pthread dl)

if(GDA_ENABLE_MCP_SERVER)
  add_library(gda_mcp src/mcp/jsonrpc.cpp src/mcp/transport_stdio.cpp src/mcp/transport_http.cpp)
  target_link_libraries(gda_mcp PUBLIC gda_core httplib::httplib)
  target_compile_definitions(gda_core PUBLIC GDA_WITH_MCP_SERVER=1)
endif()

add_executable(gdrive-agent src/cli/main.cpp src/cli/commands.cpp)
target_link_libraries(gdrive-agent PRIVATE
  gda_core CLI11::CLI11
  $<$<BOOL:${GDA_ENABLE_MCP_SERVER}>:gda_mcp>
)

if(GDA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)             # 註冊 C++ (doctest) 測試,見第 8.2 節
  add_test(
    NAME godrive_go_tests              # 註冊 Go (testing+testify) 測試,見第 8.3 節
    COMMAND ${GO_EXECUTABLE} test -mod=vendor ./...
    WORKING_DIRECTORY ${GODRIVE_DIR}
  )
endif()

install(TARGETS gdrive-agent RUNTIME DESTINATION bin)
```

`pthread`、`dl` 是 Go c-archive 在 Linux 上常見需要一起連結的系統函式庫(Go runtime 本身用得到),純系統函式庫,不算「外部相依套件」。把 Go 測試也註冊成一個 CTest test 的好處是:`ctest`(在 build 目錄下執行)會一次跑完 C++ 跟 Go 兩側全部的單元測試,不需要記兩套指令。

### 7.3 README.txt 草稿

```
GOOGLE DRIVE AGENT
==================

一個自含(self-contained)的原生 Linux 命令列工具:C++ 外殼(CLI/
參數解析/選配的 MCP Server Facade)+ Google 官方 Go Drive client
library(google.golang.org/api/drive/v3,透過 cgo 編譯成靜態函式庫
直接連結進同一個執行檔)。不需要 Node.js / Python / 外部 MCP server
行程,執行期沒有任何外部套件或 service 依賴,提供與 Claude 裡 Google
Drive MCP 工具對等的操作:search, list recent, read, download,
get metadata, get permissions, create, update, copy, share, trash。

1. 需求(僅建置期需要,執行期不需要)
   - Linux(x86_64 / aarch64),glibc-based 散佈版
   - CMake >= 3.20
   - 支援 C++20 的編譯器(gcc >= 11 或 clang >= 14)
   - Go 工具鏈 >= 1.22(用來把 godrive/ 編譯成靜態函式庫)
   - 一組啟用了 Drive API 的 Google Cloud 專案,以及一個
     OAuth 2.0 "Desktop app" 類型的 Client ID(見第 3 節)

2. 編譯
   $ cmake -B build -DCMAKE_BUILD_TYPE=Release
   $ cmake --build build -j
   $ sudo cmake --install build

3. 第一次設定(Google Cloud OAuth client)
   - 到 Google Cloud Console 建立專案、啟用 Drive API
   - 建立 OAuth 2.0 Client ID,類型選 "Desktop app"
   - 把 client_id / client_secret 存到
     ~/.config/google-drive-agent/client.json

4. 登入
   $ gdrive-agent login
   (沒有瀏覽器的環境可用 --device 走 device code 流程)

5. 使用範例
   $ gdrive-agent search "title contains 'invoice' and starred = true"
   $ gdrive-agent list-recent --limit 20
   $ gdrive-agent download <fileId> -o out.pdf
   $ gdrive-agent upload ./report.pdf --parent <folderId>
   $ gdrive-agent share <fileId> --email alice@example.com --role writer
   $ gdrive-agent trash <fileId>

6. 選配:當 MCP Server 用
   $ gdrive-agent serve-mcp --transport stdio
   (讓其他 MCP-compatible host,例如本機 Claude Desktop,把這個
   執行檔設成它的 Google Drive MCP server)

7. 安全性注意事項
   - OAuth token 存在 ~/.config/google-drive-agent/token.json,
     權限 600;執行 `gdrive-agent logout` 可清除本機 token
     (真正撤銷授權仍需到 Google 帳號的第三方存取權限頁面操作)
   - 本工具要求完整的 `drive` scope(而非 `drive.file`),因為
     需要搜尋/管理既有檔案,不只是自己建立的檔案;請妥善保管
     本機 token 檔案

8. 第三方程式碼
   - google.golang.org/api(BSD-3-Clause)、golang.org/x/oauth2
     (BSD-3-Clause)原始碼 vendor 於 godrive/vendor/
   - nlohmann/json、CLI11(選配功能另含 cpp-httplib)於建置時下載

9. 授權(LICENSE)
   (依專案選擇的授權條款填寫)
```

### 7.4 本地建置指令

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/gdrive-agent login
```

### 7.5 godrive/ Go module 初始化範例

```
cd godrive
go mod init google-drive-agent/godrive
go get google.golang.org/api/drive/v3
go get golang.org/x/oauth2/google
go mod vendor      # 把依賴原始碼實際下載進 vendor/,commit 進 repo
```

---

## 8. 測試策略(Unit Testing)

這個專案橫跨 C++ 與 Go 兩個語言,兩側各有語言自己的慣例,**不強行統一成一套工具**,但統一用 CMake/CTest 當最上層的「一個指令跑完全部測試」入口。

### 8.1 工具選擇與理由

| 側 | 工具 | 理由 |
|---|---|---|
| C++(`tests/cpp/`) | **doctest**(斷言/測試框架) | header-only、單檔引入,編譯速度比 Catch2 快很多;功能(`TEST_CASE`、`SUBCASE`、`CHECK`/`REQUIRE`)對這個專案的需求(驗證 JSON 轉換、CLI 格式化輸出)完全足夠,不需要 Catch2 的 BDD 風格或進階 matcher。CMake 整合用 doctest 官方附的 `doctest.cmake`(`doctest_discover_tests`),每個 `TEST_CASE` 自動變成獨立的 CTest 測試項目。 |
| Go(`godrive/*_test.go`) | **標準庫 `testing`** + **`testify`**(`assert`/`require`) | `testing` 是 `go test` 本身要求的基礎,無法替代;`testify` 是 Go 生態最廣泛使用的斷言/輔助庫(MIT 授權),讓斷言寫法更簡潔(`assert.Equal`、`require.NoError`),且跟專案現有的「用 `go mod vendor` 下載依賴進專案」慣例一致——`testify` 本身也會被 vendor 進 `godrive/vendor/`。 |

> 以上版本號、工具名稱都是目前(2026-10)的現況,實作時建議再確認一次版本 tag。

### 8.2 C++ 側:測試位置與 fixture 慣例

- 測試原始碼:`tests/cpp/*.cpp`,一個被測模組對應一個 `test_*.cpp`(例如 `test_tool_dispatcher.cpp`、`test_ffi_bridge.cpp`)。
- **Fixture / case 檔案位置:`tests/cpp/fixtures/`**,底下再依用途分兩類子目錄:
  - `fixtures/ffi_responses/`:模擬 godrive(Go c-archive)透過 FFI 回傳的 JSON 字串。例如 `search_files_basic.json`:
    ```json
    {
      "files": [
        {"id": "1abc", "name": "Q3 Report.pdf", "mimeType": "application/pdf"},
        {"id": "2def", "name": "Q3 Report - copy.pdf", "mimeType": "application/pdf"}
      ],
      "nextPageToken": null
    }
    ```
  - `fixtures/cli_golden/`:CLI 指令格式化後「應該長什麼樣子」的 golden file(純文字),測試時把實際輸出跟這個檔案逐字比對。
- **可測試性的關鍵設計**:`ToolDispatcher` 不直接呼叫 cgo 匯出的 C 函式,而是依賴一個介面:

  ```cpp
  // include/gda/ffi/godrive_bridge.hpp
  class IGodriveBridge {
  public:
    virtual ~IGodriveBridge() = default;
    virtual std::string SearchFiles(const std::string& argsJson) = 0;
    virtual std::string ListRecentFiles(const std::string& argsJson) = 0;
    // ...其餘對應 11 個工具 + Login/Logout
  };

  class GodriveBridge final : public IGodriveBridge { /* 真正呼叫 gda_search_files() 等 cgo 匯出函式 */ };
  ```

  正式執行時,`main.cpp` 建立真正的 `GodriveBridge`;測試時建立一個 `FakeGodriveBridge`,從 `fixtures/ffi_responses/` 讀檔案回傳,**完全不需要編譯/連結 godrive 這個 Go c-archive,也不需要真正的 Google 帳號**。範例:

  ```cpp
  TEST_CASE("search command formats two results as a table") {
    FakeGodriveBridge fake;
    fake.LoadFixture(std::string(GDA_TEST_FIXTURES_DIR) + "/ffi_responses/search_files_basic.json");
    ToolDispatcher dispatcher(fake);
    auto result = dispatcher.SearchFiles(R"({"query":"title contains 'Q3'"})");
    CHECK(result.files.size() == 2);
  }
  ```

  `GDA_TEST_FIXTURES_DIR` 由 `tests/CMakeLists.txt` 透過 `target_compile_definitions` 注入成 fixtures 的絕對路徑,避免測試結果跟著「從哪個目錄執行 ctest」而改變。

- `tests/CMakeLists.txt` 草稿:

  ```cmake
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11)
  FetchContent_MakeAvailable(doctest)

  add_executable(gda_tests
    cpp/test_tool_dispatcher.cpp
    cpp/test_ffi_bridge.cpp
    cpp/test_mcp_jsonrpc.cpp
  )
  target_link_libraries(gda_tests PRIVATE gda_core doctest::doctest)
  target_compile_definitions(gda_tests PRIVATE
    GDA_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/cpp/fixtures")

  include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
  doctest_discover_tests(gda_tests)
  ```

### 8.3 Go 側:測試位置與 testdata 慣例

Go 有自己明確的慣例,**不要套用 C++ 那套 `tests/` 目錄結構**:

- 測試檔案必須命名為 `*_test.go`,而且要跟被測的 `.go` 檔案放在**同一個目錄**(例如 `drive_client.go` 配 `drive_client_test.go`),這是 `go test` 工具鏈本身的規則,不是我們自訂的慣例。
- **Fixture / case 檔案位置:`godrive/testdata/`**——`testdata` 是 Go 工具鏈特別認可、會自動從 build/vet 掃描中排除的目錄名稱,是 Go 社群公認「放測試資料」的標準位置。
- 建議的具體用法——用一個外部 JSON case 檔驅動 table-driven test,之後**加測試案例只要加一行 JSON,不用改程式碼**:

  `godrive/testdata/query_translation_cases.json`:
  ```json
  [
    {"name": "title_contains", "mcp_query": "title contains 'report'", "expected_q": "name contains 'report'"},
    {"name": "parent_equals",  "mcp_query": "parentId = '1234'",       "expected_q": "'1234' in parents"},
    {"name": "owner_me",       "mcp_query": "owner = 'me'",            "expected_q": "'me' in owners"}
  ]
  ```

  `godrive/query_translator_test.go`:
  ```go
  package godrive

  import (
    "encoding/json"
    "os"
    "testing"
  )

  type queryCase struct {
    Name      string `json:"name"`
    MCPQuery  string `json:"mcp_query"`
    ExpectedQ string `json:"expected_q"`
  }

  func TestTranslateQuery_TableDriven(t *testing.T) {
    data, err := os.ReadFile("testdata/query_translation_cases.json")
    if err != nil {
      t.Fatal(err)
    }
    var cases []queryCase
    if err := json.Unmarshal(data, &cases); err != nil {
      t.Fatal(err)
    }
    for _, c := range cases {
      t.Run(c.Name, func(t *testing.T) {
        got, err := TranslateQuery(c.MCPQuery)
        if err != nil {
          t.Fatalf("unexpected error: %v", err)
        }
        if got != c.ExpectedQ {
          t.Errorf("TranslateQuery(%q) = %q, want %q", c.MCPQuery, got, c.ExpectedQ)
        }
      })
    }
  }
  ```

### 8.4 怎麼在不打真實 Google API 的情況下測試 `drive_client.go`

`google.golang.org/api` 的每個 service(`drive.NewService(...)`)都接受自訂的 `*http.Client`(透過 `option.WithHTTPClient(...)`),所以測試時可以塞一個假的 `http.RoundTripper`,直接從 `testdata/drive_api_responses/*.json` 讀出「假裝是 Google 回的」JSON,完全不連網、不需要真實帳號或 OAuth token:

```go
// godrive/internal/testutil/fixture_transport.go
package testutil

type FixtureTransport struct {
  T       *testing.T
  Fixture string // 例如 "testdata/drive_api_responses/files_list_basic.json"
  Status  int    // 0 表示 200
}

func (f *FixtureTransport) RoundTrip(req *http.Request) (*http.Response, error) {
  body, err := os.ReadFile(f.Fixture)
  if err != nil {
    f.T.Fatalf("missing fixture %s: %v", f.Fixture, err)
  }
  status := f.Status
  if status == 0 {
    status = 200
  }
  return &http.Response{
    StatusCode: status,
    Body:       io.NopCloser(bytes.NewReader(body)),
    Header:     http.Header{"Content-Type": []string{"application/json"}},
  }, nil
}

func NewTestDriveService(t *testing.T, fixture string) *drive.Service {
  t.Helper()
  client := &http.Client{Transport: &FixtureTransport{T: t, Fixture: fixture}}
  srv, err := drive.NewService(context.Background(), option.WithHTTPClient(client))
  if err != nil {
    t.Fatal(err)
  }
  return srv
}
```

`drive_client_test.go` 裡的用法:

```go
func TestSearchFiles(t *testing.T) {
  srv := testutil.NewTestDriveService(t, "testdata/drive_api_responses/files_list_basic.json")
  got, err := SearchFiles(srv, "title contains 'report'")
  require.NoError(t, err)
  assert.Len(t, got.Files, 2)
}
```

錯誤情境(429/403/`invalid_grant`)的重試與 fallback 邏輯,也用同一個機制測試——`testdata/drive_api_responses/error_403_rate_limit.json` 存一份模擬的錯誤回應,`FixtureTransport.Status` 設成對應的 HTTP 狀態碼即可,不需要真的打到 Google 才能觸發 429。

### 8.5 整合測試(選配,預設不跑)

上面全部都是不連網、不需要真實帳號的單元測試。如果之後想加「真的打一次 Google Drive API,確認整個 OAuth + SDK 串起來沒問題」的整合測試:

- 放在獨立的 `tests/integration/`(C++)或 `godrive/integration_test.go`(Go,檔名建議加 `//go:build integration` build tag)。
- 預設**不**被 `ctest`/`go test ./...` 執行到——Go 用 build tag(`go test -tags=integration ./...` 才會跑),C++ 用一個獨立的 CMake target(不掛在 `GDA_BUILD_TESTS` 底下的預設測試列表)。
- 需要真實的 OAuth client 設定與一個測試用 Google 帳號,憑證透過環境變數(例如 `GDA_TEST_REFRESH_TOKEN`)注入,絕對不要把真實憑證寫進 repo。

### 8.6 一個指令跑完全部測試

```
cmake -B build -DGDA_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

`ctest` 會依照第 7.2 節 CMakeLists.txt 的設定,同時跑 C++ 的 `gda_tests`(doctest)跟 Go 的 `godrive_go_tests`(`go test ./...`),兩側都通過才算整體測試通過。

---

## 9. CLI 指令設計

| 指令 | 說明 |
|---|---|
| `gdrive-agent login [--device]` | OAuth2 登入;`--device` 走 headless device code 流程 |
| `gdrive-agent logout` | 清除本機 token |
| `gdrive-agent search "<query>"` | 對應 `search_files` |
| `gdrive-agent list-recent [--order-by X] [--limit N]` | 對應 `list_recent_files` |
| `gdrive-agent metadata <fileId>` | 對應 `get_file_metadata` |
| `gdrive-agent permissions <fileId>` | 對應 `get_file_permissions` |
| `gdrive-agent read <fileId> [--with-comments]` | 對應 `read_file_content` |
| `gdrive-agent download <fileId> [--export-mime-type X] [--revision ID] -o <path>` | 對應 `download_file_content` |
| `gdrive-agent upload <local_path> [--title X] [--parent ID] [--no-convert]` | 對應 `create_file` |
| `gdrive-agent create-doc --title X --type doc\|sheet\|slide\|folder [--parent ID]` | 對應 `create_file`(無內容版) |
| `gdrive-agent update <fileId> [--title X] [--parent ID] [--description X]` | 對應 `update_file` |
| `gdrive-agent copy <fileId> [--title X] [--parent ID]` | 對應 `copy_file` |
| `gdrive-agent share <fileId> --email X --role writer\|commenter\|reader` | 對應 `share_file` |
| `gdrive-agent trash <fileId>` | 對應 `trash_file` |
| `gdrive-agent serve-mcp [--transport stdio\|http] [--port N]` | 選配,見 6.6 節 |

---

## 10. 開發階段與里程碑

> 每個 Phase 都應該包含對應的單元測試(C++ 用 doctest、Go 用 `testing`+`testify`,見第 8 節),不是最後才補——尤其第 6.3 節的 query DSL 翻譯表,建議一開始就寫成第 8.3 節那種 table-driven 的 `testdata/query_translation_cases.json`,邊做邊加案例。

- **Phase 0**:`godrive/` Go module 初始化、`go mod vendor`、CMake 的 `GoDriveCore.cmake` 整合、C++ 外殼骨架、`login`/`logout`
- **Phase 1(唯讀能力)**:`search_files`、`list_recent_files`、`get_file_metadata`、`get_file_permissions`、`download_file_content`、Google 原生類型的 `read_file_content`
- **Phase 2(寫入能力)**:`create_file`(SDK 內建 upload/resumable)、`update_file`、`copy_file`
- **Phase 3(協作能力)**:`share_file`(含角色高低比較邏輯)、`trash_file`
- **Phase 4(體驗強化)**:CLI 輸出格式化、重試機制打磨、PDF/Office `read_file_content`(視實際需求決定是否要做)
- **Phase 5(選配)**:MCP Server Facade(stdio + Streamable HTTP)

---

## 11. 安全與隱私考量

- **OAuth client_id/secret**:對 "Desktop app" 類型,Google 官方立場是這組 secret「不是真正的機密」,但仍建議不要直接 commit 進公開 repo——放進 `~/.config/google-drive-agent/client.json`,或用環境變數/build-time 注入。
- **Refresh token**:存在 `~/.config/google-drive-agent/token.json`,檔案權限 `600`;`gdrive-agent logout` 只清本機檔案,真正撤銷授權要到 Google 帳號的「第三方存取權限」頁面手動操作。
- **Scope 取捨**:因為要「搜尋/管理任何既有檔案」而非只管理這個程式自己建立的檔案,必須用完整的 `https://www.googleapis.com/auth/drive` scope。這是敏感權限,純個人使用沒問題,但這個 token 一旦外洩,等同整個 Drive 的完整讀寫權限都被拿走。
- 這個 app 不應該、也不會把 OAuth token、檔案內容等任何東西送到 Google 官方 API 網域以外的任何伺服器。
- **vendor 進專案的第三方原始碼**(`godrive/vendor/` 下的 `google.golang.org/api`、`golang.org/x/oauth2`)建議定期手動更新(`go get -u` + 重新 `go mod vendor`),因為是靜態編譯進執行檔,不會自動收到上游的安全性修補。

---

## 12. 風險、未知與待驗證事項

1. **cgo c-archive 與 C++ `main()` 整合的連結細節**,需要在 Phase 0 用一個最小範例先驗證過(空的 `//export` 函式,確認 CMake 能順利建置、連結、呼叫),再開始寫實際業務邏輯,避免後期才發現整合有問題。
2. **MCP 規格仍在快速變動**(2025-11-25 → 2026-07-28 就有一次破壞性改版),選配的 MCP Server Facade 要有心理準備之後還要再改一次。
3. `share_file` 的 `chatSpaceId` 分享對象,在公開 Drive API v3 / Go SDK 文件裡的對應方式,這次規劃**沒能完全確認**,實作前需要額外查證,或先只支援 `emailAddress` 分支。
4. `read_file_content` 要做到跟 Anthropic 版本一樣支援 PDF / MS Office / 圖片的「自然語言文字化」,工作量遠大於其他 10 個工具,務必明確分期,不要卡在 v1。
5. Google OAuth "Testing" vs "Production" 狀態對 refresh token 存活時間的確切規則,查到的二手資料彼此不完全一致,實作時請以 Google 當下的官方 Auth Platform 文件為準,並確保程式對「refresh 失敗」有乾淨的 fallback。
6. `google.golang.org/api` 處於 maintenance mode,長期來看 Google 可能推出新一代 client library(研究時看到的訊號是 Google 把資源往「Cloud Client Libraries」方向引導,但那是給 GCP 基礎設施服務用的,不包含 Drive/Workspace API);對 Drive v3 這種穩定介面,maintenance mode 風險評估為低,但建議每隔一段時間檢查官方公告。

---

## 13. 下一步建議

這份文件目前只是規劃書,還沒有建立任何程式碼、`CMakeLists.txt`、`README.txt` 或 Go module 的實體檔案(本文件第 7.2、7.3、7.5 節的內容是給你複製使用的草稿)。如果方向確認沒問題,下一步可以請我:

1. 先做 Phase 0 的「最小驗證」:一個空的 Go c-archive + 一個 C++ `main()` 呼叫它,確認 CMake 整合真的順利建置、連結、執行,把第 12 節風險 1 排除掉;或
2. 直接建立完整目錄骨架,並從 Phase 0 + Phase 1(登入 + 唯讀操作)開始寫。

---

## 14. 附錄:參考資料

- [MCP 2026-07-28 Specification 發布說明](https://blog.modelcontextprotocol.io/posts/2026-07-28/)
- [MCP 2026-07-28 Transports 規格](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports)
- [MCP 官方 reference servers repo](https://github.com/modelcontextprotocol/servers)
- [Node.js embedder API 文件(embedding.json)](https://r2.nodejs.org/docs/v14.19.0/api/embedding.json)
- [Node.js 靜態嵌入遇到 V8 undefined reference 的討論](https://github.com/orgs/nodejs/discussions/35796)
- [Node.js Single Executable Applications 文件](https://nodejs.org/download/release/v26.7.0/docs/api/single-executable-applications.html)
- [Embedding Python in a C++ self-contained executable(python-build-standalone 討論)](https://discuss.python.org/t/embedding-python-in-a-c-self-contained-executable/25677)
- [pybind11 embedding 文件](https://pigweed.googlesource.com/third_party/github/pybind/pybind11/+/1d3436ada08b549ec6f9cf3dbd93b8a531bd04cd/docs/advanced/embedding.rst)
- [google.golang.org/api/drive/v3 官方文件](https://pkg.go.dev/google.golang.org/api/drive/v3)
- [Go cgo c-archive 與 C++ 互叫範例(go-interlang)](https://redirect.github.com/draffensperger/go-interlang)
- [Go cgo c-archive 重複 symbol 問題討論(golang/go#20639)](https://github.com/golang/go/issues/20639)
- [Google OAuth2 Installed Applications(loopback flow,舊版鏡像文件)](https://chromium.googlesource.com/external/github.com/google/google-api-python-client/+/HEAD/docs/oauth-installed.md)
- [Google OAuth Testing→Production token 過期問題討論(二手來源,建議以 Google 官方文件覆核)](https://tech.sailjada.com/posts/2026-05-06-2046.html)
