# Acceptance tests to implement — from the 2026-09-29 note

Source: `NOTATKA_GPT_2026-09-29_KOMPILATOR_INTERAKCJI.md` §3, §18, §24 (criteria
proposed by GPT for Claude to assign to existing requirements and tests
**without lowering any quality gate**). Status column is the state of the repo on
2026-09-29 — none of these is an implemented test yet unless stated.

Legend: **U** = unit (doctest), **E** = end-to-end (Playwright/CLI), **X** = eval
harness (`loom/tools/eval`), **—** = not implemented.

| # | Criterion (short) | Req. | Test kind / area | Repo status |
|---|---|---|---|---|
| 0 | Batch and chronological replay of the same ordered event stream give consistent results under a fixed contract (time-knowledge boundary, config, interpreter versions, stored model answers); re-processing creates no new "independent" evidence | R14, R26 | X — extract/resolve + eval | — (same-pipeline test exists for archived vs live claims; replay parity and dedup of re-processed evidence not tested) |
| 1 | History ≠ request: the same history yields different payloads under different policies; UI shows both; a synthetic consolidation carries sources and never poses as a quote | R26, R31 | U (request compile) + E | — (`ContextEngine` is not on the chat request path) |
| 2 | Content by plan: one document uses different scope/detail per plan point; omission of an important source/premise is detectable | R28 | U + X | — |
| 3 | User control: scope can be widened without raising detail of everything, and vice versa; sampling density is yet another control | R28, R32 | U + E | — (`ContextEngine` has budget and resolutions, no scope/detail split) |
| 4 | Jev: candidates independently judged useful can all be positive; rubric, version and result stored; relevance is never turned into truth | R38, R27 | U (ScriptedTransport) | — |
| 5 | UI composition: several graphs + a table + a card container with different couplings and a detached reference panel | R29 | E | — (current workbench: coordinated panels, not free composition) |
| 6 | Detachable profiles: changing UI profile changes neither selector, model nor permissions; a preset can be decomposed and modified; an unavailable tool reports missing capability | R30, R31 | U + E | — |
| 7 | Perspective experiment: different samples of a region give separate, auditable configurations; none becomes the user's view; no hand-made concept axes required | R32 | X | — |
| 8 | Consolidation: a refinement sequence yields an active specification keeping the important exceptions without carrying wrong versions; history reproducible | R34 | U + X | — |
| 9 | Triggers/budget: an explicitly enabled rule may start a bounded experiment; it cannot start a spend loop or repeat disallowed tool effects | R33 | U (ScriptedTransport) | — |
| 10 | Evaluation/promotion: a configuration change has criteria, data, version and rollback; a better judge score never updates canonical facts | R33, R37 | U | — |
| 11 | Lexical shadow: a verified lexical-only hit becomes a regression case; lexical noise forces no change of semantics | R25, R27 | X — catalog eval | — (in progress: catalog recall round 2) |
| 12 | Entity semantics: alias/ASR may merge identity; lineage, convergence, composition and shared-module stay relations | R35 | U — resolve | partial (`same_as` vs lineage exists; convergence/composition/sharing not modelled) |
| 13 | Multi-projection: an old project shown historically and simultaneously as a component of the current architecture, provenance intact | R35, R29 | E | — |
| 14 | Extrapolated seed: a structural candidate is a marked hypothesis with operator and expected properties; never an observation before validation | R36 | U — generalize | partial (inferred/extrapolated with Expected Property exist; extrapolations never become premises; structural-transfer hypotheses partly via morphisms) |
| 15 | Predict-before-observe: later confirmation keeps the prediction moment → fair measurement of the generator | R36 | X | partial (predictions per cut exist; `predicted_at` genealogy after confirmation not kept) |
| 16 | Model-of-models: weakness attached to a specific operation with measurement data; does not degrade the model globally | R37 | U | — |
| 17 | Compensation ablation: run with/without the failure-aware instruction on a frozen task; check the compensation does not worsen other cases | R37 | X | — |

Measurement rules from the note (§16) to apply in every experiment here: freeze
data and assumptions; make variant differences explicit; identical objective
criterion; independent check after tuning; repeated runs of one case do not add
independent cases; pooled accuracy can hide majority-class advantage; a result
after changing a threshold on the same data is exploratory; human feedback, model
judge and automatic tests measure different things.

## Verified code findings behind these criteria (2026-09-29)

Reported in the note's initial repo review (§17) and re-checked in code today:

1. **Confirmed** — the `ContextEngine` is reachable only through
   `loom_context_build` (C ABI); `ChatEngine` never uses it, so a context preview
   is not what a conversation sends. (→ #1, #3)
2. **Confirmed** — dependency closure (`src/context/context_engine.cpp`, the
   "premises of accepted items are pulled in" loop) drops a premise silently when
   the budget is exhausted: `try_accept` fails and nothing marks the conclusion
   incomplete. Needs a guarantee (select dependency bundles) or an explicit
   `incomplete` marker. (→ #2, #8)
3. **Confirmed** — `resolve/assess.cpp` sets `confidence = 1.0` for every
   `User`-class claim: it conflates fidelity of reading the utterance,
   credibility of its content and the owner's right to decide. Separate the
   three. (→ #16)
   **Partial correction, 2026-09-30:** calibration now preserves the producer's
   confidence for `User` claims, including 0, 0.35 and 1. Owner priority in
   conflict resolution remains independent and unchanged. This removes the
   calibration override without adding an ABI or schema field; it does not
   establish empirical calibration or complete the separation of those three
   dimensions. Explicit `KnowledgeStore` confirmation/edit replay still sets
   confidence to 1 under its existing contract and needs a separate migration.
4. **Confirmed** — catalog link building compares all unit pairs
   (`src/catalog/score.cpp`, nested `for i<j` with MinHash Jaccard and per-pair
   set construction): O(N²), ≈ 2.5·10⁹ pairs at 50 000 units. Needs candidate
   generation (LSH banding, inverted index on rare terms, time windows). (→ R1 scale)
5. **Reported, not re-verified** — default calibration carries hand-set priors,
   so a high score is not empirical confidence; de-duplication of copies and
   shared provenance of evidence remains important.
6. **Known** — results on supplied structures do not show text → correct
   structure on real archives (see `docs/STATE.md` §5).
