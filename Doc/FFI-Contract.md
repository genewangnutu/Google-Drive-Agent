# godrive ↔ C++ FFI Contract

這份文件是 `godrive`(Go,module 1)與 FFI Bridge(C++,module 2,`src/ffi/godrive_bridge.cpp`)
兩個 sub-agent 之間**唯一**的共享真相來源。兩邊都必須完全照這份文件實作,不要各自發明函式名稱或
JSON 欄位名稱——否則兩個模組各自測試都會過,但整合時會對不起來。

對應規劃書:`Doc/Google-Drive-Agent-Plan.md` 第 6.1、6.2、6.3、8.4 節。

## 1. 總體規則

- 每個 `//export` 函式都是 **C ABI**(`extern "C"`),所有輸入/輸出都是 **JSON 字串**(`char*`,
  UTF-8,null-terminated)。沒有例外、沒有二進位結構傳遞。
- 每個回傳 `char*` 的函式,那個指標的記憶體是 **Go 配置的**。C++ 端用完以後必須呼叫
  `gda_free_string()` 釋放,不能用 C++ 的 `free()`/`delete` 直接處理。
- 除了 `gda_free_string` 以外,**所有函式都回傳同一個 envelope 結構**(不管成功或失敗):

  成功:
  ```json
  {"ok": true, "data": { ...依函式不同... }}
  ```

  失敗:
  ```json
  {"ok": false, "error": {
    "code": "UNAUTHENTICATED | NOT_FOUND | PERMISSION_DENIED | RATE_LIMITED | INVALID_ARGUMENT | INTERNAL",
    "http_status": 0,
    "message": "human readable string, 可直接印給使用者看",
    "retryable": false
  }}
  ```

  `code` 固定是上面六個字串之一(Go 端判斷用,`googleapi.Error.Code`/`.Errors[].Reason` 映射過去)。
  `http_status` 是 Google API 回的原始 HTTP 狀態碼,查不到時填 `0`。這個 envelope 設計的理由:
  C++ 端(Tool Dispatcher)永遠先檢查 `ok`,不需要針對每個函式各自判斷「怎樣算錯誤」。
- Go 端的 `//export` 函式本身**不應該 panic**——任何 error 都要被攔截、轉換成上面的失敗 envelope
  再回傳,panic 會直接弄掛整個行程(包含呼叫它的 C++ 那一側)。
- 字串裡所有欄位名稱都是 **camelCase**,跟這個 session 的 MCP 工具 schema 保持一致(例如
  `fileId`、`pageSize`、`parentId`),不要在 Go 端轉成 Go 慣例的 `FileId`/`PageSize` 這種 JSON key。

## 2. 匯出函式清單(godrive 必須實作、FFI Bridge 必須呼叫,一個不能少也不能多)

| # | 函式簽章(cgo `//export`) | 對應 MCP 工具 / `IGodriveBridge` 方法 |
|---|---|---|
| 1 | `char* gda_login(const char* argsJson)` | (無對應 MCP 工具;CLI `login` 指令用) |
| 2 | `char* gda_logout(void)` | (無對應 MCP 工具;CLI `logout` 指令用) |
| 3 | `char* gda_search_files(const char* argsJson)` | `search_files` / `SearchFiles` |
| 4 | `char* gda_list_recent_files(const char* argsJson)` | `list_recent_files` / `ListRecentFiles` |
| 5 | `char* gda_get_file_metadata(const char* argsJson)` | `get_file_metadata` / `GetFileMetadata` |
| 6 | `char* gda_get_file_permissions(const char* argsJson)` | `get_file_permissions` / `GetFilePermissions` |
| 7 | `char* gda_read_file_content(const char* argsJson)` | `read_file_content` / `ReadFileContent` |
| 8 | `char* gda_download_file_content(const char* argsJson)` | `download_file_content` / `DownloadFileContent` |
| 9 | `char* gda_create_file(const char* argsJson)` | `create_file` / `CreateFile` |
| 10 | `char* gda_update_file(const char* argsJson)` | `update_file` / `UpdateFile` |
| 11 | `char* gda_copy_file(const char* argsJson)` | `copy_file` / `CopyFile` |
| 12 | `char* gda_share_file(const char* argsJson)` | `share_file` / `ShareFile` |
| 13 | `char* gda_trash_file(const char* argsJson)` | `trash_file` / `TrashFile` |
| 14 | `void gda_free_string(char* s)` | (記憶體管理,無對應工具) |

全部宣告在 Go 端的 `godrive/export_tools.go`。cgo 會自動產生 `libgodrive.h`(build 產物,不 commit,
見 `.gitignore`),FFI Bridge 的 `src/ffi/godrive_bridge.cpp` `#include` 這個產生出來的 header。

## 3. 每個函式的 `data` / 參數形狀

以下只定義 `argsJson` 的欄位,跟成功時 `data` 裡的欄位——欄位名稱、型態是兩個 sub-agent 共同的
契約,不可更改;如果實作中發現欄位不夠用,先回報整合負責人(主 agent),不要單方面加欄位。

