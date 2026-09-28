# Codex continuation — 2026-09-28

Working branch: `codex/loom-handoff-2026-09-28`, based on Claude's full Loom branch at `3219f88e7e90ab78eabeb80ef77bf7a7b55fecbc` (not the older Python-only main).

Read `docs/HANDOFF_2026-09-28.md`, `CLAUDE.md`, and the owner requirements/conceptual model before continuing. The owner explicitly requested frequent GitHub checkpoints because conversations may become inaccessible.

## Active work

- Complete knowledge CLI/pipeline and evaluation runner; propagate failures and invalidate caches when source bytes change.
- Fix project-attribution noise and replace the permitted-failure test with an enforced regression gate.
- Expose the existing knowledge/catalog/context C ABI in the authenticated HTTP API.
- Add coordinated knowledge views to the web workbench, preserving the chat and existing graph.
- Repair the observed Clang CI build failure and verify core, compatibility, API and UI paths.

## Findings verified so far

- Latest inspected Loom CI (`36367858560`): GCC dev and ASan succeeded; Clang vendored failed on unused `kLog` in `src/archive/ingest.cpp`.
- The archived knowledge CLI can return success after an internal knowledge failure.
- Evaluation command imports a missing `kbeval.realrun` module.
- Knowledge cache identity currently omits source-content changes.
- Attribution can reinforce earlier assignments and admits weak runner-up matches.

Implementation and testing are in progress. No green build or complete feature claim is made by this checkpoint. The real temporal-holdout answer-key branch remains unread during development.
