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
        ConnectionActivity.kt   local / server-account chooser (P3)
        StorageActivity.kt      data location setting, move with verification, delete old copy (F2)
        DataLocation.kt         the setting (SharedPreferences "storage") + actions on it
        SecretsVault.kt         Android Keystore AES key for sealing secrets.json
        ConnectionPolicy.java, DataLocationPolicy.java, DataMigration.java,
        SecretsEnvelope.java, SecretsVaultCore.java
                                pure Java rules (no android.* imports) with host tests in verify/
      cpp/
        CMakeLists.txt          add_subdirectory(loom core), builds loom_jni.so
        loom_jni.cpp            the JNI glue (see its own file header)
      assets/web/               `copyWebAssets` Gradle task syncs loom/web/dist here
      res/                      minimal theme/icon (vector, no binary assets)
  verify/                       host-only JNI smoke test + rule tests, no Android SDK needed
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
| `graphEntry(requestJson)` | string? | `loom_graph_entry` |
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
| `invoke(port, action, requestJson, callbackId)` | string, string, string, string | one chunk: `loom_invoke`'s result (done=true); ports `storage`, `github`, `core` |
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
* `mixedContentMode = MIXED_CONTENT_NEVER_ALLOW`. The manifest allows cleartext
  traffic only so the explicit "plain HTTP for a LAN server" option can work;
  `ConnectionPolicy` is the gate (see "Connection mode" below).

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

## Data directory (F2: the location is a setting)

Where the data lives is a **setting**, kept per device in SharedPreferences
`storage` (not inside the data directory it describes). `StorageActivity`
(reached from the gear > "Data location...") changes it; nothing is moved or
deleted without pressing a button there.

| kind | path | other apps | uninstall |
|---|---|---|---|
| `private` (default for new installs) | `Context.getFilesDir()/ChatADHD` | cannot read it (system sandbox) | data removed |
| `app_external` | `getExternalFilesDir()/ChatADHD` (`Android/data/...`) | not readable on Android 11+; Android 10 and older: apps with storage access may | data removed |
| `shared` | `/storage/emulated/0/Documents/ChatADHD` | every app with "All files access" can read it, `secrets.json` included | **survives**; other tools (sync clients, the desktop app) can use it |
| `custom` | any absolute path the user types | depends on the path | depends |

* **New installs** use `private` and are never asked for "All files access".
* **Existing installs** whose data already lives in `Documents/ChatADHD`
  (detected by the `.chatadhd_data` sentinel) **stay there**: nothing is moved
  on upgrade. A red banner over the web UI says the data is readable by other
  apps; tapping it opens `StorageActivity`, which has the button
  **"Przenieś do prywatnego katalogu" / "Move to private storage"**.
* **Shared storage stays selectable** (it survives uninstall, which matters
  to some users). It is never forbidden; the banner and the warning text are the
  whole consequence. Whether a location counts as shared is decided from the
  real (canonical) path, so `..` or a symlink cannot disguise one
  (`DataLocationPolicy.classify`).
* A saved choice always wins. If preferences are lost and data exists in both
  places, `private` is taken (it is the migration target).

### Moving the data (copy -> verify -> switch -> keep the old copy)

`DataMigration.copyAndVerify` (pure Java, tested against the real core):

1. the Loom core is shut down first (closes SQLite, checkpoints the WAL);
2. the destination must be absent or empty (nothing is ever overwritten); a
   destination that already holds ChatADHD data is only offered as "switch to
   it";
3. every file is written as `name.part`, fsync'ed, **re-read from disk and
   its SHA-256 compared with the digest of the bytes read from the source**,
   then renamed; the `.chatadhd_data` sentinel is copied **last**, so a
   half-copied folder is never mistaken for a data directory;
4. only when all files verified, the setting is switched (`commit()`, not
   `apply()`); on any error or cancel the destination is emptied again and the
   old location stays in use;
5. the **old copy is left untouched** and listed in `StorageActivity` with a
   "Delete old copy" button (confirmation; refused unless the folder holds the
   sentinel and is neither the live folder nor its parent/child). Until the user
   deletes it, the old copy - `secrets.json` included, if it was in a shared
   folder - is still readable by other apps; the screen says so.

Symbolic links are not copied (reported). `MainActivity` asks for "All files
access" only when the chosen location is outside the app's own storage.

### API keys: sealed with the Android Keystore

`secrets.json` is the one file the core keeps API keys in. The core reads it
**once at start** and rewrites the whole file on `set_secret` /
`delete_secret` (verified by `verify/.../DataLifecycleTest`), so the plain file
only has to exist briefly. `SecretsVaultCore` (pure Java; key supplied by
`SecretsVault.kt`) therefore:

* seals the file into `secrets.json.keystore` (`LSK1` | iv | AES-256-GCM, AAD
  binds magic + purpose) with a non-exportable AES key in the **Android
  Keystore** (`KeyGenParameterSpec`, GCM, 256 bit, randomized IV, alias
  `chatadhd.secrets.v1`), reads it back and compares, and only then deletes the
  plain file;
