# CLAUDE.md — ChatADHD Codebase Guide

> **ZASADA WŁAŚCICIELA (2026-09-30), OBOWIĄZUJE ZAWSZE:** NIE PODEJMUJEMY DECYZJI ZA UŻYTKOWNIKA,
> ZWŁASZCZA OGRANICZAJĄCYCH. WSZYSTKO JEST KONFIGUROWALNE: MODEL, ZAKRES, ROZUMOWANIE, AUTOMATYCZNE
> PRZYJMOWANIE WYNIKÓW, CO WYSYŁAMY. USTAWIENIA MUSZĄ POZWALAĆ PRZEPALIĆ NAWET MILIARD DOLARÓW.
> JEDYNY WYJĄTEK: SPODZIEWANY WZROST ZUŻYCIA O RZĄD WIELKOŚCI (×10) POTWIERDZA UŻYTKOWNIK.
> DOMYŚLNE WARTOŚCI TO PRESETY, NIE REGUŁY. NIE WYMYŚLAJ OGRANICZEŃ.

This file is the primary reference for AI assistants working on this repository.

## Decisions (owner, 2026-10-11)

The owner supplies abstractions, philosophy and examples; **technical decisions
are Claude's job**. Do not ask the owner technical questions or offer menus of
options: decide according to R1–R53 (everything configurable, defaults are
presets, no invented limits), record the decision and how to reverse it in
`docs/reports/INDEX.md`, and move on. Ask the owner only about his money beyond
the agreed budget, sending his private data to a new provider, or a product
direction that contradicts the requirements — and then as a yes/no question.


**Spending authority (owner, 2026-10-24):** „możesz podejmować decyzje odnośnie zużycia w przypadkach jeśli nie
przekroczysz limitu euro dziennie trzech euro tygodniowo albo jeśli jest duży prawdopodobieństwo że przez działanie
sumaryczne doprowadzisz do oszczędności. Vertex ma wiele zalet”. Read as: cloud spending decisions without asking while
new spend stays ≤ 1 €/day and ≤ 3 €/week (my reading of „euro dziennie”, to confirm), or when the action very probably
saves money overall. Credits count as his money too. Everything else (and his private data to a new service) stays a
yes/no question.

**Budget for 2026-10-25 only (owner):** „dzisiaj … śmiało możesz wziąć 3 € z open router jak trzeba, z Google tam gdzie są kredyty to po 10% każdego możesz wziąć, a tam gdzie trzeba będzie zapłacić 5 € możesz rachunek nabić”. Today: OpenRouter up to 3 €; Google credits up to 10 % of each credit (≈ 6 € of the €59.97 pool, ≈ 85 € of the €855 GenAI App Builder credit); paid charges up to 5 €. From 2026-10-26 the standing rule (≤ 1 €/day, ≤ 3 €/week) applies again.

**Credits are separate (owner, 2026-10-27):** „to 1 dziennie to się nie tyczy tych kredytów które tam mamy tamte to 10% do wykorzystania bez pytania a na ageds nawet więcej jak trzeba”. The ≤ 1 €/day, ≤ 3 €/week rule covers paid charges only; Google credits may be used up to 10 % of each credit without asking (standing, not only 25.10), and AGEDS may use more than 10 % when needed (report the amount).

**Data to Google Vertex (owner, 2026-10-25, R100):** all his data may be sent to Vertex AI, on the condition that it is not made public and not used for training (Vertex does not train on customer data by default; keep it that way). Vertex caching stays ON (cost); do not disable it.

**Before a question (owner, 2026-10-23):** „upewniając się że dobrze rozumiesz to zanim zaczniesz mi
zadawać pytania, to przeanalizuj twoje zrozumienie w kontekście filozofii. Bo jeśli coś się nie zgadza
i nie było wyraźnie zaznaczone to najpewniej źle rozumiesz”. So a doubt is first checked against the
philosophy (everything configurable, defaults are presets, examples are examples, nothing decided for the
user, the engine covers every configuration): a mismatch that he did not mark explicitly is most likely my
misunderstanding, and I repair my understanding instead of asking. Only what survives that check — a fact
only he knows, his money, his private data, a contradiction of a requirement — becomes a question, shown
together with the analysis that led to it. When the answer would only select one case among possible ones,
the design covers all cases (R59) and no question is asked.

