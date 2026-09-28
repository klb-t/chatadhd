# Loom Android shell

The Android app for Loom/ChatADHD: a thin Kotlin shell (one Activity, one
WebView) hosting the web UI built in `loom/web` and a JNI bridge
(`window.LoomBridge`) onto the Loom C ABI (`loom/include/loom/loom.h`).
Nobody outside this directory needs to know Loom exists — the web UI talks
to `window.LoomBridge` the same way it would talk to `loom/server`'s HTTP
API (that adapter is `loom/web/src/api/loom-jni.ts`, owned by a different
agent; this file is the contract between the two).

## Layout

```
loom/android/
  settings.gradle.kts, build.gradle.kts, gradle.properties   root Gradle project
  app/
    build.gradle.kts            applicationId com.chatadhd.android, minSdk 26
    src/main/
      AndroidManifest.xml       INTERNET + storage permissions (see below)
      java/com/chatadhd/android/
        MainActivity.kt         hardened WebView, permission flow
        LoomBridge.kt           window.LoomBridge (the @JavascriptInterface surface)
        LoomNative.kt           external fun declarations onto libloom_jni.so
        LoomCallbacks.kt        native -> JS chunk/event delivery
        LoomHttp.kt             HttpURLConnection platform HTTP transport
      cpp/
        CMakeLists.txt          add_subdirectory(loom core), builds loom_jni.so
        loom_jni.cpp            the JNI glue (see its own file header)
      assets/web/               `copyWebAssets` Gradle task syncs loom/web/dist here
      res/                      minimal theme/icon (vector, no binary assets)
  verify/                       host-only JNI smoke test, no Android SDK needed
```

## The bridge contract

`MainActivity` adds one object as `window.LoomBridge` on a WebView that only
ever loads our own asset origin (see "WebView hardening" below). Every
method takes/returns **JSON strings** (never parsed JS objects) — the same
convention `loom.h` itself uses. `LoomBridge.kt` is the source of truth;
this table is a map of it.

### Fast, synchronous calls

These call straight into `LoomNative.nativeInvoke(method, argsJson)`, which
dispatches to one `loom_*` C ABI function each (see `loom_jni.cpp`'s
`dispatch_table()`) and returns its result (or `{"error":{"code","message"}}`)
directly. Safe to call from JS without a callback — they're local
SQLite/JSON operations and the WebView already runs `@JavascriptInterface`
calls off the render thread.

| JS method | args | loom.h function |
|---|---|---|
| `info()` | | `loom_info` |
| `getConfig()` | | `loom_get_config` |
| `setConfig(key, value)` | string, string | `loom_set_config` |
| `setConfigJson(patchJson)` | string | `loom_set_config_json` |
| `setSecret(key, value)` | string, string | `loom_set_secret` |
| `hasSecret(key)` | string | `loom_has_secret` -> `{"value":bool}` |
| `deleteSecret(key)` | string | `loom_delete_secret` |
| `listSecretKeys()` | | `loom_list_secret_keys` |
| `listConversations(limit)` | int | `loom_list_conversations` |
| `createConversation(title)` | string? | `loom_create_conversation` |
| `getConversation(convId)` | string | `loom_get_conversation` |
| `updateConversation(convId, patchJson)` | string, string | `loom_update_conversation` |
| `deleteConversation(convId)` | string | `loom_delete_conversation` |
| `getMessages(convId)` | string | `loom_get_messages` |
| `getMessagesEx(convId, includeAll)` | string, bool | `loom_get_messages_ex` |
| `getMessage(msgId)` | string | `loom_get_message` |
| `editMessage(msgId, newText)` | string, string | `loom_edit_message` |
| `restoreVersion(msgId)` | string | `loom_restore_version` |
| `getVersions(msgOrGroupId)` | string | `loom_get_versions` |
| `setMessageStatus(msgId, status)` | string, string | `loom_set_message_status` |
| `updateMessage(msgId, patchJson)` | string, string | `loom_update_message` |
| `search(query, optionsJson)` | string, string? | `loom_search` |
| `chatCancel(requestId)` | string | `loom_chat_cancel` |
| `getModels()` | | `loom_get_models` |
| `getProviders()` | | `loom_get_providers` |
| `can(resource, capability, constraintsJson)` | string, string, string? | `loom_can` -> `{"value":bool}` |
| `getNodes(filterJson)` | string? | `loom_get_nodes` |
| `getEdges(filterJson)` | string? | `loom_get_edges` |
| `expandGraph(seedIdsJson, depth)` | string, int | `loom_expand_graph` |
| `getGraphData(convId)` | string? | `loom_get_graph_data` |
| `selectContext(text, depth, maxTokens)` | string, int, int | `loom_select_context` |
| `selectContextEx(requestJson)` | string | `loom_select_context_ex` |
| `semanticStatus()` | | `loom_semantic_status` |
| `semanticPause()` / `semanticResume()` / `semanticWake()` | | `loom_semantic_*` |
| `listMemory()` | | `loom_list_memory` |
| `createMemory(json)` | string | `loom_create_memory` |
| `updateMemory(nodeId, json)` | string, string | `loom_update_memory` |
| `deleteMemory(nodeId)` | string | `loom_delete_memory` |
| `getMemoryContext(maxChars)` | int | `loom_get_memory_context` |
| `detectFormat(path)` | string | `loom_detect_format` |
| `listSources(limit)` | int | `loom_list_sources` |
| `getProvenance(subjectId)` | string | `loom_get_provenance` |
| `queryEvents(queryJson)` | string? | `loom_query_events` |
| `listTasks(filterJson)` | string? | `loom_list_tasks` |
| `getTask(taskId)` | string | `loom_get_task` |
| `resumeTasks()` | | `loom_resume_tasks` |
| `cancelTask(taskId)` | string | `loom_cancel_task` |
| `cryptoStatus()` | | `loom_crypto_status` |
| `cryptoSetup(password)` / `cryptoUnlock(password)` | string | `loom_crypto_setup` / `_unlock` |
| `cryptoLock()` | | `loom_crypto_lock` |
| `cryptoEncrypt(plaintext)` | string | `loom_crypto_encrypt` |
| `cryptoDecrypt(blobJson)` | string | `loom_crypto_decrypt` |
| `mediaStatus()` | | `loom_media_status` |
| `unsubscribe(token)` | string (int64) | `loom_unsubscribe` |
| `invoke(method, argsJson)` | string, string | escape hatch: any `dispatch_table()` key above, for any loom.h call this table hasn't grown a named wrapper for yet |