* before the core starts, writes the plain `secrets.json` (owner-only, atomic)
  from the sealed copy; right after start, and after every bridge call that can
  save a secret (and in `onStop`/`onDestroy`), seals again. While the app runs,
  the plain file exists only for those moments;
* **keeps a plain `secrets.json` readable for migration**: a plain file that
  exists is authoritative (an install from before sealing, or a crash between a
  save and the re-seal) and is sealed, never overwritten by an older sealed
  copy;
* when sealing is impossible the plain file simply stays (the app tells the
  user); when the key is gone (uninstall + reinstall, restore to another phone)
  the sealed file is **not deleted**: the app says the keys must be entered
  again, and an old unreadable file is kept as `secrets.json.keystore.unreadable`;
* is a setting: "Seal API keys with the Android Keystore" in `StorageActivity`
  (default on). Turning it off writes one plain copy again. The key is not bound
  to the lock screen (the background semantic worker must be able to start
  unattended); binding it is a possible future setting.

What this does **not** do: conversations, memory and config stay plain (the
core's zero-knowledge encryption is a separate feature, still off on Android,
see Gaps). The protection of those files is the choice of location above.

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

Optional Gradle properties (used by the CI workflow, harmless otherwise):
`-Ploom.versionCode=N`, `-Ploom.versionName=X`, `-Ploom.abis=arm64-v8a`,
`-Ploom.nativeBuildType=Release`, `-Ploom.ccache=true`.

### Package built by GitHub Actions (P6)

`.github/workflows/package.yml` (manual `workflow_dispatch` only, so it
costs minutes only when asked) builds the web bundle, then `./gradlew
assembleDebug`, and uploads the artifact **`chatadhd-android-apk`**
(`app-debug.apk` + `.sha256`); it also builds the server archive (artifact
`chatadhd-server-linux-x64`, see `deploy/README.md`). Inputs: ABIs (default
`arm64-v8a`; add `x86_64` for emulators), native build type (default
`Release`: a `Debug` native core is slow on a phone), SDK CMake version
(default: newest 3.x the SDK offers; the project needs >= 3.24). The NDK and
`compileSdk` are read from `app/build.gradle.kts`, never duplicated. The job
fails if a native lib for a requested ABI or the real web bundle is missing
from the APK, and prints `apksigner` certificate digests.

