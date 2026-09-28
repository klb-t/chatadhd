# Codex continuation — 2026-09-28

Working branch: `codex/loom-handoff-2026-09-28`, based on Claude's full Loom branch at `3219f88e7e90ab78eabeb80ef77bf7a7b55fecbc` (not the older Python-only main).

Read `docs/HANDOFF_2026-09-28.md`, `CLAUDE.md`, and the owner requirements/conceptual model before continuing. The owner explicitly requested frequent GitHub checkpoints because conversations may become inaccessible.

## Implemented continuation

- Knowledge CLI propagates failures, caches include source bytes, cached products rematerialize into new/missing output directories, pipeline version is bumped.
- Attribution uses leave-one-out evidence and consistent candidate gates; its regression is now mandatory (8 cases / 494 assertions passed). Identity aliases respect Unicode word boundaries; explicit stems retain prefix matching.
- Authenticated HTTP knowledge/catalog/context routes and `loom_catalog_select` C ABI are connected to the coordinated web workbench. Chat remains mounted. HTTP and JNI transports share named-object arguments.
- Android bridge handles terminal callbacks on early errors and keeps retired subscription userdata alive until runtime shutdown drains callbacks. Expanded host JNI smoke passed. Device/Kotlin build is not verified.
- Browser build, transport contract and 16 Chromium e2e cases passed, including phone-width layout.
- Evaluation runner exists, confines temporary workspaces, verifies temporal ancestry, and marks the currently contaminated temporal holdout unavailable. It never presents retrospective consistency as predictive accuracy. 15 Python harness tests passed.
- CI builds the server and runs JNI/web validation. Clang unused-variable failures discovered in archive/importer/provider tests are removed.

## Current integration verification

- A deeper audit found full imports extracted only selected catalog units, while empty selections/read failures silently fell back to raw sources. Extraction now consumes the exact imported IDs and propagates catalog errors; new regressions are being verified.
- Full/copy previously retained no original bytes. It now stores complete source containers and each imported unit, with verified BlobStore hashes and reads after original-source removal. Selective/copy retains chosen units only; link remains dependent on the original source. Tests are being added for deletion, ZIP binary members, scope and repeat import.
- Singleton/wrapped JSON locators now match their hashes; legacy locators are recovered only when their expected hash verifies.
- Raw retention is authoritative. Normalized message tables still do not expose all original model/date/branch/attachment metadata. Upgrading an existing link retains raw bytes without replacing its existing conversation placeholder.
- Native rebuild/final full ctest and fresh integrated/selfhost evaluation are in progress. An interrupted local run hit workspace artifact syncing (zeroed ELF header / executable permissions); it is not counted as passing.

## Resume and remaining work

Draft PR: https://github.com/klb-t/chatadhd/pull/6. Keep checkpointing this branch after meaningful work. Read `AGENTS.md` and the original handoff first. The original Claude handoff remains historical.

Next: finish raw-retention regression verification, run the full native suite/CI, rerun integrated synthetic and repository selfhost reports on the corrected scope, and publish exact results. Extraction quality is experimental; earlier isolated module scores do not establish end-to-end quality. Real user export and Android device validation are still outstanding. UI layout persistence/free docking and judgement editing remain future work.

The real temporal-holdout answer-key branch remains unread during development. Do not tune against it. The local checkout was reconstructed through the GitHub API and has synthetic local ancestry; use GitHub Git-data APIs with the current remote parent/tree, not a force push of local history.
