# Codex continuation — 2026-09-28

Working branch: `codex/loom-handoff-2026-09-28`, based on Claude's full Loom branch at `3219f88e7e90ab78eabeb80ef77bf7a7b55fecbc` (not the older Python-only main).

Read `docs/HANDOFF_2026-09-28.md`, `CLAUDE.md`, and the owner requirements/conceptual model before continuing. The owner explicitly requested frequent GitHub checkpoints because conversations may become inaccessible.

## Implemented continuation

- Knowledge CLI propagates failures, caches include source bytes, cached products rematerialize into new/missing output directories, pipeline version is bumped.
- Attribution uses leave-one-out evidence and consistent candidate gates; its regression is now mandatory (8 cases / 494 assertions passed). Identity aliases respect Unicode word boundaries; explicit stems retain prefix matching.
- Authenticated HTTP knowledge/catalog/context routes and `loom_catalog_select` C ABI are connected to the coordinated web workbench. Chat remains mounted. HTTP and JNI transports share named-object arguments.
- Android bridge handles terminal callbacks on early errors and keeps retired subscription userdata alive until runtime shutdown drains callbacks. Expanded host JNI smoke passed. Device/Kotlin build is not verified.
- Browser build, transport contract and 16 Chromium e2e cases passed, including phone-width layout.
- Evaluation runner exists, confines temporary workspaces, verifies temporal ancestry, and marks the currently contaminated temporal holdout unavailable. It never presents retrospective consistency as predictive accuracy. 16 Python harness tests passed. Precision uses labeled conversations; unlabeled auxiliary documents are counted separately.
- CI builds the server and runs JNI/web validation. Clang unused-variable failures discovered in archive/importer/provider tests are removed.

## Current integration verification

- A deeper audit found full imports extracted only selected catalog units, while empty selections/read failures silently fell back to raw sources. Extraction now consumes the exact imported IDs and propagates catalog errors; full scope, empty selection and error regressions pass.
- Full/copy previously retained no original bytes. It now stores complete source containers and each imported unit, with verified BlobStore hashes and reads after original-source removal. Selective/copy retains chosen units only; link remains dependent on the original source unless a copy was already retained. Five retention regressions pass, covering deletion/reopen, ZIP binary members, scope and copy/link replay.
- Singleton/wrapped JSON locators now match their hashes; legacy locators are recovered only when their expected hash verifies.
- Raw retention is authoritative. Normalized message tables still do not expose all original model/date/branch/attachment metadata. Upgrading an existing link retains raw bytes without replacing its existing conversation placeholder.
- Scanner version 3 / pipeline version 4 invalidate outputs from earlier scope/locator behavior. Standalone JSON with more than one streaming chunk of leading whitespace is covered.
- Final local native build succeeds. Full CTest: **59/60 pass**, with the sole failure **unit.test_catalog_eval**: conversation recall **13/45 = 0.2889**, below the unchanged **0.55** gate. Labeled-conversation precision **13/13 = 1.0**; trap selections **0/5**, generic noise **0/15**. Do not mark this branch fully green or weaken the gate to conceal the gap.
- The stricter identity matcher removed incidental `EP`/`LEM` hits inside unrelated words. Fixing BM25 query/sketch normalization and philosophy evidence in link propagation passes independent feature regressions but does not solve the benchmark recall gap.
- HTTP/API/ABI/compatibility/import tests are included in the passing native tests. Expanded host JNI smoke passes; Chromium e2e **16/16** passes, including phone layout. The latest code checkpoint is still a draft PR.
- Some earlier local verification artifacts suffered synchronization problems (zeroed ELF header/executable mode and stale SQLite WAL beside a newer DB). Fresh isolated `/tmp` runs are used for measurements; do not treat a live copied DB/WAL pair as a valid snapshot. An interrupted run is not counted as passing.

## Resume and remaining work

Draft PR: https://github.com/klb-t/chatadhd/pull/6. Keep checkpointing this branch after meaningful work. Read `AGENTS.md` and the original handoff first. The original Claude handoff remains historical.

Next: improve genuine selective retrieval without restoring substring false positives, validate against real owner exports, and investigate generalization cost on repository-scale input. Integrated synthetic and repository selfhost reports record their exact source/binary scope separately; isolated module scores do not establish end-to-end quality. Android device validation is still outstanding. UI layout persistence/free docking and judgement editing remain future work.

The real temporal-holdout answer-key branch remains unread during development. Do not tune against it. The local checkout was reconstructed through the GitHub API and has synthetic local ancestry; use GitHub Git-data APIs with the current remote parent/tree, not a force push of local history.