* **Signing.** The APK is signed with the *debug* key, which is enough to
  install it. Every GitHub runner is a fresh machine, so a new debug key would
  be generated each time, and Android refuses to update an app signed with a
  different key (and uninstalling removes private data!). The workflow
  therefore pins the key: from the secret `ANDROID_DEBUG_KEYSTORE_B64`
  (`base64 -w0 ~/.android/debug.keystore`) if set, else from the Actions
  cache (evicted after 7 days without use; a warning annotation says when a
  new key was generated). **Release signing** is done in the workflow, not in
  Gradle: with all four secrets `ANDROID_KEYSTORE_BASE64`,
  `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, `ANDROID_KEY_PASSWORD` it
  runs `assembleRelease`, then zipalign + apksigner (keystore in `RUNNER_TEMP`,
  passwords via env, deleted at the end) and uploads
  `chatadhd-android-apk-release`; with none it builds the debug APK as
  `chatadhd-android-apk-TEST-KEY`; some but not all fails the run.
* `versionCode` = run number, `versionName` = `ci-<run>-<sha7>`.
* Install: download the artifact on the phone, unzip, open the `.apk`.

`gradlew`/`gradle-wrapper.jar` are checked in and were generated locally with
`gradle wrapper --gradle-version 8.14.3` from the system Gradle install (no
network fetch). A full `./gradlew assembleDebug` needs the Android
SDK/NDK and Google's Maven repo, neither reachable in this sandbox — see
below for what was verified without them.

## Verification without an Android SDK

Three things were run in this environment; all are reproducible without a
phone, an emulator, or the Android SDK/NDK. (The Kotlin files - including the
whole of F2's UI - are only syntax-checked here; the first Actions run is
their first compilation.)

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

### 3. F2 rules and the real core (`verify/`, CTest `android_*`)

`verify/CMakeLists.txt` compiles the app's own pure-Java files
(`ConnectionPolicy`, `DataLocationPolicy`, `DataMigration`, `SecretsEnvelope`,
`SecretsVaultCore`), never copies:

- `android_DataLocationTest`: new install -> private, legacy shared data stays
  shared, a saved choice wins, `..`/symlink/prefix-lookalike cannot pass for a
  private folder, "delete old copy" refuses the live folder, its parent and any
  folder without the sentinel.
- `android_DataMigrationTest`: per-file SHA-256 (3 MiB random blob, empty
  file, empty directories), sentinel copied last, the source byte-identical
  afterwards, an occupied destination is refused untouched, cancel removes
  what was created and keeps a pre-existing empty folder, `secrets.json`
  owner-only in the copy, `deleteTree` never follows a link out of the tree.
- `android_SecretsVaultTest`: envelope round trip, fresh IV, tamper/wrong key/
  truncated blob rejected; plain file is authoritative after a crash, an
  unchanged sealed file is not rewritten, a lost key keeps the old file
  (`.unreadable`), a failing key provider leaves the plain file, switching the
  option off returns to one plain copy. A software AES key stands in for the
  Keystore on the host.
- `android_DataLifecycleTest`: the **real core** through the JNI glue - start
  on a plain `secrets.json`, seal it while the core runs (the core still knows
  its keys), `set_secret` makes the core rewrite the whole file, seal again,
  shut down, **move the directory with `DataMigration`**, start on the copy:
  conversation (Polish + emoji) and both keys are there; the old copy still
  starts.

Not verifiable here and therefore first checked on a device: the real Android
Keystore, `Environment.isExternalStorageManager()`/the settings intent, what
other apps can actually read on a given Android version, and the layout of
`StorageActivity`.

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
- **Devices of the page (P13) are wired but not run on a device.** The web UI is a *client* of the kernel: it declares
  per session which devices it can drive (`loom_invoke("actions", "client.declare", ...)`, the same call as over HTTP;
  `loom/web/src/api/loom-jni.ts` marks the transport `clientKind: "android"`) and carries out the device steps of a paused
  run itself (`actions.client_result` over `loom_invoke`). Inside the WebView that needs three things from this shell,
  all in `MainActivity`/`LoomBridge` and the manifest: `RECORD_AUDIO`/`CAMERA` and a `WebChromeClient.onPermissionRequest`
  that hands the microphone and the camera to the page this app loaded (the rule is the pure-Java
  `DevicePermissionPolicy`, checked on the host by `DevicePermissionTest`), `onShowFileChooser` for the file chooser, and
  `LoomBridge.notify` / `notificationState` / `requestNotifications` for notifications (the WebView has no Notification
  API; Android 13 asks at runtime). The declaration and `client_result` path through the real JNI glue is checked on the
  host by `HostSmokeTest`. Recordings, pictures and files never travel inside `client_result` (P14 6): the page hands the bytes
  to the kernel in bounded chunks (`client.blob_begin` / `append` / `commit` through the generic `invoke`, so the shell needs no
  new bridge method), the answer names the blob by its hash, and a stored blob is read back with `client.blob_read` chunks; the
  chunking, the hash and the read-back are in `HostSmokeTest` too. What is not checked: the Kotlin (no SDK here), the system permission dialogs, the recording itself
  on a phone. In server mode the bridge is not injected, so a remote page gets the WebView's own devices but no shell
  notifications.
- **`assets/web/index.html` is a placeholder.** It only smoke-tests
  `window.LoomBridge` (open it and click through the buttons once
  `loom/web/dist` isn't there yet). `copyWebAssets` overwrites it with the
  real UI (a `Sync` task, so stale files from an old build are removed too)
  the first time `loom/web` has a production build.
- **Release signing isn't configured** — `buildTypes.release` has no
  signing config; `assembleRelease` will produce an unsigned APK. The Actions
  package is debug-signed with a pinned debug key (see "Package built by GitHub
  Actions"). Needs the owner's keystore as an Actions secret.
- **F2 was never run on a device.** The sealing/move logic is host-tested
  against the real core; the Android parts (Keystore, permission intents,
  `StorageActivity`) are written, not run. Known simplifications: moving is a
  full copy with no resume - if the process is killed mid-copy the destination
  is left as a partial folder without the sentinel, the old location was never
  switched (nothing is lost), and the next attempt refuses that folder as "not
  empty" until the user deletes it; symbolic links are skipped; and an upgrade
  whose "All files access" was revoked cannot see its old shared data (it looks
  like a new install; choose "shared" in `StorageActivity` and grant access to
  get it back).
- **No instrumented/Espresso tests** — out of scope for a host-only
  sandbox; the host JNI smoke test is the closest available substitute for
  the native/bridge layer.

## Connection mode: local or server account (P3)

The app has a per-device setting (SharedPreferences `connection`, not the
data dir): `local` (default; bundled web + `window.LoomBridge`, data on the
phone) or `server` (WebView loads the Loom server's URL; no bridge, so the
web app uses HTTP transport and the P1 sign-in screen; the session cookie is
kept by Android's CookieManager). Switching never deletes local data.

- First run shows `ConnectionActivity`; later via the gear in the bottom-right corner.
- The URL is checked by `ConnectionPolicy` (pure Java, no android imports) and
  probed with `GET /api/auth/me` (200/401 + JSON = Loom server); the failure
  reason is shown.
- HTTPS required. Plain HTTP only if the user ticks the option AND the host is
  LAN/loopback (password and cookie travel unencrypted). The manifest allows
  cleartext only so that this opt-in can work; the policy is the gate.
- **Security:** `LoomBridge` is injected only in local mode for the bundled
  origin (`ConnectionPolicy.shouldInjectBridge`); never for a remote page.
- Tests: `verify/java/loomverify/ConnectionPolicyTest.java` (CTest
  `connection_policy_test`; `verify/` compiles the app's own `ConnectionPolicy.java`, never a copy).
- Not built here: no Android SDK on the dev machine, so the APK and the Kotlin
  files were not compiled.
