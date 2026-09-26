# ChatADHD — historical version snapshots

Source snapshots recovered from the owner's archives (uploaded 2026-09-26).
They are kept verbatim (only `__pycache__` removed) as immutable raw sources
for Loom's self-discovery / Archive Intelligence and for porting work.
They are **not** built or run by CI.

| Snapshot | Declared version | File dates | Lineage |
|---|---|---|---|
| `chatadhd_v0.8.3/` | `__version__ = "0.8.0"` in `main.py` (archive named v0.8.3) | 2026-03-17 … 2026-03-21 | forked from **0.7.10** (`main`): adds `engine/attachments.py`, request-spec editor in `chat_engine`, importer/models/GUI changes |
| `chatadhd_v0.9.0/` | 0.9.0 ("drugie podejście") | 2026-04-17 … 2026-04-18 | forked from **0.7.10** independently of 0.8.x: video pipeline (scene graph, film structure, compositor, async jobs, OpenRouter video), panel/dock system, live debug. Does **not** contain the 0.8.x changes. |

Both lines share the unchanged 0.7.10 core (`engine/db.py`, `core/semantic.py`,
`engine/graph_engine.py`, ...). Merging them is a real fork-resolution task.

Related: `docs/history/CHATADHD_PELNY_RAPORT_HISTORYCZNY.md` — the owner's
report of development sessions 2026-01-21 → 2026-02-08 (v0.2 → v0.06.03),
including a "potentially lost features" list.