`method()`/`init()`/`version()` are special: `init(dataDir, optionsJson)` and
`version()` bypass the dispatcher (there is no Loom context yet, or none is
needed) and call `LoomNative.nativeInit` / `nativeVersion` directly.

The React adapter uses `call(method, argsJson)` with named JSON objects (for
example `{"conv_id":"c_…","patch":{"title":"Updated"}}`). JNI normalises
these into the same positional dispatcher used by the older Kotlin wrappers.
Memory creation accepts the whole object; memory updates take an `id` plus
the patch fields. Invalid JSON is an error, never an implicit empty request.
Long-running React calls use `startStream(method,argsJson,callbackId)` and
`cancelStream(callbackId)`. A finite stream delivers a terminal callback even
when it fails before native context initialisation or after shutdown.

### Long-running / networked calls

These return `{"accepted":true}` **immediately** and run on a 4-thread
executor (`LoomBridge.executor`); their result streams back through
`window.__loomCallbacks[callbackId](chunkJson, done)` (see next section).
The web adapter must generate a fresh `callbackId` (any string; the sample
UI uses a UUID) per call and register `window.__loomCallbacks[callbackId]`
before invoking these.

| JS method | args | chunks delivered |
|---|---|---|
| `chat(requestJson, callbackId)` | `loom_chat_ex` request object, string | every `start`/`delta`/`reasoning`/`done`/`error` chunk loom.h documents, verbatim, `done` flag as-is |
| `importFile(path, title, callbackId)` | string, string?, string | `{"type":"progress","current","total","status"}` (done=false) while running, then `loom_import_file`'s result JSON once (done=true) |
| `exportConversation(convId, fmt, callbackId)` | string, string, string | one chunk: `loom_export_conversation`'s result (done=true) |
| `refreshModels(callbackId)` | string | one chunk: `loom_refresh_models`'s result (done=true) |
| `graphReindex(convId, callbackId)` | string?, string | one chunk: `loom_graph_reindex`'s result (done=true) |
| `transcribe(audioPath, optionsJson, callbackId)` | string, string?, string | one chunk: `loom_transcribe`'s result (done=true) |
| `ocr(imagePath, optionsJson, callbackId)` | string, string?, string | one chunk: `loom_ocr`'s result (done=true) |
| `githubSync(requestJson, callbackId)` | string, string | one chunk: `loom_github_sync`'s result (done=true) |
| `subscribe(event, callbackId)` | string, string | `{"event":name,"payload":...}` repeatedly (done=false) until `unsubscribe(token)`; returns `{"token":n}` synchronously |

### The streaming contract, precisely