## How the owner works: long series of messages (owner, 2026-10-23)

> „możesz też zapisać jako uwagi nt. mojego sposobu pracy, że komunikuje się jak mam czas i wtedy każdą
> myśl na bieżąco pisze później pracuje nad następną więc się możesz spodziewać dłuższych serii”

He talks to the assistant when he has time, writes each thought as it comes, then goes back to working on
the next thing — so expect **longer series** of messages (several in a row, minutes apart, each a
correction, an addition or a new question), then silence. What follows from it (coordinator's decisions,
presets, reversible):

- A series is one stream of thought spread over time, not a list of separate orders. Every message is
  recorded durably **at once** (verbatim, with an id: `OWNER_REQUIREMENTS`, later `tools/spec.py add`);
  that is cheap and loses nothing.
- **Expensive work starts only after approval (owner, 2026-10-23, later message, supersedes the 15-minute
  timer I had set).** „droga praca, to startujesz po upewnieniu się że dobrze zrozumiałeś, parafrazując
  wymagania i prosząc o ewentualne korekty i zatwierdzenie”. Expensive = a Fable stage, a multi-agent
  workflow, any package beyond a trivial fix (preset; cheap and free of approval: read-only checks, gates,
  log readers, recording his words, small fixes). Procedure: paraphrase the requirement in my own words,
  mark what is an example, show the philosophy check (below), ask for corrections and approval, record the
  approved understanding (`understanding` of the spec node), only then start. An approved paraphrase stays
  valid for the packages that implement it. A running agent is never restarted for a correction (waste
  control 6): the correction is recorded and applied as an addendum after the agent ends.
- Replies inside a series are short: what was understood, what was recorded, what changed. The deep dive
  (reminder of the base, then the details, the non-intuitive ones) belongs to a question and to the end of
  a series, as one consolidated answer with the list of what the series changed.
- **Thread style (owner, 2026-10-23 00:1x, after switching the coordinator to Fable):** „nie gadaj tyle bo ja i tak
  tego nie czytam; oprócz konwersacji gdzie dyskutujemy to w tym wątku wpisz tylko co zaczynasz implementować,
  co kończysz, co budujesz, co nie wyszło, co agenci wymyślili oryginalnego”. So: status lines only (started /
  finished / building / failed / original ideas from agents), one line each; discussion only when he opens
  it; a paraphrase for approval (R61) is at most five lines. Long reasoning goes to `docs/reports/QUEUE_S1.md`,
  not to the chat. He also set the coordinator model to Fable himself („jesteś Fable, baw się dobrze, korzystaj
  z Ultra koda z rozsądkiem, tym większym im droższe zadanie”) — workflows scaled to the cost of the task.
- **R93 (owner, 2026-10-23 ≈ 19:1x UTC) supersedes the "status lines only" shorthand:** „sprawozdanie mi dawaj pełnymi
  wypowiedziami Nie oszczędzaj aż tak tokenów bo nie rozumiem o co ci chodzi jak tak piszesz skrótami. słowa ze slangu
  Koterskiego to możesz stosować Jak widzisz że Ja je stosuję. tak to po ludzku mi gadaj”. So: reports in full sentences and
  plain Polish; every working name or abbreviation (package ids, gate labels, session names, CI, FF, sha) is said in words
  at first use or left out; slang only when he uses it himself. Short is still good — but never at the cost of his
  understanding; the long reasoning still goes to `docs/reports/QUEUE_S1.md`. **R95 (same day, minutes later) narrows it:**
  „Tamta Moja uwaga odnosiła się tylko do słownictwa - żebyś ogranicza używanie słów co do których nie masz pewności że
  dobrze definicję kumam” — so the rule is about WORDS only (no unexplained working names or jargon), not about density:
  do not explain obvious things, and never the same thing twice („60-70% tłumaczysz rzeczy oczywistych, niektóre już
  któryś raz”). Normal density, plain words.
