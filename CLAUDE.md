# CLAUDE.md — ChatADHD Codebase Guide

> **ZASADA WŁAŚCICIELA (2026-09-30), OBOWIĄZUJE ZAWSZE:** NIE PODEJMUJEMY DECYZJI ZA UŻYTKOWNIKA,
> ZWŁASZCZA OGRANICZAJĄCYCH. WSZYSTKO JEST KONFIGUROWALNE: MODEL, ZAKRES, ROZUMOWANIE, AUTOMATYCZNE
> PRZYJMOWANIE WYNIKÓW, CO WYSYŁAMY. USTAWIENIA MUSZĄ POZWALAĆ PRZEPALIĆ NAWET MILIARD DOLARÓW.
> JEDYNY WYJĄTEK: SPODZIEWANY WZROST ZUŻYCIA O RZĄD WIELKOŚCI (×10) POTWIERDZA UŻYTKOWNIK.
> DOMYŚLNE WARTOŚCI TO PRESETY, NIE REGUŁY. NIE WYMYŚLAJ OGRANICZEŃ.

This file is the primary reference for AI assistants working on this repository.

## Working with subagents (owner, 2026-10-10)

Run subagents **one at a time**, as a queue — no parallel agents and no workflow
fan-outs unless the owner explicitly asks for them. The owner checks in roughly
once per five hours, and Claude's usage is limited per window: progress should
continue between check-ins systematically and without loss, not in bursts.
Measured on 2026-10-10: five parallel agents used up the window in 8 minutes
(57.5M cache-read and 1.34M cache-write tokens) and everything then stalled for
4.5 hours. Every limit hit also costs re-caching each live context
(0.2–0.46M tokens per agent and for the main session), and workflow agents
cannot be resumed at all, so their work is lost.

- Each agent works in its own git worktree and commits a checkpoint after every
  meaningful piece (WIP commits are fine), so nothing depends on its context.
- After a limit, resume the interrupted agent with its transcript (SendMessage)
  instead of restarting it. A fresh agent with a precise brief (files, lines,
  decisions already taken) is cheaper than resuming a very large context.
- Keep contexts small: targeted reads (grep, line ranges), no dumps of large
  files or logs; native builds serialized and run once per change.

## Project Overview

**ChatADHD** (v0.07.09) is a mobile-first AI chat client built with Python and Kivy.

Core capabilities:
- Multi-model chat via OpenRouter (Claude, GPT, Gemini, DeepSeek, etc.)
- Streaming responses with reasoning/thinking token display
- Branching conversations (edit a message → old version preserved, new branch created)
- Hierarchical memory tree injected into each LLM prompt
- Real-time knowledge graph built from semantic analysis of messages
- 3-tier semantic search: embeddings → TF-IDF → keyword fallback
- Optional AES-256-GCM zero-knowledge encryption
- Universal conversation importer (ZIP, JSON, HTML, MHT, SQLite, Markdown, text, screenshot OCR)
- Voice input via Groq Whisper or Google Speech
- Bidirectional GitHub sync
- Themes: Dark / AMOLED
- Android + desktop support (single codebase)

## Architecture: KOD ≠ DANE

The most important architectural principle:

- **KOD** (code) — lives wherever you unzip it. Versioned. Replaceable.
- **DANE** (data) — lives in a fixed, persistent directory. Never overwritten by upgrades.

```
CODE (this repo)                    DATA (~/.chatadhd/ or CHATADHD_DATA)
────────────────────────────        ────────────────────────────────────
main.py                             .chatadhd_data   ← sentinel file
core/                               config.json
engine/                             secrets.json     ← owner-only perms (600)
gui/                                chatadhd.db      ← SQLite WAL
requirements.txt                    memory.json
                                    models.json
                                    attachments/
                                    exports/
                                    logs/
```

Data directory resolution order (see `engine/paths.py:resolve_data_dir`):
1. `CHATADHD_DATA` env var or explicit override
2. First directory containing `.chatadhd_data` sentinel file
3. First candidate path (created automatically)

Desktop default: `~/.chatadhd/`
Android: `/storage/emulated/0/Documents/ChatADHD/`

## Directory Structure