```js
window.__loomCallbacks = window.__loomCallbacks || {};
const id = crypto.randomUUID();
window.__loomCallbacks[id] = (chunkJson, done) => {
  const chunk = JSON.parse(chunkJson);   // chunkJson is JSON *text*, not an object
  // ... handle chunk ...
  if (done) delete window.__loomCallbacks[id];
};
LoomBridge.chat(JSON.stringify({message: "hi"}), id);
```

`chunkJson` is always the raw JSON text (matching every other `LoomBridge`
method's "JSON string in, JSON string out" convention) — the adapter
`JSON.parse`s it itself. It is embedded as a JS string literal via
`org.json.JSONObject.quote()` (`LoomCallbacks.kt`), which escapes exactly
what needs escaping for a JS/JSON string literal (quotes, backslashes,
control characters, U+2028/U+2029); it is never string-concatenated raw.
Delivery always happens on the UI thread via `WebView.evaluateJavascript`,
from whatever thread produced the chunk (a Loom worker thread as easily as
the calling background-executor thread).

## WebView hardening

* The page is **never** loaded from a `file://` URL. `androidx.webkit`'s
  `WebViewAssetLoader` serves `app/src/main/assets/web/` (the
  `copyWebAssets` Gradle task's target — see below) over a virtual
  `https://appassets.androidx.startup/assets/web/` origin instead, so
  `allowFileAccess`, `allowFileAccessFromFileURLs` and
  `allowUniversalAccessFromFileURLs` all stay off: the WebView has no
  filesystem access beyond what the asset loader explicitly hands it.
* `LoomBridge` is added to exactly one `WebView`, which only ever navigates
  within that virtual origin — `shouldOverrideUrlLoading` hands any other
  host to the system browser instead of navigating the WebView there — so
  the interface is never exposed to a third-party page.
* `mixedContentMode = MIXED_CONTENT_NEVER_ALLOW`, `usesCleartextTraffic =
  false` in the manifest.

## HTTP: injected from Kotlin, not linked into the native library

`loom/CMakeLists.txt`'s `LOOM_WITH_OPENSSL` is forced `OFF` for this build
(`app/src/main/cpp/CMakeLists.txt`): the native library never links
cpp-httplib/OpenSSL. Instead, `MainActivity`/`LoomNative.nativeInit` calls
`loom_set_http_transport` once with a C callback (`http_send_trampoline` in
`loom_jni.cpp`) that calls `LoomHttp.sendRequest(requestJson, handle)` — a
plain `java.net.HttpURLConnection` implementation (no OkHttp) that streams
the response back into the native side through
`nativeHttpResponseBegin/Write/Fail` (thin wrappers over
`loom_http_response_begin/write/fail`), checking
`nativeHttpResponseCancelled` between reads. Every Loom network call — chat,
model refresh, semantic-LLM analysis, batch polling, ASR/OCR providers,
GitHub sync — goes through this path, on whichever thread made the call.

**Side effect worth knowing:** `LOOM_WITH_OPENSSL=OFF` doesn't only skip
cpp-httplib's TLS — `src/crypto/crypto.cpp` (the AES-256-GCM `core/crypto.py`
port) is entirely `#if defined(LOOM_HAVE_OPENSSL)`, same as Python's own
"gracefully degrades to no-op if `cryptography` is missing" behaviour. So on
this build `loom_crypto_status` reports `available: false` and the optional
zero-knowledge encryption feature is off, same as chat/DB/graph — not a bug,
just the one real tradeoff of injecting HTTP from Kotlin instead of linking
OpenSSL in. If encryption on Android turns out to matter more than this
tradeoff, the fix is `LOOM_WITH_OPENSSL=ON` in `app/src/main/cpp/CMakeLists.txt`
(OpenSSL still doesn't need to *carry* HTTP — a platform HTTP transport and a
linked OpenSSL for crypto aren't mutually exclusive) plus bundling
`libcrypto.so`/`libssl.so` for both ABIs, which nobody has sized or tested
here.

## Data directory & permissions

The shared data directory
(`/storage/emulated/0/Documents/ChatADHD`, `engine/paths.py` /
`loom/include/loom/config.h`) is outside app-specific storage on purpose —
it must survive an uninstall/reinstall and stay readable by the desktop
app. From API 30 that needs the "All files access" special permission
(`MANAGE_EXTERNAL_STORAGE`); `MainActivity.ensureStoragePermissionThenLoad()`
checks `Environment.isExternalStorageManager()` and, if needed, shows a
`Snackbar` that opens the system settings screen for it before loading the
web UI. Below API 30 the manifest declares legacy `READ/WRITE_EXTERNAL_STORAGE`
permissions (`android:maxSdkVersion` capped) and `requestLegacyExternalStorage="true"`;
the runtime permission request remains a gap listed below. `MainActivity.loadApp()`
calls `LoomBridge.init(null,null)` before loading the React page, so its initial
backend queries have a context. The public `init(dataDir, optionsJson)` remains
available for explicit native clients.

## Building the app

Requires the Android SDK + NDK (not available in this environment — see
"Verification without an Android SDK" below for what *was* run).

```bash
cd loom/android
# 1. loom/web must have a production build first:
#      (cd ../web && npm run build)   # -> loom/web/dist
# 2. ./gradlew assembleDebug
#    - `copyWebAssets` (a Sync task) copies loom/web/dist into
#      app/src/main/assets/web, warning (not failing) if it's missing so
#      JNI-only work isn't blocked on the web build.
#    - externalNativeBuild/CMake builds loom_jni.so for arm64-v8a and
#      x86_64 (ndkVersion "27.0.12077973", ANDROID_STL=c++_shared).
```

`gradlew`/`gradle-wrapper.jar` are checked in and were generated locally with
`gradle wrapper --gradle-version 8.14.3` from the system Gradle install (no
network fetch). A full `./gradlew assembleDebug` needs the Android
SDK/NDK and Google's Maven repo, neither reachable in this sandbox — see
below for what was verified without them.

## Verification without an Android SDK

Two things were run in this environment; both are reproducible without a
phone, an emulator, or the Android SDK/NDK.

### 1. Gradle config, offline

```
cd loom/android && gradle help --offline
```

Fails at plugin *artifact resolution* (`com.android.application:...:8.6.1`
— Google's Maven isn't reachable from this sandbox), which is expected and
listed as an acceptable skip in this task's brief. It fails there, not
earlier: `settings.gradle.kts` and the root `build.gradle.kts` both parse
and evaluate correctly up to that point, which is what's actually being
checked offline.

`app/src/main/cpp/CMakeLists.txt` was verified for real, on the host
compiler: `cmake -S app/src/main/cpp -B <tmp>` configures and generates
cleanly (it `add_subdirectory()`s the real `loom/` tree with
`LOOM_USE_SYSTEM_SQLITE=OFF`, `LOOM_WITH_OPENSSL=OFF`, `LOOM_BUILD_TESTS=OFF`
forced, exactly as Android will build it), and `ninja loom_core` with that
exact configuration compiles clean (one pre-existing, unrelated
`-Wunused-function` warning in `net/http_default.cpp`; not `-Werror` here,
`LOOM_WERROR` is off for this subproject on purpose).

### 2. Host JNI smoke test (`loom/android/verify/`)

Compiles the **real, unmodified** `app/src/main/cpp/loom_jni.cpp` against
the JDK's own `jni.h` and a prebuilt `libloom.so`, then runs a plain-`javac`
Java test (`loomverify.HostSmokeTest`, no Kotlin, no Android SDK) that calls
the exact native method signatures `LoomBridge.kt`/`LoomNative.kt` use:

```bash
cmake -S loom/android/verify -B <build-dir> -DLOOM_LIBRARY=<path-to-libloom.so>
cmake --build <build-dir>
ctest --test-dir <build-dir> --output-on-failure
```

Builds `libloom.so` yourself first if you don't have one:
`cd loom && cmake --preset dev && cmake --build --preset dev` (needs
`-DLOOM_SHARED=ON`, already set by the `dev`/`release` presets). Configure
silently adds no test (exit 0, zero tests) if no JDK or no `libloom.so` is
found, so it never breaks a machine with neither — see the two `return()`s
at the top of `verify/CMakeLists.txt`.

**This was run in this environment and passed**, exercising:
- `nativeInit` against a throwaway data directory (real SQLite, real config/
  secrets stores).
- Rejection of malformed/non-object init options inside the JNI error
  boundary, and exactly one terminal callback for early chat/import failures
  before init and after shutdown.
- `set_config`/`set_secret` through `nativeInvoke`.
- `create_conversation`.
- Named object arguments from the React adapter: conversation lookup, nested
  conversation patches, whole-object memory creation and flat memory patches.
- In-flight unsubscribe lifetime: pause one event callback, unsubscribe a
  second already-dispatched handler, then resume and verify its callback ID
  remains valid. This exercises the native shared-lock concurrency directly.
- An offline knowledge run, catalog queries and goal-directed context through
  the same native dispatcher used by the workbench.
- `chat` (streaming): a real `loom_chat_ex` call against a local
  `com.sun.net.httpserver.HttpServer` mock that serves an SSE stream,
  reached through the **real** `LoomHttp`-equivalent Java HTTP transport
  (a plain-Java port of `LoomHttp.kt` without the two Android-only calls,
  `android.util.Base64`/`Log`) injected via `loom_set_http_transport`. The
  request body sent to the mock server and the streamed response chunks
  delivered back through `LoomCallbacks.onChunk` were both asserted for
  exact content.
- `get_messages`: the round-tripped user message — containing Polish
  diacritics, BMP and astral-plane emoji, and a ZWJ family-emoji sequence
  (`Zażółć gęślą jaźń - test 😀🔥🧠👨‍👩‍👧‍👦`) — is asserted to match
  **exactly** (UTF-8 → JNI `NewString`/`GetStringChars` (UTF-16, not the
  modified-UTF-8 `NewStringUTF`/`GetStringUTFChars`) → SQLite → UTF-8 →
  Java `String`).
- `nativeShutdown`.

Fixing this test caught a real bug: the first version of `loom_jni.cpp`
called `loom::utf8::encode/decode` and `loom::json::dump/parse_or` — Loom's
own internal C++ helpers, not part of the C ABI. That happens to link when
`loom_jni.so` statically absorbs `loom_core` (the on-device Android build),
but fails — as it did here the first time this test ran — the moment the
JNI glue is linked against a *prebuilt* `libloom.so` instead, because
`libloom.so` exports only the `extern "C" loom_*` functions (hidden
visibility otherwise; see `loom/CMakeLists.txt`'s shared-library target and
`loom/README.md`'s C ABI conventions). `loom_jni.cpp` now hand-rolls its own
UTF-8 codec and uses `nlohmann::json` directly (header-only, no linkage of
its own) instead — `loom.h` really is the only interface it depends on now,
which this test is what proved it.

## Gaps

- **Activity lifecycle still performs blocking native work on the UI thread.**
  `loadApp()` initialises the native context before mounting the web UI, and
  `onDestroy()` calls shutdown synchronously. The JNI shared context lock
  prevents shutdown from freeing a context while a call is active, so shutdown
  can wait for a long chat/import/knowledge operation. Device testing and a
  lifecycle owner for the process-wide context are needed before moving this
  work off-thread; merely launching shutdown in another thread would allow an
  old Activity to close a newly attached Activity's context.
- **Queued stream cancellation is not a lifecycle manager.** Chat cancellation
  uses the request ID and knowledge cancellation uses the currently running
  native run. A cancellation arriving before an executor task registers its
  native request may miss it. The browser callback is removed, but work can
  still start. This needs executor-task ownership and device-side tests.
- **Legacy storage permissions need a device pass.** The API 26–29 path currently
  proceeds to `loadApp()` without a runtime READ/WRITE permission request; the
  all-files-access settings flow only covers API 30+. No host test validates
  Android storage permission behavior.
- **Unsubscribed callback userdata is retained until native shutdown.** The
  event bus permits an already-dispatched event to finish after unsubscribe.
  JNI keeps its callback ID alive until active calls and workers have drained,
  avoiding a use-after-free. Retired IDs are cleared on shutdown; many
  subscribe/unsubscribe cycles in one long-lived context retain these small
  strings until then.
- **Zero-knowledge encryption (AES-256-GCM) is unavailable on this build** —
  see "HTTP: injected from Kotlin" above; it's a direct consequence of
  `LOOM_WITH_OPENSSL=OFF`, not an oversight, but worth listing here too since
  it's a headline ChatADHD feature.
- **Never run on a device or emulator** — no Android SDK/NDK in this
  environment. Everything above is host-side verification of the same
  code, not an on-device run.
- **No microphone capture wired up.** `loom_transcribe`/`media_providers.h`
  expect a file path; nothing in this shell records audio to one yet
  (`RECORD_AUDIO` isn't even in the manifest). `transcribe()` works for a
  file the user picked some other way (e.g. a share-sheet import).
  Recording UI is a `loom/web` + a small additional bridge concern, not
  started here.
- **`assets/web/index.html` is a placeholder.** It only smoke-tests
  `window.LoomBridge` (open it and click through the buttons once
  `loom/web/dist` isn't there yet). `copyWebAssets` overwrites it with the
  real UI (a `Sync` task, so stale files from an old build are removed too)
  the first time `loom/web` has a production build.
- **Release signing isn't configured** — `buildTypes.release` has no
  signing config; `assembleRelease` will produce an unsigned APK.
- **No instrumented/Espresso tests** — out of scope for a host-only
  sandbox; the host JNI smoke test is the closest available substitute for
  the native/bridge layer.