- **R62/R63 (2026-10-23):** chat output costs his time more than tokens — status lines only when they
  carry news. While he is away (tens of hours) and the coordinator knows with high probability what would
  satisfy him, it acts without waiting; the R61 paraphrase is then recorded as `proposed` for his review.
  A stronger coordinator model may loosen the procedural rules a little (they were written for Sonnet);
  the philosophy itself stays.
- **A line for him to paste into a Claude prompt must read as a sentence, never as a shell command (owner, 2026-10-23 20:4x:
  „zmyliłeś mnie bo jak widzę git pull &&... to piszę w bashu”).** Start with a verb addressed to Claude („przeczytaj…”,
  „zrób git pull, potem…”); a leading `git …`/`cd …` makes him type it into bash.
- **Weekly limits (owner, 2026-10-23 20:4x, R91):** Fable may run to 100 % of its weekly bar; every other model stops at
  90 % of „All models” (preset `caps` in `tools/pace_plan.json`). Usage credits are ON in his account (40 EUR/month cap, his own
  setting, dated observation 2026-10-23): past 100 % the platform spends credits, not nothing.
- His silence is not agreement and not a pause in the work: the queue (`docs/reports/QUEUE_S1.md`) must
  hold the whole state, because he reads it in bursts.

## Lists are examples (owner, 2026-10-16)

> „o ile nie mówię inaczej, to listy które podaję, na przykład podstawowe węzły w podstawowym
> widoku root grafu (ustawienia, użytkownik, io, narzędzia, konektory, graf, aplikacja, dostawca,
> środowisko, automatyzacje, eksperymenty…) to są tylko przykłady na których masz się wzorować
> (jak też na filozofii), wypełniając tę listę pasującymi rzeczami które już są w apce, ale również
> takimi o których nie mówiłem, ale widzisz, że by były albo potrzebne, albo fajne”

Every list the owner gives (node kinds, menu items, settings groups, perspectives, formats,
flows…) is a seed, not a scope: fill it with what already exists in the app and with what the
philosophy implies, and say what was added. Unless he says a list is closed, it is open.

## Examples are examples; the engine handles every configuration (owner, 2026-10-23)

> „widzę w tabelce wpisałeś przypisanie jako wymagania, co się kłóci z filozofią. unikaj zapisywania
> przykładów bez wyraźnego zaznaczenia że to są przykłady które silnik i abstrakcje mają obsłużyć we
> wszelkich możliwych konfiguracjach, mało użyteczne (typu przypisywanie wielu reprezentacjom tej samej
> wartości w tym samym kontekście z takimi samymi modyfikatorami, konkretny przykład: starsze wersje
> robią się przezroczyste, mniejsze, oddalone w perspektywie, bardziej czerwone, mniej zielone, niebieskie
> bardziej oddalone od 128, przy czym kierunek oddalenia jeszcze od czegoś innego wynika ) również.
> wyzwalając ewentualnie, zwracając uwagę na niewielką użyteczność i pytając czy to jest celowe i czy
> następnym razem nie pytać”

- A concrete assignment (which channel carries which measure, which colour means what, what is shown
  first) written anywhere — plan, spec, brief, report, doc, UI text — is either a **labelled example**
  ("przykład") or a **default preset in data**; it is never a requirement, and it is never attributed to
  the owner ("jak mówiłeś") unless it is his quoted statement.
- The engine and its abstractions must handle **every** configuration, also those of little use: the same
  value shown by many representations in the same context under the same modifiers; a mapping whose
  direction depends on another variable (his example: older versions turn transparent, smaller, more
  distant in perspective, redder, less green, blue pushed away from 128 — and the direction of the push
  itself follows from something else). Nothing is rejected for being unusual.
- Little usefulness — or an objectively degenerate result (e.g. nothing visible) — is not an error: it
  raises a **non-blocking advisory** that says what looks of little use and why (evidence, not a verdict),
  asks whether it is intended, and offers "don't ask next time" (remembered like `declined`, reversible in
  settings; how often to ask, up to never, is a configurable preset).