```
chatadhd/
├── main.py              # Entry point — ChatADHDApp (Kivy App subclass)
├── requirements.txt
├── core/
│   ├── crypto.py        # AES-256-GCM encryption (CryptoEngine)
│   ├── semantic.py      # Regex-based NER + topic extraction (SemanticAnalyzer)
│   └── selector.py      # 3-tier search engine (SelectorEngine)
├── engine/
│   ├── paths.py         # KOD≠DANE path resolution
│   ├── config.py        # Config + Secrets JSON stores
│   ├── db.py            # Thread-safe SQLite (Database)
│   ├── events.py        # Pub/sub event bus (EventBus)
│   ├── chat_engine.py   # API calls + conversation management (ChatEngine)
│   ├── memory_engine.py # Hierarchical memory tree (MemoryEngine)
│   ├── models.py        # OpenRouter model registry (ModelRegistry)
│   ├── graph_engine.py  # Real-time knowledge graph builder (GraphEngine)
│   ├── graph_memory.py  # Graph-based context selector (GraphMemorySelector)
│   ├── semantic_llm.py  # LLM-powered analysis with regex fallback (SemanticLLM)
│   ├── semantic_worker.py # Background analysis daemon (SemanticWorker)
│   ├── batch_api.py     # Anthropic Message Batches API
│   ├── importer.py      # Universal conversation importer (ConversationImporter)
│   ├── github_sync.py   # GitHub bidirectional sync (GitHubSync)
│   └── providers.py     # OCR/ASR provider manager (ProviderManager)
└── gui/
    ├── base.py          # Theme engine, shared widgets (C, LOGBUF, RBtn, set_theme)
    ├── chat_panel.py    # Main chat UI (ChatPanel)
    ├── conv_panel.py    # Conversation list (ConvPanel)
    ├── memory_panel.py  # Memory tree UI (MemoryPanel)
    ├── dialogs.py       # Settings, pickers (SettingsPopup)
    ├── import_panel.py  # Threaded import UI (ImportPanel)
    ├── voice_panel.py   # Voice input UI
    ├── github_panel.py  # GitHub sync UI
    └── graph_viz.py     # Force-directed graph visualiser (GraphExplorerPanel)
```

## Key Modules In Depth

### `engine/db.py` — Database

- SQLite with WAL journal mode. Schema version: `_SCHEMA_VERSION = 4`.
- All writes acquire `threading.RLock`. Safe from Kivy UI thread + background threads.
- **Tables:** `_meta`, `conversations`, `messages`, `nodes`, `links`
- **Schema migrations** are forward-only, guarded by `has_column()` checks.
- Message statuses: `active` | `excluded` | `version` | `deleted`
- `semantic_status` column on messages: `pending` → `done` (drives the background worker)
- IDs are prefixed hex strings: `c_`, `m_`, `vg_`, `n_`, `l_`
- Batch insert via `batch_create_msgs()` for bulk imports (1000/transaction)

### `engine/chat_engine.py` — Chat Engine

- Constructs LLM message arrays: system prompt → memory context → graph memory → conversation history → current message
- Calls OpenRouter `/chat/completions` via `requests`
- Supports streaming (SSE `data:` lines) and non-streaming
- Web search: adds `{"id": "web"}` plugin to payload
- Reasoning: configures `reasoning` dict based on model name heuristics
- Emits `MSG_CREATED` event after each message save → triggers GraphEngine

### `engine/events.py` — Event Bus

Synchronous pub/sub singleton `bus`. All handlers run in the emitter's thread.

Named event constants to use (never raw strings):
```python
MSG_CREATED, MSG_UPDATED, NODE_CREATED, EDGE_CREATED,
CONV_CREATED, CONV_SWITCHED, GRAPH_CHANGED, IMPORT_DONE
```

`SEMANTIC_PROGRESS` is defined in `engine/semantic_worker.py`.

### `engine/config.py` — Configuration

- `Config` — non-secret settings, persisted to `config.json`. Auto-upgrades on load.
- `Secrets` — API keys + passwords, persisted to `secrets.json` with `chmod 600`.
- Both extend `_JsonStore` which uses atomic write (`.tmp` → rename).

**Config defaults** (from `DEFAULTS` dict):
| Key | Default |
|-----|---------|
| `base_url` | `https://openrouter.ai/api/v1` |
| `default_model` | `anthropic/claude-sonnet-4-20250514` |
| `semantic_model` | `""` (disabled) |
| `temperature` | `0.7` |
| `max_tokens` | `4096` |
| `theme` | `dark` |
| `stream` | `True` |
| `semantic_analysis` | `True` |
| `graph_memory_depth` | `2` |
| `graph_memory_max_nodes` | `20` |

