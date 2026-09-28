# ChatADHD / Loom continuation

Current graph-native round: `docs/research/GRAPH_NATIVE_PROGRAMME_2026-09-28.md`.
Measured checkpoints: `docs/research/GRAPH_NATIVE_RESULTS_2026-09-28.md`.
The owner also recalled the inexpensive separately configured semantic model.
Read `docs/research/SEMANTIC_MODEL_FLOW_2026-09-28.md` for the actual native
integration, request budgets, candidate boundary and remaining semantic gaps.
The owner clarified that argument/thought structure belongs in the same knowledge
graph; compare derived subgraph projections without another authoritative store.

The resumed research round starts at `docs/research/PROGRAMME_2026-09-28.md`:
composable thought structures, independent experiments, topic segmentation and
existing-graph context. Its measured status is separate from the native baseline.
Read `docs/research/RESULTS_2026-09-28.md` for measured outcomes, limitations and
the next research directions; the fresh scoped-source comparison is separate
from supplied-gold graph scores and broad natural-language coverage.
Read `docs/CODEX_HANDOFF_2026-09-28.md` for the preceding work, tests and
remaining limits. Claude's original handoff is `docs/HANDOFF_2026-09-28.md`.
The complete Loom development line is `codex/loom-handoff-2026-09-28`; verify
the current remote refs before starting from the older Python-only `main`.

Read `CLAUDE.md` for build/compatibility conventions and the architecture
documents in the handoff's order. Owner requirements and the single conceptual
model govern terminology. Preserve all source bytes, distinguish inference
from observation, keep policy in data, and retain the owner's options.

The owner explicitly requested frequent commits pushed to GitHub and a current
handoff because conversation sessions can become inaccessible. Save meaningful
increments; keep untested checkpoints clearly labelled and record exact test
results. Do not rely on the conversation as the only record of progress.

Do not read or tune against `eval/real-holdout-key` during development. Current
source-date filtering is not a clean historical benchmark when the data pack
contains later knowledge. Keep integrated metrics separate from per-module
metrics and retrospective reconstruction separate from prediction.

Core Python/C++ on-disk compatibility remains mandatory; schema version 4 is
unchanged. New state belongs in `loom_*` tables. Every public ABI addition must
pass the exported-symbol compatibility test and its actual transport paths.