- **A number in a list is never the list (owner, 2026-10-23 09:1x: „żadne dwa pokrętła, nie zamykaj list wybranymi nie
  zawsze reprezentatywnymi przykładami”).** When he names two limits or three settings, writing „two knobs” or „three
  positions” closes the list; the record says „limits such as …” / „positions such as …”, and the engine takes an open
  set. His examples may be unrepresentative on purpose; their choice never defines the range.
- **Option lists only where necessary (owner, 2026-10-23 09:2x: „listy opcji definiujemy jedynie wtedy gdy są
  konieczne”).** The default shape of a setting is an open value (number, text, range, predicate, reference to data)
  with presets as examples; a closed list (enum, fixed set of allowed values) exists only where the thing itself forces
  it (a provider protocol, a file format, the law), with the reason and the date recorded so it can be opened again
  when the reason goes away. No enums invented for convenience in the kernel or in the packs.
- Every agent brief carries this rule (`tools/workflows/single-package.js`).
- **Scripts and tools too (owner, 2026-10-23 07:5x, first run of the VM bootstrap: it stopped on „skrypt jest pisany pod
  Ubuntu 24.04” while his VM runs 26.04 — „A słyszałeś kiedyś o mojej tzw. filozofii?”).** An observation about the
  environment (OS release, user name, a package missing in that release, a tool version) is never a stop: the script
  says what it was checked on and continues, skips what it cannot do with an advisory, and repeats all advisories in
  its summary; a stop is reserved for a real impossibility (no package manager at all, no permission) and is opt-in
  otherwise (`--strict-os`). The same holds for defaults: what the script can do for the person it does by default
  (e.g. the `/home/user` link) with an opt-out, instead of a flag he has to discover after a failure.
- **A brief is a list of examples too (owner, 2026-10-23, second message).** When the coordinator hands an
  agent choices phrased as requirements, the agent reads them as examples of the functionality of the
  engine and its abstractions: it builds the general mechanism; the brief's case is one instance and one
  test, never the scope.
- **Absence today is not absence forever (owner, 2026-10-23, third message).** "No known consumer / tool /
  format / feature today" is never a design argument for dropping or demoting an option: the ecosystem has
  other consumers than the ones we know, and we are building a new paradigm in which our own output may be
  what others come to read. A fact about the present state of the world (who imports what, which app has
  which feature) is a **dated observation** with a validity window and a source rating (R59 ledger), may
  feed an advisory, and is never a constraint.
- **Check before use (coordinator's rule, 2026-10-23, after a Haiku review of two Fable designs found
  8 blocking and 26 wording violations of R58–R60).** Every design document and every brief gets a cheap
  read-only check (Haiku, 4 anti-patterns: a choice written as a requirement or attributed to the owner
  without his verbatim words; an option demoted for cost or usefulness; a present-state fact used as a
  constraint; a fact the document itself marks unverified) plus the R60 question, BEFORE it is used; the
  findings are claims to verify, the fixes run on Sonnet as an addendum, never as a restart.
- **Necessary, insufficient.** Examples are necessary and not sufficient; only the full exhaustion of the
  list of possibilities is sufficient. Accepting an engine feature therefore means: the examples pass AND
  the possibility space is covered (generative / property-based tests where feasible), and what stays
  uncovered is reported as a gap, not as out of scope.
- **How a list is filled, three stages:** (1) the most popular, obvious examples as the seed; (2) web
  research before building a release (build-time discovery, results as reviewable data); (3) at runtime an
  "unsupported formats" agent that, meeting a format that did not exist before, searches the web for
  everything about it and produces a profile as data for the person to confirm (web and model calls only
  when the person enables them, with a cost estimate; queries built from structural descriptors, never from
  the content of his data).
- **Information from the web:** official documentation first; then reliable forums and professional
  sources; where nothing reliable exists, analysis of the data supported by analysis of the weaker sources
  and TESTING what was learned there. Test outcomes feed back into a reliability rating of the source and
  of the author and, with enough data, depend on time (formats and behaviours drift with versions).
  Precedent to extend: `loom/data/policy/calibration.json` (Beta reliabilities of extractors and rules,
  used in `loom/src/resolve/assess.cpp`).

## No user-visible text in code (owner, 2026-10-30)