**Secrets keys:** `api_key`, `anthropic_batch_key`, `github_token`

### `engine/memory_engine.py` — Memory Tree

- In-memory tree persisted to `memory.json` (atomic write via `.tmp`).
- `MemoryNode` dataclass fields: `id`, `content`, `parent_id`, `node_type`, `active`, `depth`, `weight`, `tags`, `created`, `metadata`
- Node types: `text` | `folder` | `file` | `dir`
- `get_active_context(max_chars=16_000)` — serialises active nodes into text for LLM system prompt
- Auto-tags on `add_node()` via `SemanticAnalyzer.extract_topics()`

### `core/semantic.py` — Semantic Analysis

Three-level pipeline, zero deps at base level:
1. **Regex NER** (`extract_entities`) — emails, URLs, IPs, dates, money, code refs, etc.
2. **Topic tagging** (`extract_topics`) — keyword-count-based; built-in topics: `legal`, `finance`, `tech`, `ai_ml`, `security`, `health`, `project_mgmt`
3. **Relation extraction** (`extract_relations`) — heuristic SVO patterns

Module-level singleton: `from core.semantic import analyzer`

### `core/selector.py` — Search Engine

Auto-detects best available tier at import:
- **Tier 1**: `sentence-transformers` embeddings (`all-MiniLM-L6-v2`)
- **Tier 2**: scikit-learn TF-IDF cosine similarity
- **Tier 3**: keyword substring matching (always available)

Usage: `sel.index(texts, ids=None)` then `sel.search(query, top_k=5)`

### `engine/semantic_worker.py` — Background Worker

Daemon thread draining `semantic_status = 'pending'` messages. Three modes:
- **regex**: instant, local, free
- **llm**: one-by-one via OpenRouter (~2 req/s throttle)
- **batch**: Anthropic Message Batches API (50% cheaper; activates when >500 pending and `anthropic_batch_key` is set)

Reports progress via `SEMANTIC_PROGRESS` event. Wakes on `IMPORT_DONE` event.

### `engine/graph_engine.py` — Knowledge Graph

Subscribes to `MSG_CREATED`. For each message:
1. Runs `SemanticLLM.analyse()` (falls back to regex if LLM unavailable)
2. Entities → `nodes` table rows (kind: entity/topic/etc.) + `links` (`mentions`)
3. Topics → topic nodes + `links` (`tagged_with`)
4. Relations → edges between existing entity nodes
5. Emits `GRAPH_CHANGED` if anything was written

`ingest_analysis()` is the public API called by `SemanticWorker` for batch processing.

### `engine/importer.py` — Universal Importer

Auto-detects format from extension + content sniffing.

Supported formats: `zip` | `sqlite` | `json` | `jsonl` | `html` | `mht` | `screenshot` | `markdown` | `text`

All format handlers converge to `_import_message_list()` which uses `db.batch_create_msgs()` and emits `IMPORT_DONE`.

Large JSON files (>5 MB) are stream-parsed to avoid OOM.

Handles: Claude.ai exports (`chat_messages` key), ChatGPT (`mapping` tree), OpenAI API format, generic role/content arrays.

### `core/crypto.py` — Encryption

AES-256-GCM with PBKDF2-SHA256 key derivation (600,000 iterations). Optional — gracefully degrades to no-op if `cryptography` package missing.

Module-level singleton: `from core.crypto import crypto`

### `gui/base.py` — Theme + Shared Widgets

- `C` dict — active colour palette (mutate with `set_theme(name)`)
- `LOGBUF` — `LogBuffer` circular buffer (500 entries) for in-app log display
- `RBtn` — rounded button with background colour support
- `THEMES` dict has `dark` and `amoled` entries

## Conventions

### ID Generation

All IDs: prefixed 12-char hex from `uuid4().hex[:12]`
- Conversations: `c_`
- Messages: `m_`
- Version groups: `vg_`
- Nodes: `n_`
- Links: `l_`

### Thread Safety

