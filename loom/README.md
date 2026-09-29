# Loom

Loom is the C++20 kernel behind ChatADHD. It replaces the Python `engine/` and
`core/` packages with one library that every client can use: the Kivy app
today, and later the Android shell (JNI), the CLI, the HTTP server and the web
UI. In the terms of the architecture document
([`docs/architecture/MEGA_MASTER_2026-09-16.md`](../docs/architecture/MEGA_MASTER_2026-09-16.md)),
Loom is the reusable kernel and ChatADHD is its first reference workbench.

The Python-parity core is covered by differential tests. The archive and
knowledge pipelines, CLI, HTTP server, web workbench and Android bridge are
implemented. Extraction quality is still experimental: passing the software
tests does not establish complete understanding of real provider exports or
historical predictive accuracy. Current work and measured limits are recorded
in [`docs/CODEX_HANDOFF_2026-09-28.md`](../docs/CODEX_HANDOFF_2026-09-28.md).

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
| Knowledge layer foundation — `model.h`, `kb.h`, `knowledge_store.h`, `knowledge.h`, `loom/data/` | implemented ([below](#knowledge-layer)) |
| Knowledge layer areas — `catalog.h`, `extract.h`, `resolve.h`, `generalize.h` | implemented; extraction/retrieval quality remains under evaluation |
| Knowledge layer area — `context_engine.h`, `materialize.h` (context+materialize) | implemented ([below](#knowledge-layer)) |
| `server/` (REST/SSE facade), `web/` (React UI), `android/` (JNI shell) | knowledge/catalog/context integrated; browser and host-JNI tests; device testing outstanding |

## Build

Requirements: CMake 3.24+, Ninja, a C++20 compiler (GCC 13 or Clang 18), and
Python 3.11 with `requests` and `cryptography` for the compat tests.
All other dependencies are vendored in [`third_party/`](third_party/README.md).

```bash
cd loom
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

To include the HTTP server and its integration test, configure with
`cmake --preset dev -DLOOM_BUILD_SERVER=ON`. The web workbench uses the same
knowledge JSON as the CLI; see [`server/KNOWLEDGE_API.md`](server/KNOWLEDGE_API.md)
and [`web/README.md`](web/README.md). Run `npm ci && npm run build && npm run
test:transport && npm run e2e` in `loom/web` (install Chromium with
`npx playwright install chromium` first).

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
loom import ~/export.zip [--force]         # universal importer (ChatGPT/Claude export ZIPs: lossless)
loom import conversations.json --export-mode on   # lossless for a bare export .json too
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

## Provider exports (ChatGPT / Claude), lossless

`ImportOptions::export_mode` (`auto` default, `on`, `off`; CLI `--export-mode`,
C ABI option `export_mode`) selects the interpretation in
`src/import/export_*.cpp`. Format knowledge and confidence levels:
`docs/exports/OPENAI_ANTHROPIC_EXPORT_FORMATS.md`; fixtures and the independent
oracle: `tests/fixtures/exports/` (`EXPECTED.json`, made by
`tools/gen_export_fixtures.py`); tests: `tests/test_import_exports.cpp`.

- **When**: `auto` sends every ZIP through this path (nested containers, sharded
  `conversations-NNN.json`, unknown providers are reported; archives that are not
  provider exports fall back to the per-member legacy importers) and keeps bare
  `.json` on the Python-parity flattening; `on` also interprets bare exports.
- **Stored** (no schema change): conversation `source` is `import:openai|anthropic|unknown`;
  messages keep the provider role (`system`/`tool` included) and `model`; the
  current branch is `active`, hidden plumbing (system, tool, thoughts, code,
  browse, ...) is `excluded`, alternate branches are `version` rows (siblings
  share `version_group_id`, `parent_id` follows the provider tree), unreachable
  nodes are `excluded`. `messages.metadata.export` holds the complete original message
  object (`raw`), typed block summaries, attachments/pointers (resolved to ZIP
  members, bytes in the BlobStore), citations, flags; `conversations.metadata.export`
  holds every unmodelled conversation key, the graph facts and the branch
  bookkeeping. Accounts, feedback, shared links, projects, project docs, memories,
  canvas documents and unknown members are `nodes` of kind `export:*`.
- **Report** (`ImportResult::export_report`): provider, per-member disposition,
  counts in the shape of `EXPECTED.json`, asset links, unresolved keys,
  unreferenced/duplicate assets, errors, repairs, `json_leaves == leaves_preserved`.
- **Never a crash**: truncated arrays are salvaged element by element, invalid
  UTF-8 and lone surrogates are repaired (counted in `repairs`), zip-slip names are
  skipped, nesting > 512 is refused; each problem is an entry in `errors` and
  `partial` is set.

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

## Knowledge layer

The self-discovery / paradigm engine, built on one binding conceptual model:
[`docs/architecture/LOOM_CONCEPTUAL_MODEL.md`](../docs/architecture/LOOM_CONCEPTUAL_MODEL.md).
Every term there has exactly one meaning, and code, tables, JSON keys and C ABI
names use it in that sense. Closed sets are code, open sets are data (I6).

| Model (§) | Code | Data |
|---|---|---|
| Closed sets: evidence class, origin, principle level/form, validation status, the 14 universal roles, resolution, context band, status (incl. `lost`/`restored`), check state, ... (§2–§5) | `include/loom/model.h` enums, `to_string`/`parse` | — |
| Operation vocabularies: bindings, anchors, conditions, value ops, Expected-Property predicates, detect ops, segmenters, extractors | `include/loom/kb.h` `k*` arrays | combined by pack files |
| Source/Unit/Observation, Entity, Claim + Assessment (the seven questions), Model (§1–§2) | `model.h` structs, JSON round trip, content-derived ids (prefix table in the header) | — |
| Principle, Operator, Morphism (§3.1–§3.6) | `model.h` | `philosophy/principles.json`, `philosophy/operators.json` (dated priors), `rules/inference_rules.json` (operators that produce claims), `morphisms/` |
| Project kind, facet, artifact type, domain kind -> role (§3.5) | `model.h` views (`project_kind()`, ...) | `project_kinds/`, `facets/`, `artifact_types/`, `morphisms/anchoring.json` |
| Area, Instance, SlotValue (§3.5, §3.7) | `model.h` | — |
| Goal type, Goal, ContextSet (§4) | `model.h`; `context_engine.h` | `goals/goal_types.json` |
| Decision, Fork, StatusRecord, Prediction, Product, Judgement (§5, §6.7) | `model.h`; `order_status_history()` | — |
| Storage (I10) | `knowledge_store.h`: `loom_kb_*` tables, created lazily; runs, indexed queries, judgement replay (I4) | — |
| Pipeline (§6) | `knowledge.h`: stages `catalog -> extract -> resolve -> assess -> generalize -> materialize` as resumable TaskEngine tasks, pack hash in every input hash | `policy/` thresholds, calibration, relevance, selection rules, evidence encoding |
| Temporal holdout (§7.1) | `model::PriorFilter` / `priors(pack, filter)`; `KnowledgeConfig.prior_cut` | every prior source carries a date |

Invariants enforced in code: every claim validates its assessment (observed
needs support, inferred needs an Expected Property and a check state, a
check state needs a property); the store refuses extrapolated/absent premises
and chained transfers (I3); judgements are append-only and replayed last;
`_meta.loom_schema_version` stays `"1"` (the layer records
`_meta.loom_kb_schema_version`); the pack validator rejects any name outside a
closed set, dangling references, relations that do not anchor on the
meta-model, transfers between kinds of different roles and undated priors.

### Context + materialize

`include/loom/context_engine.h`, `include/loom/materialize.h`,
`src/context/`, `src/materialize/`. Implemented (R12, R13 §1; R11; I8).

- **Goal typing** (`ContextEngine::type_goal`): a deterministic PL/EN
  cue-phrase classifier over `goals/goal_types.json` (longer, more specific
  cues score higher; ties broken by id), with an *optional* LLM fallback
  used only when the cue confidence is low AND `semantic_model` +
  `secrets.api_key` are configured (mirrors `SemanticLLM::enabled()`; never
  called in the default/offline configuration, so it never affects
  determinism by default). The owner may force a goal type; an unknown
  forced type is `Errc::NotFound`.
- **ContextSet builder** (`ContextEngine::select`): gathers candidates —
  invariant principles and preferences (stable band, goal-independent by
  construction so repeated calls share an identical prefix); the project's
  instance slots (role-filtered), decisions and status history (project
  band); claims one hop from the goal's targets in either direction (goal
  band) — scores each as relevance × authority (`model::authority_rank`) ×
  freshness (half-life decay against the corpus's own latest date, never
  wall-clock, so runs stay deterministic) × confidence, applies an
  MMR-style diversity pass per band, resolves each item's resolution
  (label/summary/full/raw) from the goal type's role map, then fills the
  three-band token budget in order with unused budget cascading forward
  into later bands, and finally closes the dependency graph (a claim's
  `premises.claims`/`premises.principles`) over what was accepted, marking
  `required_by`. Every included and dropped item carries a `why`.
- **Renderer** (`ContextEngine::render`): joins the already-rendered,
  evidence-marked (`policy/evidence_encoding.json`) item texts band by band
  under a heading; `trace()` returns the same items as sections-as-data
  (band → items, plus `dropped`) for a UI, CLI or test to inspect without
  re-parsing the prompt text; `build()` is a `type_goal` + `select` +
  `render` combinator — an additive integration point a caller (ChatEngine,
  the CLI, the server) can opt into. The legacy `ContextSelector` /
  `GraphMemorySelector` (Python parity, `graph_memory.h`) are untouched.
- **Materializer**: `self_description` renders `SELF.md` (projects, their
  component entities' status history including lost/restored oscillation,
  decisions, forks, areas, principles marked seed-vs-discovered, operators,
  open questions); `dossier` is a per-instance slot table with evidence
  markers, conflicts and transferred (by-analogy) slots; `backlog` collects
  contested claims, violated Expected Properties, `violates` claims, absent
  required slots and lost features; `extrapolated_spec` lists only
  `Extrapolated`-evidence slot values under "Proposals", never as fact (I3);
  `check_preferences` runs the `rules/checks.json` detectors
  (`string_array_literal`, `regex_line`, `regex_block`, via `re/regex.h`)
  over real files, returning `ProductCheck`s with provenance — a preference
  violation is a failing check, not a style note (I8). Every rendered
  artifact carries a content-derived `data.input_hash` (pack hash + run +
  sorted dependency ids) for incremental regeneration. `run_stage()` wires
  all of this into the `knowledge.materialize` pipeline stage: artifacts to
  `BlobStore` + `loom_artifacts` (+ `config.out_dir`), `Product` rows to the
  `KnowledgeStore`.
- C ABI: `loom_context_build` (`ContextRequest` JSON →
  `{"context_set","text"}`) and `loom_materialize`
  (`{"kind","run"?,"instance"?}` → `Rendered` JSON), `src/capi/capi_context.cpp`.
- Known gaps: goal-band candidate gathering is a 1-hop BFS from the goal's
  targets (no multi-hop expansion); target entities are taken as given
  (no free-text entity-mention resolution when `targets` is empty); the
  optional LLM goal classifier has no dedicated test against a real
  provider (only a `ScriptedTransport` one); `self_description`'s "open
  questions" section is a flat scan of every claim's `assessment.open.questions`
  (no dedicated store table yet, and no automatic matching to the decision
  that resolved one).

### Ownership map

Sources and tests are picked up by glob, so nobody edits `CMakeLists.txt`.
Foundation files are read-only for the areas; a needed change is additive and
goes through the lead (a new closed-set name is a model change).

| Area | Owns (create/replace freely) | C ABI file | Tests |
|---|---|---|---|
| **catalog** (R1) | `include/loom/catalog.h`, `src/catalog/**`, `loom_cat_*` tables, CLI `loom catalog ...` / `loom archive run --knowledge` in `cli/` | `src/capi/capi_catalog.cpp` | `tests/test_catalog*.cpp` |
| **extract+resolve** | `include/loom/extract.h`, `include/loom/resolve.h`, `src/extract/**`, `src/resolve/**` | `src/capi/capi_extract.cpp` | `tests/test_extract*.cpp`, `tests/test_resolve*.cpp` |
| **generalize** | `include/loom/generalize.h`, `src/generalize/**` | `src/capi/capi_generalize.cpp` | `tests/test_generalize*.cpp` |
| **context+materialize** | `include/loom/context_engine.h`, `include/loom/materialize.h`, `src/context/**`, `src/materialize/**` | `src/capi/capi_context.cpp` | `tests/test_context*.cpp`, `tests/test_materialize*.cpp` |
| foundation (lead) | `include/loom/{model,kb,knowledge_store,knowledge}.h`, `src/model/**`, `src/kb/**`, `src/knowledge/**`, `src/capi/capi_knowledge.cpp`, `loom/data/**`, `tools/gen_kb_*.py`, `loom.h` | `src/capi/capi_knowledge.cpp` | `tests/test_{model,kb_pack,pack_model,knowledge_store,knowledge,capi_knowledge}.cpp` |

The stage function of each area (`run_stage` / `run_resolve_stage` /
`run_assess_stage`) plugs into `KnowledgeEngine`. A stage returns `{"output": <hash of
what it wrote>, "stats": {...}}`, clears the rows it owns before writing
(rebuild) and checkpoints through `StageContext`. Data-pack additions an area
needs (a lexicon entry, a threshold) are pack edits reviewed by the lead.

### Extending with data only

Run `python3 tools/gen_kb_pack.py` after any edit under `loom/data/`
(`test_kb_pack` checks the embedded copy), then `ctest`: the validator reports
every problem with its file and JSON pointer.

- **Project kind**: `project_kinds/<id>.json` (schema `loom.kb.project_kind/1`)
  with domain kinds, each with exactly one `role` of the 14; relations between
  kinds must use a relation of `morphisms/anchoring.json` `relation_map` whose
  role relation links the two roles; list it in `pack.json`. Its anchoring
  morphisms are derived automatically; add transfer morphisms to analogous
  kinds in `morphisms/transfer.json`.
- **Facet**: `facets/<id>.json` with `applies_to`; list it in the host
  kinds' `facets`.
- **Artifact type**: `artifact_types/<id>.json`: `medium`, `detect` ops,
  `parse.segment` segmenters, `extract` extractors (closed sets in `kb.h`), and
  the `structure` of its instances.
- **Morphism**: an entry in `morphisms/transfer.json` between two kinds of the
  same role, with the Expected Property a transferred inference vouches for.
- **Principle / operator**: an entry in `philosophy/principles.json` /
  `operators.json` with level + form (principles), bilingual text, verbatim
  phrasings, and dated `sources` (the date is when the owner's text existed:
  temporal holdout). Seeds are always `candidate`; the engine promotes.

## Code ownership

Each area owns its headers, `src/<dir>/`, one `src/capi/capi_<area>.cpp` and
its own tests. Constructor signatures in the headers are the wiring contract;
additive header changes are fine, anything else goes through the lead.