> „Boziu stringi w kodzie to największy grzech! od razu niech wstawi obsługę pakietów językowych”

Every text a person sees lives in data (language packs), never in code. A new language is data only, with no code change;
the set of languages is open. `tools/gate.sh` rejects new hard-coded text; exceptions are data with a stated reason.

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
- **Owner, 2026-10-13:** the parallel session S2 (`docs/reports/PLAN_S2_2026-10-13.md`) may run
  several agents at once (preset 4; the owner raises it in one word). S1 stays single-track.
- **Waste control (owner + GPT, 2026-10-21; measurements in `docs/reports/INDEX.md`, window
  telemetry in `docs/reports/LIMITS_LEDGER.md`).** Supersedes "one at a time" as a hard rule;
  the ×10 confirmation rule stays.
  1. *Durable checkpoints:* every meaningful increment goes into git (and to the remote for
     work branches) with a resume note; the session context is never the only record.
     Workflow scripts (= package briefs) are kept in `tools/workflows/`. An auto-push
     `post-commit` hook exists in `tools/` but is **not installed** until the owner says yes.
  2. *Small self-contained packages:* input, result, acceptance check, resume card. Cost grows
     with the **square** of an agent's length (every turn re-reads its whole context; measured:
     98 % of one workflow's cache reads sat in five 100–550-turn agents), so a package is
     about 50 tool calls, then a commit and a card; the next agent reads cards + `git log`.
  3. *Adaptive parallelism:* no fixed agent cap. Concurrency follows the measured pace of the
     five-hour window (utilization against the straight line to the reset) and the measured
     cost per agent. Preset until the ledger calibrates: up to 3 agents, reviewers one after
     another. Running dry early and leaving the window unused are both waste.
  4. *Limit telemetry:* the session's `rate_limit_event` (via `list_events`, kinds
     `["rate_limit_event"]`) carries `five_hour`/`seven_day` utilization and `resetsAt`; sample
     it into the ledger at every notification instead of estimating from token counts.
  5. *Cache TTL and model choice (measured 2026-10-21, `tools/usage_report.py`):* 24 % of agent spend was not work — 7 % the start
     of each agent (prompt prefix), 17 % whole contexts rewritten after a pause over 5 minutes (a tool call that blocks for a build,
     a test run or a CI wait). Rules: no single tool call longer than ~4 minutes (background + short polls); fixes after reviews,
     mechanical packages and log reading run on the cheaper models (Haiku/Sonnet), Fable only for design stages; a review of a bug fix
     is one reviewer, proportional to the fix. The weekly pace is `tools/pace_plan.json` (owner, 2026-10-21) — run `tools/pace.py`
     before starting an agent.
  6. *Corrections while an agent runs (measured 2026-10-22: three stops and restarts of one research + design
     workflow wasted ≈ 19 million cost equivalents, about 0.9 point, nothing of it recoverable):* a running
     workflow agent cannot be updated — its prompt is fixed at start and no channel reaches it
     (`ListAgents` shows none). So: (a) research and design are separate workflows, giving the coordinator a
     control point before the expensive stage; (b) the owner's rules live in `CLAUDE.md`, which every agent
     receives at start — briefs point to them instead of pasting them; (c) a correction is applied by a short
     addendum pass after the agent finishes, unless the agent is still in its first minutes and the
     correction changes its whole direction; (d) `resumeFromRunId` replays only the unchanged PREFIX of
     `agent()` calls — never edit the first call when later results should be kept; write stage results to
     files and pass them as `args` to the next workflow; (e) before any stop, compare the sunk cost with the
     cost of an addendum.

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
- `semantic_status` is a text column. Native per-message live/worker analysis now uses `pending` → durable `executing` → `done` or `failed`; explicit `pending` requeues failed/interrupted work. Legacy Python records visible failures as `failed` but does not implement the native atomic claim. Historical rows and separate batch consumers are not rewritten (see the 2026-10-09 B report).
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