- **UI thread** (Kivy main): all widget operations must happen here
- **Background threads**: `SemanticWorker`, streaming API calls
- Cross-thread UI updates: use `Clock.schedule_once(lambda dt: ..., 0)`
- DB writes: protected by `threading.RLock`
- Event bus: synchronous — handlers run in emitter's thread; UI event handlers must use `Clock.schedule_once`

### Error Handling

- No silent `except: pass` — always log at minimum `log.debug(..., exc_info=True)`
- API errors raise `ValueError` with message for UI display
- Optional features (encryption, embeddings, LLM semantic) degrade gracefully via try/except at import or call time
- SemanticLLM disables itself after 5 consecutive failures

### File Persistence

- All JSON writes are atomic: write to `.tmp`, then `Path.replace()` (rename)
- `secrets.json` gets `chmod 600` (owner-only) on every save
- DB uses WAL + `synchronous=NORMAL` for crash resilience

### Logging

All modules use `logging.getLogger(__name__)`. Logging is configured in `main.py` before any imports:
```python
logging.basicConfig(level=logging.DEBUG, format="%(asctime)s [%(levelname)-5s] %(name)s: %(message)s")
```

### Kivy / GUI Patterns

- All styled widgets import from `gui.base`, not Kivy directly
- `dp()` for layout dimensions, `sp()` for font sizes
- Panels have a `toggle()` method and a `_visible` flag
- `refresh()` re-renders panel contents from current engine state

## Running the App

```bash
# Minimum deps
pip install kivy requests
python main.py

# With encryption
pip install cryptography

# With TF-IDF semantic search
pip install scikit-learn

# With embedding search (best quality)
pip install sentence-transformers
```

Environment variables:
- `CHATADHD_DATA` — override data directory path
- `KIVY_LOG_LEVEL` — Kivy log verbosity (`debug`, `info`, `warning`, `error`)

## Configuration: First Run

1. Launch `python main.py`
2. Open Settings (gear icon in chat panel)
3. Set **API Key** (OpenRouter key for chat; `anthropic_batch_key` optional for batch semantic)
4. Set **Model** (default: `anthropic/claude-sonnet-4-20250514`)
5. Optionally set **Semantic Model** (cheap fast model like `anthropic/claude-haiku-4-5` for background analysis)

## Database Schema Summary

```sql
conversations(id, title, created, updated, source, metadata)
messages(id, conv_id, parent_id, role, text, model, status,
         version_group_id, version_num, weight, attachments,
         metadata, created, semantic_status)
nodes(id, kind, label, content, tags, metadata, created)
links(id, src, dst, link_type, weight, metadata, created)
_meta(key, value)
```

`messages.metadata` and `nodes.metadata` store JSON blobs. `messages.attachments` stores a JSON array of file paths.

## Extending the Codebase

### Adding a new config key

1. Add to `DEFAULTS` dict in `engine/config.py`
2. Config auto-upgrades on next load (bumps `_config_version` to 3+)
3. Access via `self.config.get("new_key", fallback)`

### Adding a new event

1. Add constant to `engine/events.py`: `MY_EVENT = "my:event"`
2. Emit with `bus.emit(MY_EVENT, data_dict)`
3. Subscribe with `bus.on(MY_EVENT, handler_fn)`
4. For UI handlers: `bus.on(MY_EVENT, lambda d: Clock.schedule_once(lambda dt: handler(d), 0))`

### Adding a new import format

1. Add extension detection to `ConversationImporter.detect_format()` in `engine/importer.py`
2. Add handler method `import_<format>(path, title)`
3. Add dispatch in `import_file()` method
4. All handlers should call `self._import_message_list(messages, title)` with a list of `{"role": ..., "content": ...}` dicts

### Adding a new graph node/edge type

- Node kinds: `entity` | `topic` | `concept` | `code_ref` | `file` | `person` | `org` | `message` (and any new string)
- Edge types: `mentions` | `depends_on` | `references` | `reply_to` | `part_of` | `generated_by` | `tagged_with` | `derived_from` | `contradicts` | `implements`
- Add new kinds/types by just using them — no enum or registry to update

### DB schema changes

1. Increment `_SCHEMA_VERSION` in `engine/db.py`
2. Add `ensure_column()` calls in `_migrate()`
3. Guard any new indexes with `ensure_index()` and a column check
4. Never use `CREATE TABLE` with new columns — use `ALTER TABLE ADD COLUMN`

## Current project state

