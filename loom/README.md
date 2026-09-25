# Loom

Loom is the C++20 kernel behind ChatADHD. It replaces the Python `engine/` and
`core/` packages with one library that every client can use: the Kivy app
today, and later the Android shell (JNI), the CLI, the HTTP server and the web
UI. In the terms of the architecture document
([`docs/architecture/MEGA_MASTER_2026-09-16.md`](../docs/architecture/MEGA_MASTER_2026-09-16.md)),
Loom is the reusable kernel and ChatADHD is its first reference workbench.

This is wave 1. The foundation is complete and tested, and every other module
has a complete, documented header and a compiling stub:

| Area | Status |
|---|---|
| `result.h`, `log.h`, `util/*` (ids, time, SHA-256/HMAC/PBKDF2, base64, UTF-8 + Unicode tables, JSON, fs) | **implemented** |
| `db.h` (full `engine/db.py` port + `loom_*` tables + FTS5 search), `sqlite.h` | **implemented** |
| `event_bus.h`, `config.h` (Config, Secrets, data-dir resolution) | **implemented** |
| `provenance.h` (BlobStore, ProvenanceStore, EventLog), `tasks.h`, `relations.h` | **implemented** |
| `runtime.h` (composition root), `loom.h` (C ABI, 82 functions) | **implemented** (wrappers call stubbed engines where noted) |
| `net/http.h`: ScriptedTransport, URL/form/multipart helpers | **implemented** |
| `re/regex.h`, `semantic_analyzer.h`, `semantic_llm.h`, `graph_engine.h`, `graph_memory.h`, `selector.h`, `memory_engine.h`, `net/http.h` (default transport), `net/sse.h`, `providers.h`, `chat_engine.h`, `batch_api.h`, `semantic_worker.h`, `importer.h`, `crypto.h`, `media_providers.h`, `github_sync.h` | **header + stub** (wave 2) |

`grep -rn "STUB: wave2" loom/src` lists every function that still needs an
implementation.

## Build

Requirements: CMake 3.24+, Ninja, a C++20 compiler (GCC 13 or Clang 18), and
Python 3.11 with `requests` and `cryptography` for the compat tests.
All other dependencies are vendored in [`third_party/`](third_party/README.md).

