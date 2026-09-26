# Loom

Loom is the C++20 kernel behind ChatADHD. It replaces the Python `engine/` and
`core/` packages with one library that every client can use: the Kivy app
today, and later the Android shell (JNI), the CLI, the HTTP server and the web
UI. In the terms of the architecture document
([`docs/architecture/MEGA_MASTER_2026-09-16.md`](../docs/architecture/MEGA_MASTER_2026-09-16.md)),
Loom is the reusable kernel and ChatADHD is its first reference workbench.

Every module is implemented and tested (waves 1–2), and wave 3 adds the
first vertical slice of the architecture: the **Archive-to-Project
self-hosting pipeline** (MEGA MASTER §4.9, §7, §16), the `loom` command line,
the HTTP server and the Android shell.

| Area | Status |
|---|---|
| `result.h`, `log.h`, `util/*` (ids, time, SHA-256/HMAC/PBKDF2, base64, UTF-8 + Unicode tables, JSON, fs) | implemented |
| `db.h` (full `engine/db.py` port + `loom_*` tables + FTS5 search), `sqlite.h` | implemented |
| `event_bus.h`, `config.h`, `provenance.h`, `tasks.h`, `relations.h`, `runtime.h` | implemented |
| `re/regex.h`, `semantic_analyzer.h`, `semantic_llm.h`, `graph_engine.h`, `graph_memory.h`, `selector.h`, `memory_engine.h` | implemented |
| `net/http.h`, `net/sse.h`, `providers.h`, `chat_engine.h`, `batch_api.h`, `semantic_worker.h` | implemented |
| `importer.h`, `crypto.h`, `media_providers.h`, `github_sync.h` | implemented |
| `archive.h` — Archive Intelligence / Project Compiler | implemented (this README, [below](#archive-intelligence)) |
| `loom.h` — C ABI (archive, artifacts, `loom_import_file_ex` added) | implemented |
| `cli/` — `loom` command line | implemented |
| `server/` (REST/SSE facade), `web/` (React UI), `android/` (JNI shell) | see their own READMEs |

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
  src/archive/      Archive Intelligence pipeline (see below)
  cli/              the `loom` command line (+ tests/test_cli_smoke.py)
  server/, web/, android/   HTTP server, web UI, Android shell (own READMEs)
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

## Command line

`cli/main.cpp` builds the `loom` executable (`LOOM_BUILD_CLI`, on in every
preset). Each command opens the data directory (shared with the Python app)
without background workers, runs, and exits. `--json` switches every command to
machine-readable output; `loom --help` lists everything.

```bash
loom init                                  # create/open the data directory
loom conv list | conv show ID [--all] | conv create/rename/delete
loom msg edit ID "new text" | msg versions ID | msg restore ID | msg status ID excluded
loom chat --model M --depth 2 "question"   # streams; reasoning is shown dimmed
loom import ~/export.zip [--force]         # universal importer
loom export CONV_ID --format markdown --out chat.md
loom search "knowledge graph" | context "text" | graph nodes/edges/expand/reindex/stats
loom semantic status|pause|resume|wake|run | memory list/add/delete/context
loom config get/set | secret set KEY (value from stdin) | models [--refresh]
loom tasks list/show/resume/cancel | provenance ID | sources | artifacts list/show
loom archive run ... | archive status [RUN_ID] | crypto status/setup/unlock/lock
```

Secrets and passwords are read from stdin (with echo off on a terminal) and
never accepted as arguments. Ctrl-C cancels a chat (the partial answer is kept)
or pauses an archive run at its next checkpoint.

## Archive Intelligence

`include/loom/archive.h`, `src/archive/`. The Project Compiler workflow from
the architecture document, built as the self-hosting test: it reads chat
exports, documents, a repository and its git history, and compiles them into a
project description with sources for every claim.

```bash
loom --data-dir /tmp/loom-archive archive run \
     --source ~/chatgpt-export.zip --source ~/claude-export.zip \
     --repo . --out out/ --seed ChatADHD --seed Loom
```

| Stage (task kind `archive.*`) | What it does |
|---|---|
| `ingest` | Files, directories and zips. ChatGPT and Claude exports are walked structurally: timestamps, branch forks and the current path, plus Claude `projects.json` and `memories.json`. Other formats go through the Importer. Markdown and text become sections. Code files become digests of symbols, comments and TODOs. Git history (`git log --name-status` via `popen`, desktop-only) turns each commit into a dated document. `--include-db` adds existing conversations and their version groups. Raw bytes go to the BlobStore, and there is a `loom_sources` row and a `loom_provenance` row per message. Document keys are content hashes, so the corpus is the same in every data directory. |
| `retrieve` | BM25 over FTS5 (LIKE fallback) for each vocabulary term, top k per term (default: corpus size / 10). A conversation or document that is mostly relevant is completed. Titles are never used as a filter. |
| `expand` | Adds salient terms of the prose hits: TF-IDF contrast against the whole corpus, regex NER / identifiers, and sentence co-occurrence with the vocabulary. Each term records its reasons and evidence (provenance `term:<t>`). `retrieve`/`expand` repeat until no new term appears or `max_passes` is reached. |
| `graph` | Semantic analysis of the hits into the knowledge graph (SemanticLLM with `--llm auto` and a configured model, regex otherwise), plus per-document salient terms. |
| `cluster` | Deterministic Louvain on the term co-occurrence graph, which gives the themes. |
| `timeline` | Chronological path per theme, ChatGPT/Claude forks (kept or abandoned branch), version groups, commits. |
| `items` | Typed items: `idea`, `decision`, `rejected_option`, `open_question`, `implementation`, `bug`, `requirement`, `invariant`, `rationale`. A bilingual PL+EN cue-phrase classifier with section-heading hints gives each a confidence. Optional LLM refinement of low-confidence items with `--llm auto`. |
| `relate` | `supersedes` (a later decision reverses an earlier one on the same subject), `contradicts` and `resolves` (a later decision answers an earlier question). Nothing is deleted; items get a status. |
| `synthesize` | Renders `MASTER.md` (themes, timeline, decisions, rejected options, open questions, requirements, invariants, every claim as `[title › location @ date]`), `source_map.csv`, `timeline.json`, `items.jsonl`, `graph.json`, `project_manifest.json` and `gap_report.md`. The gap report checks named components and interfaces against declared symbols, spec bullets against code, referenced files, themes without code and TODO/FIXME. CamelCase names that come up again in synthesis feed one more retrieval round (`max_synthesis_rounds`). |
| `materialize` | Stores artifacts in the BlobStore and `loom_artifacts`, writes them plus `task_log.jsonl` to `--out`, and adds theme and item nodes with `supersedes`/`contradicts`/`resolves` edges to the graph. |

Invariants (tested in `tests/test_archive.cpp`):

- Every stage has an input hash over its parameters and the content hashes of
  the stage outputs it reads. An unchanged re-run is a cache hit for every
  stage.
- Cancelling (`CancelToken`, Ctrl-C, `loom_archive_cancel`, a client
  disconnecting from `/api/archive/run`) saves a checkpoint and pauses the
  stage task. The next run with the same inputs resumes it, and the artifacts
  come out identical to an uninterrupted run. A crashed process leaves the
  task `running`, and `recover_interrupted()` requeues it.
- Without network (`llm` = `off`, the default) the artifacts are
  byte-identical across runs and data directories: no random ids, no clock.
- The output directory gets a `.loom-archive` marker, and the walker skips
  marked directories so a run never ingests its own output.

C ABI: `loom_archive_run(ctx, config_json, cb, ud)`, `loom_archive_cancel`,
`loom_archive_status`, `loom_list_artifacts`, `loom_get_artifact`,
`loom_import_file_ex` (importer `force`). Server: `POST /api/archive/run`
(SSE progress), `POST /api/archive/cancel`, `GET /api/archive/status`,
`GET /api/artifacts[/{id}[/raw]]`.

The self-hosting run over this repository, with its findings, is in
[`docs/selfhost/`](../docs/selfhost/README.md).

Known limits: the ChatGPT/Claude structural walker parses one conversation at a
time, but a zip member is extracted to a temporary file first. Stage outputs
(the corpus with its text) are stored as JSON blobs, which is fine for tens of
thousands of messages but not yet for millions. The classifier is cue-based, so
recall on implicit decisions is limited. That is the reason for the optional
LLM refinement.

## Code ownership

Each area owns its headers, `src/<dir>/`, one `src/capi/capi_<area>.cpp` and
its own tests. Constructor signatures in the headers are the wiring contract;
additive header changes are fine, anything else goes through the lead.