The canonical, dated state of the project (verified numbers, open work, what is
needed from the owner) is `docs/STATE.md`. Read it first.

## Loom (C++ Core)

`loom/` is a C++20 port of `engine/` + `core/`: one kernel library
(`libloom`) meant to be shared by every client (Kivy today via this repo,
plus a CLI, an HTTP server + React web UI, and an Android/JNI shell), reading
and writing the *same* data directory as the Python app. Full detail lives in
`loom/README.md` — this section is only what an assistant needs to know
before touching it.

**Layout** (`loom/`):
```
include/loom/     public headers — the contract; loom.h is the C ABI
src/               implementation; src/capi/ = C ABI, one file per area
src/archive/       Archive Intelligence / Project Compiler pipeline
tests/             doctest unit tests (test_<area>.cpp)
tests/compat/      Python-vs-C++ differential tests + loom_compat_tool
cli/               `loom` command line
server/            REST/SSE facade over loom.h (cpp-httplib)
web/               React + TypeScript web UI (Vite), talks to server/
android/           JNI shell
third_party/       vendored deps (nlohmann/json, cpp-httplib, doctest, SQLite, miniz)
```

**Build & test**:
```bash
cd loom
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```
Presets: `dev` (debug + libloom.so), `release`, `asan` (ASan+UBSan), `tsan`
(ThreadSanitizer, vendored SQLite), `vendored` (SQLite amalgamation, what
Android uses). Sources/tests are picked up by glob — adding a file never
needs a `CMakeLists.txt` edit. For `loom/web`: `npm ci && npm run build &&
npm run e2e` (Playwright/Chromium, see `loom/web/e2e/`).

**Ownership conventions**: each area owns its public header(s), its
`src/<dir>/`, exactly one `src/capi/capi_<area>.cpp`, and its own
`tests/test_<area>.cpp`. Constructor signatures in the headers are the
wiring contract between areas — additive header changes are fine; anything
else goes through the area's lead. The C ABI (`loom.h`) never throws across
the boundary (every `capi_*.cpp` function is wrapped by an exception
firewall), returns caller-freed heap JSON strings, and is covered by
`test_abi_compat.py` so `libloom.so` never exports more or less than
`loom.h` declares.

> **Update 2026-09-29 (owner):** Python compatibility is no longer required — it was
> only a minimum plan. The invariant below remains enforced by tests as a regression
> sentinel until a deliberate schema migration; it does not constrain new work.

**Python↔C++ compat invariant**: Loom and the Python app share one on-disk
format and must stay indistinguishable to a reader of the data directory —
same schema DDL (`schema_version` stays `"4"`), same forward-only guarded
migrations, same ID/timestamp/JSON formatting (`uuid4().hex[:12]`,
`utcnow().isoformat()+"Z"`, `json.dumps`-identical output), same file names
and data-dir resolution order. `tests/compat/` enforces this by running the
same operations through `engine.db.Database` and through Loom (separate
files, one shared file in alternation, two processes at once) and diffing
the results byte-for-byte; other compat tests cover JSON/float formatting,
UTF-8/`str` semantics, config/secrets and the data-dir resolution order
itself. **Any change to `engine/db.py`, `engine/config.py`, ID generation, or
JSON serialization in Python must have a matching change on the Loom side
(and vice versa) in the same commit/PR**, or a compat test will start
failing — that failure is the invariant working, not a false positive.
Deliberate, documented differences (e.g. row-ordering ties broken by rowid)
are listed in `loom/README.md`'s Decisions table; anything not listed there
is a bug.

## Security Notes

- `secrets.json` stores all credentials; never log its contents
- Encryption is optional and local-only; LLM providers always receive plaintext
- The `secrets.get("api_key")` pattern is the standard for credential access
- `_lock_perms()` in `_JsonStore` applies `chmod 600` — may silently fail on Android
- No telemetry, no analytics, no remote tracking of any kind

## Version History

| Version | Notable changes |
|---------|----------------|
| 0.7.9 | Background SemanticWorker with batch API support |
| 0.7.8 | Graph memory selector |
| 0.7.7 | LLM semantic analysis |
| 0.7.6 | Graph engine real-time |
| 0.7.5 | Universal importer streaming |
| 0.7.0 | Knowledge graph, force-directed viz |
| 0.6.x | Memory tree, semantic search |