```bash
cd loom
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

| Preset | What it is |
|---|---|
| `dev` | Debug, system SQLite, also builds `libloom.so` |
| `release` | Release, vendored SQLite, `libloom.so` |
| `asan` | AddressSanitizer + UBSan |
| `tsan` | ThreadSanitizer (vendored SQLite, so SQLite itself is instrumented) |
| `vendored` | Debug with the SQLite amalgamation (what Android uses) |

CMake options: `LOOM_USE_SYSTEM_SQLITE`, `LOOM_WITH_OPENSSL` (`AUTO`/`ON`/`OFF`),
`LOOM_BUILD_TESTS`, `LOOM_BUILD_CLI`, `LOOM_BUILD_SERVER`, `LOOM_SHARED`,
`LOOM_WERROR`. The presets turn `-Werror` on. Sources and tests are picked up by
glob, so adding a file never requires editing `CMakeLists.txt`.

Targets: `loom_core` (static library), `loom` (shared library that exports only
the C ABI), `loom_tests` (doctest, one ctest entry per `tests/*.cpp`) and
`loom_compat_tool` (the C++ side of the Python differential tests).

## Layout

```
loom/
  include/loom/     public headers (the contract); loom.h is the C ABI
  src/              implementation; src/capi/ = C ABI, one file per area
  tests/            doctest unit tests (test_<area>.cpp)
  tests/compat/     Python differential tests + loom_compat_tool (tool_<area>.cpp)
  tools/            generators (Unicode tables, analyzer rules from core/semantic.py)
  third_party/      nlohmann/json, cpp-httplib, doctest, miniz, SQLite
```

## Sharing data with the Python app

Loom and the Python app share one data directory. Both use the same file names
(`chatadhd.db`, `config.json`, `secrets.json`, `memory.json`, `models.json`,
`attachments/`, `exports/`, `logs/`), the same resolution order
(`CHATADHD_DATA`, then the `.chatadhd_data` sentinel, then `~/.chatadhd`) and the
same formats. The rules:

- The core schema DDL is byte-identical to `engine/db.py`, and `schema_version`
  stays `"4"`. Migrations are the same forward-only, guarded steps.
- IDs, timestamps and JSON columns are written exactly the way Python writes
  them (`uuid4().hex[:12]`, `utcnow().isoformat()+"Z"`, `json.dumps`).
- Loom-only state lives in `loom_*` tables and never changes what the core
  tables mean. There are no triggers.
- Full-text search is derived state kept in a separate file
  (`chatadhd.fts.db`). Loom reconciles it after writes from any process
  and rebuilds it when it is missing or corrupt, so the main database never
  needs an SQLite build with FTS5.

`tests/compat/test_db_compat.py` checks all of this by running the same
operations through `engine.db.Database` and through Loom, on separate files,
on one shared file in alternation, and from two processes writing at the same
time. It then compares the results, the API views and the raw rows. A
one-character drift in JSON formatting makes it fail. The other compat tests
cover JSON and float formatting, UTF-8 and `str` methods on random input,
config and secrets, data-dir resolution, and the exported ABI.

## C ABI conventions

The full contract is at the top of [`include/loom/loom.h`](include/loom/loom.h). In short:

- Every `const char*` a function returns is heap JSON that the caller owns and
  releases with `loom_free_string`.
- On failure, JSON functions return `{"error":{"code":"not_found","message":"..."}}`
  and never NULL (except `loom_init`). Int functions return `0` or a negative
  `LOOM_E_*` value (the negated `loom::Errc`).
- Every function is thread-safe, and no exception ever crosses the boundary.
- `loom_chat` blocks and streams JSON chunks (`start`, `delta`, `reasoning`,
  then exactly one final `done` or `error`) to the callback on the calling
  thread.
- Platforms can route all HTTP through their own stack with
  `loom_set_http_transport` (Android: OkHttp).
- `libloom.so` exports exactly the functions declared in `loom.h`, and
  `test_abi_compat.py` enforces this.

## Decisions

Following constitution A of the architecture document, each decision is one of:
**invariant** (enforced in code and tests), **policy/default** (data you can
replace) or **platform detail** (an adapter).

| Decision | Class |
|---|---|
| Shared on-disk format with the Python app (schema v4 DDL, IDs, timestamps, `json.dumps` formatting, file names, data-dir resolution) | invariant |
| Loom state only in `loom_*` tables; no triggers or virtual tables in the main DB | invariant |
| Raw sources are immutable: content-addressed blobs (`blobs/ab/cd/<sha256>`, chmod 444), with sources and provenance rows for every import | invariant |
| Task and event history is append-only (`loom_events`); tasks carry input/output hashes and checkpoints and resume after a crash | invariant |
| Errors are values (`Result`/`Status`); nothing throws across the C ABI | invariant |
| C ABI: opaque context, caller-freed JSON strings, error object, negative codes | invariant (versioned by `LOOM_ABI_VERSION`) |
| FTS is derived, rebuildable state with a LIKE fallback | invariant; whether FTS5 is available is a platform detail |
| Relation vocabulary (inverse, symmetry, category); unknown types auto-registered | policy/default (seeded JSON) |
| Analyzer rules (regexes, topic keywords), generated from `core/semantic.py` | policy/default (data) |
| Config defaults = Python `DEFAULTS`; Loom policy keys (`loom_event_log_types`, `loom_task_workers`) are served as fallbacks and never written | policy/default |
| Provider manifests and capabilities; `can(resource, capability, constraints)` instead of `if provider == X` | policy/default |
| Context budget (tokens ≈ code points / 4), source order memory → graph → FTS | policy/default |
| System SQLite vs vendored amalgamation | platform detail |
| HTTP stack (cpp-httplib + OpenSSL, or an injected platform transport) | platform detail |
| Log sinks (stderr, ring buffer, logcat through the C ABI) | platform detail |
| JSON as the ABI codec (`nlohmann::ordered_json`, which keeps key order like Python dicts) | platform detail; the semantics live in the C++ types |
| Unicode tables pinned to the generating CPython (currently Unicode 14.0) | platform detail (regenerate with `tools/gen_unicode_tables.py`) |

Deliberate differences from the Python behaviour (each is documented in its header):

- Row ordering breaks ties by `rowid`, so batch-imported rows come back in insertion order.
- `update_msg` enforces the status whitelist.
- Bad JSON in a column is returned as a string instead of raising.
- `ChatEngine` no longer sends the new user message twice.
- `estimate_cost` parses string prices.
- Native TF-IDF is always available, so the default selector tier is 2.
- The C ABI returns parsed JSON for metadata columns.

## Wave 2: working in parallel

Each area owns its headers, `src/<dir>/`, one `src/capi/capi_<area>.cpp`, its
own `tests/test_<area>*.cpp` and `tests/compat/tool_<area>.cpp` /
`test_<area>_compat.py`. Nobody edits `CMakeLists.txt`, `runtime.cpp` or
another area's files. Constructor signatures in the headers are the wiring
contract. Additive header changes (new methods) are fine. Any other change goes
through the lead.

| Area | Headers | Sources | C ABI file |
|---|---|---|---|
| semantic / graph | `re/regex.h`, `semantic_analyzer.h`, `graph_engine.h`, `graph_memory.h`, `selector.h`, `memory_engine.h` | `src/re`, `src/semantic/analyzer.cpp`, `src/graph`, `src/search`, `src/memory` | `capi_graph.cpp` |
| importer | `importer.h` | `src/import` | `capi_import.cpp` |
| net / chat / worker | `net/http.h` (default transport), `net/sse.h`, `semantic_llm.h`, `providers.h`, `chat_engine.h`, `batch_api.h`, `semantic_worker.h` | `src/net/http_default.cpp`, `src/net/sse.cpp`, `src/semantic/semantic_llm.cpp`, `src/providers`, `src/chat`, `src/worker` | `capi_chat.cpp`, `capi_http.cpp` |
| crypto / media / github | `crypto.h`, `media_providers.h`, `github_sync.h` | `src/crypto`, `src/media`, `src/github` | `capi_media.cpp` |

The importer needs `re::Regex` for its HTML and text heuristics. Land the regex
engine first, or write those parsers as hand-written scanners.