**Shared machine, several agents:** never stop test servers by name (`pkill loom-server`, `kill $(pgrep -x loom-server)`,
`pkill loom_tests`) — that kills the servers of every other agent and gate on the machine (measured 21.10: it made a
neighbour's `first-steps-ui` run fail at random). Record the pid you started, or match your own port
(`pkill -f "loom-server --port N"`), and kill only that.

**A gate measures a tree nobody edits** (2026-10-23: gate `k2c1b3` went red on `unit.test_runtime_profile` only
because an agent was editing `chat_ops.pack` in the same worktree while ctest compared the embedded profile with
the file on disk; the agent's later commit made the tree consistent again and the red was void). One worktree, one
writer: no package agent starts in a worktree while a gate runs there, and no gate starts while an agent works
there — the gate's result is otherwise a measurement of a moving target, and the cheapest fix is a second worktree.
**One gate at a time on this machine** (2026-10-23 ≈ 04:55: two full gates in parallel in two worktrees — two native
builds plus two web e2e suites — ended in a container restart; both gates were lost, the second build dir had grown to
6.4 GB and the disk was at 93 %). Gates run one after another; a second worktree is for an agent writing while a gate
runs elsewhere, not for a second gate. Before a gate: at least ~5 GB free (the web build, screenshots and junit need it).
**Correction after a second restart (2026-10-23 ≈ 07:19, one agent building alone): the trigger is the DISK, not
parallelism.** A dev (debug, -O0 -g) build dir is 4.4–6.4 GB (CMakeFiles 3.0 GB, libloom_core.a 1.6 GB, binaries
1.9 GB); two of them plus ccache (5 GB) exceed the session's writable allowance and the container restarts at ~95 %,
killing every background task. Rule: ONE native build dir on the machine at a time (a second worktree builds only
after the first build dir is deleted; `ccache` makes the rebuild ~15 min); ≥ 5 GB free before any build or gate;
`du -sh /home/user/wt/*/loom/build /home/user/.ccache` is the first command after a restart. Open improvement:
`-gz` (compressed debug sections) or `-g1` on the dev preset would shrink a build dir several times.

**C++ pitfall that only AddressSanitizer catches** (CI found it after the asan preset had been broken for
days): never range-for over `.at()` / `.value()` / `operator[]` of a by-value temporary, e.g.
`for (auto& a : unwrap(f()).at("k"))` — only the last reference is lifetime-extended, the temporary dies
before the loop. Bind the object to a name first (`const Json x = unwrap(f()); for (... : x.at("k"))`). Seen again 2026-10-21 in a new test
(CI 260, `f.send(legacy)["messages"]`); `tools/lint_asan_range_for.py [base]` lists suspicious added lines and `tools/gate.sh` runs it first.

**Second ASan-only pitfall: a pointer or reference into a value that is later replaced** (CI 273, 2026-10-22: `const Json* f = field_of(l, slot)` pointed into `l.state`, then `write(e, l, …)` did `l.state = *r`, and the next `*f` read freed memory — the dev gate, built without ASan, passed). Never keep a pointer/reference into a container or state object across a call that can reassign or grow it; take a copy first (`std::optional<Json>` instead of `const Json*`) or re-look-up after the call. A local ASan build does not fit on the machine (disk), so only the `asan` job of CI proves this class — read such code with the question "what does this call assign to?".

**A CI run shown as "cancelled" with no newer push is a job timeout** (run 281, 2026-10-23: the `dev (g++)` job hit
`timeout-minutes: 45` inside "Build and test web workbench" while asan/vendored/libcxx were green; GitHub reports a timed-out
job as cancelled, and the run inherits it). Check `list_workflow_jobs` step timings before treating it as a flake; the dev
limit is now 100 min (a preset) because the web stage (60+ e2e scripts) grows with every M1 package.

**gcc accepts what clang rejects** (CI 262, 2026-10-22: the generated onboarding pack passed ~420 KB and clang's constexpr step limit stopped the vendored/Android build while every gcc job stayed green): generated `*.inc` data must be emitted with explicit lengths (`std::string_view(R"...", N)`), never as bare literals measured by a constexpr strlen. `tools/clang_inc_check.py` runs `clang++ -fsyntax-only` on every source that includes a generated `.inc`; `tools/gate.sh` runs it.

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