### 3.1 `gda_login`
- args: `{"mode": "browser" | "device"}`
- data: `{"status": "logged_in"}`
- 備註:`device` 模式下,Go 端可以直接用 `fmt.Println`/`fmt.Fprintln(os.Stderr, ...)` 把
  `user_code` 和驗證網址印到 process 自己的 stdout/stderr 再繼續背景輪詢——因為 C++ 跟 Go 是同一個
  process,使用者在同一個終端機就看得到,**不需要**把這些中間狀態也塞進回傳的 JSON 裡。函式本身
  blocking 直到使用者完成授權或逾時失敗(逾時回傳失敗 envelope,`code="INTERNAL"`)。

### 3.2 `gda_logout`
- args:(無,呼叫時仍傳 `"{}"` 避免空指標問題)
- data: `{"status": "logged_out"}`

### 3.3 `gda_search_files`
- args: `{"query": "string (MCP query DSL,規劃書 6.3 節的翻譯表)", "pageSize": 20, "pageToken": "string|null"}`
- data: `{"files": [ <FileObject>, ... ], "nextPageToken": "string|null"}`

### 3.4 `gda_list_recent_files`
- args: `{"orderBy": "recency" | "lastModified" | "lastModifiedByMe", "pageSize": 20, "pageToken": "string|null"}`
- data: 跟 3.3 相同形狀。

### 3.5 `gda_get_file_metadata`
- args: `{"fileId": "string"}`
- data: `<FileObject>`(見 3.9)

### 3.6 `gda_get_file_permissions`
- args: `{"fileId": "string"}`
- data: `{"permissions": [ {"id":"...", "type":"user|group|domain|anyone", "role":"owner|writer|commenter|reader", "emailAddress":"string|null"}, ... ]}`

### 3.7 `gda_read_file_content`
- args: `{"fileId": "string", "includeComments": false}`
- data: `{"content": "string", "mimeType": "string", "comments": [ {"id":"...", "author":"...", "content":"...", "createdTime":"..."} ] | null}`
- PDF/Office/圖片的自然語言文字化屬於 Phase 4+(規劃書第 10 節),v1 的 `godrive` 實作對這些類型可以
  回傳失敗 envelope `{"code":"INVALID_ARGUMENT","message":"unsupported mimeType for text extraction yet"}`,
  不算違反契約。

### 3.8 `gda_download_file_content`
- args: `{"fileId": "string", "exportMimeType": "string|null", "revisionId": "string|null"}`
- data: `{"base64Content": "string (base64)", "mimeType": "string", "filename": "string"}`

### 3.9 `gda_create_file`
- args: `{"title": "string", "parentId": "string|null", "textContent": "string|null", "base64Content": "string|null", "contentMimeType": "string|null", "disableConversionToGoogleType": false}`
- data: `<FileObject>`,其中 `<FileObject>` 的最小欄位集合是:
  ```json
  {"id":"string","name":"string","mimeType":"string","parents":["string"],
   "createdTime":"RFC3339","modifiedTime":"RFC3339","trashed":false,
   "webViewLink":"string|null","size":"string|null"}
  ```

### 3.10 `gda_update_file`
- args: `{"fileId": "string", "title": "string|null", "parentId": "string|null", "description": "string|null"}`
- data: `<FileObject>`(更新後的狀態)

### 3.11 `gda_copy_file`
- args: `{"fileId": "string", "title": "string|null", "parentId": "string|null"}`
- data: `<FileObject>`(新複本)

### 3.12 `gda_share_file`
- args: `{"fileId": "string", "role": "writer" | "commenter" | "reader", "emailAddress": "string|null", "chatSpaceId": "string|null"}`
- data: `{"id":"string","type":"user","role":"string","emailAddress":"string|null"}`
- `chatSpaceId` 分支:規劃書第 12 節列為待驗證事項。godrive 端如果查不到對應的公開 API 做法,
  v1 可以先回失敗 envelope(`code="INVALID_ARGUMENT"`, message 說明 chatSpaceId 分享尚未支援),
  FFI Bridge / Tool Dispatcher 不需要特殊處理這個失敗以外的案例。

### 3.13 `gda_trash_file`
- args: `{"fileId": "string"}`
- data: `{"id": "string", "trashed": true}`

## 4. 誰負責什麼(避免兩邊重工或漏工)

- **godrive(module 1,Go)**:實作上面 14 個函式的*行為*——呼叫 `google.golang.org/api/drive/v3`、
  處理 OAuth、把 Go 的 `drive.File` 等型別轉成上面定義的 JSON 形狀、把 `googleapi.Error` 轉成
  envelope 的 `error.code`/`http_status`/`retryable`。
- **FFI Bridge(module 2,C++)**:`GodriveBridge` 類別的每個方法=呼叫對應 cgo 函式、把回傳的
  `char*` 轉成 `std::string`、呼叫 `gda_free_string` 釋放、直接把整個 JSON 字串(含 envelope)原樣
  往上交給 `ToolDispatcher`——**不要**在這一層解析 `ok`/`error`,那是 Tool Dispatcher(module 3)
  的責任(它才知道怎麼把失敗轉成 `ToolError` 例外,見 `include/gda/tools/tool_dispatcher.hpp`)。
