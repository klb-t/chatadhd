# ChatADHD — historical version snapshots

Source snapshots recovered from the owner's archives (uploaded 2026-09-26).
They are kept verbatim (only `__pycache__` removed) as immutable raw sources
for Loom's self-discovery / Archive Intelligence and for porting work.
They are **not** built or run by CI.

| Snapshot | Declared version | File dates | Lineage |
|---|---|---|---|
| `chatadhd_v0.8.3/` | `__version__ = "0.8.0"` in `main.py` (archive named v0.8.3) | 2026-03-17 … 2026-03-21 | forked from **0.7.9** (commit `46d0ba7`; its `engine/models.py` is byte-identical there and it lacks the 0.7.10 model-selector work): adds `engine/attachments.py`, request-spec editor in `chat_engine`, importer/MHT/URL-import and GUI changes |
| `chatadhd_v0.9.0/` | 0.9.0 ("drugie podejście") | 2026-04-17 … 2026-04-18 | forked from **0.7.10** (`1e3fa2b`, before the 2026-03-06 abspath fix `ddcab9e`), independently of 0.8.x: video pipeline (scene graph, film structure, prompt compiler, compositor, async jobs, OpenRouter video), panel/dock system, live debug, per-run log files. Does **not** contain the 0.8.x changes. |

Lineage was verified per file against git revisions (nearest revision by
content diff, then a vote) — it contradicts the version names, which is itself
a useful self-discovery test case. Both lines share the unchanged core
(`engine/db.py`, `core/semantic.py`, `engine/graph_engine.py`, ...). The owner
decided not to merge them ("rebuild, don't recover"); see
`docs/history/ANALIZA_v0.8.3_v0.9.0.md` for what is reused as ideas and data.

Related: `docs/history/CHATADHD_PELNY_RAPORT_HISTORYCZNY.md` — the owner's
report of development sessions 2026-01-21 → 2026-02-08 (v0.2 → v0.06.03),
including a "potentially lost features" list.
