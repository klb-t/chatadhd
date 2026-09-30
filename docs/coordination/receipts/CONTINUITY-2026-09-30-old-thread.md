# Old-thread continuity audit — 2026-09-30

Scope: reconcile the September 28 Loom handoff conversation with newer work. This is an additive receipt, not an integration STATE update. No model calls, credential reads, tests, Actions, merges or shared-branch mutations were performed by this audit.

## Evidence and preservation

Compared complete recursive GitHub trees:
- old codex/loom-handoff-2026-09-28: 816406722257d70431cc2acadaac313f0503b48c
- integration gpt/research-2026-09-30: b118c80e981c08ec6d7f9ab6aacc177979186cf2

All 847 old blob paths exist in the integration snapshot: 790 retain identical Git blob SHAs; 57 changed. This proves path preservation and the stated byte identities, not correctness of every changed file. The old local checkout is no longer available in this runtime. Conversation-history retrieval was used as secondary evidence; repository artifacts take precedence.

Two apparent gaps were resolved by checking renamed/newly published artifacts:
- Original GPT-4.1-mini 48-pair comparator: docs/research/model_method_panel_v1/FIRST_PAIRS_RESULTS.md and gpt41mini_same_pairs/ preserve all 48 completed responses, manifest, ledger and score. Reported 32 TP /16 TN /0 FP /0 FN, USD 0.0111276, no paid retries. These authored exploratory pairs are not a blind holdout. The old thread's last observed 42 completions is superseded, not a reason to rerun.
- Jev context scorer: loom/tools/structure/jev_context_score.py and test are present. docs/research/model_method_panel_v1/jev_context/FIRST_RESULTS.md pins the same SHA-256 recorded in the old thread: 41975d03937a9f15027eadab3f3ecb32a091a1cb4bdf3b52741963466c41c714. New 48-request context results and raw evidence are present. This is conditional supplied-topic/Claim selection, not free graph extraction.

Current docs/STATE.md supersedes historical baselines: native 77/77 and catalog DEV recall 31/45 replace old 71/72 and 13/45 as current reported status. This audit did not rerun tests.

## Remaining unverified tail: semantic_sketch compiler

No semantic_sketch.py, test_semantic_sketch.py or SEMANTIC_SKETCH_PROTOCOL_2026-09-28.md appears in the integration tree. Library search did not recover these artifacts. This is a bounded absence finding, not proof that no private archive or other branch contains them. Final source bytes and final hash are unavailable; do not count this prototype as integrated or currently tested.

The following is a recovery specification from the old conversation, NOT recovered source:
- compile_sketch(source_packet, original JSON string/bytes), schema loom.semantic_sketch/1.
- Separate alternative readings, each with IDs, nodes, roots, coverage and unknowns.
- Typed scope, expression occurrence, term occurrence and binder nodes; preserve polarity, context, premises, scope ancestry, binding and operation-role distinctions.
- Bind exact observation quotes to UTF-8 byte offsets only when occurrence is unique, counting overlapping occurrences. No normalization or guessed first match.
- Deterministic local handles; compile through existing grounded_frames and candidate_graph machinery, with in_scope, scope_parent, operation_type, operands, bound_to and denotes relations.
- Existing five operations only in that prototype; unsupported semantics become located unknowns. This was bounded implementation coverage, not a permanent product restriction.
- No keyword-driven semantic inference, new parallel store, graph writes or automatic promotion.
- Review exposed loss of parentless-scope premises/polarity. The reported correction handled premise eligibility for every node independently of claims and recorded per-reading plus aggregate losses for root qualifications retained only in raw input.
- Historical local report: 20 prototype tests passed after the final correction and import-path adjustment. Test sources are unavailable; this is NOT a current reproducible gate. A 48-mutation field audit reportedly changed graph output for 46 cases and exposed two root-qualification losses. No final stable source hash is claimed.
- A proposed live wrapper and 16-packet pilot were not confirmed executed. Do not infer paid results or authorization from that plan.

This prototype differs from W1's active-instruction compiler. It is also separate from the later unpublished shared-lease incident and its 30 missing test sources documented in RECOVERY_COORDINATION_2026-09-30.md.

## Owner intent to preserve across lanes

These are directions and evaluation needs, not a claim that every feature is implemented:
- Meaning and argument structure live in the graph. Discover recurring typed subgraphs across topics; avoid duplicating a competing semantic store.
- Atomic operations include syllogistic/inference patterns, generalization, specification and branching/conditions. Preserve directed roles, scope, attribution, polarity and alternatives. Synonym choice should have little influence on the extracted structure.
- Words/regex are useful secondary diagnostics for omissions, not the primary semantics. Measure paraphrase invariance, cross-domain transfer and shared-vocabulary structural counterexamples separately.
- Universality and specificity are distinct axes, not forced inverses.
- Detect late topic onset, returns after digressions and multiple concurrent topics; interpret new material against existing graph evidence, preserving corrections and provenance when expanding or simplifying.
- Keep the independently configurable cheap semantic/structural model path. Evaluate Jev, structured LLM extraction and vector/lexical retrieval on matched tasks rather than declare a global winner.
- Complete imports preserve provider export structure, attachments and source links; provider-inspired views and multiple synchronized graph views remain configurable, without an arbitrary exclusive-view choice or five-view ceiling.
- Preserve maximum useful user choice with explicit loss/augmentation and observation/inference separation. Current owner decisions supersede older compatibility requirements.
- Research budget history must not reset through handoffs. This audit adds no paid authorization.

## Routing without duplicate work

Use docs/STATE.md and docs/coordination/README.md as the current coordination entry points. W1=request interpretation, W2=retrieval, W3=model recipes, W4=graph/runtime, W5=workspace views, W6=independent evidence/import. Only the integration owner edits STATE.

Suggested W3/W6 follow-up: first compare current grounded extraction machinery against the recovery specification above. Recover original bytes if an archive exists; otherwise implement only uncovered behavior under a clearly new version with reproducible tests. Do not blindly rebuild a second compiler or replay completed GPT/Jev calls. W2 should retain structure-sensitive evaluation separately from retrieval ranking.

This receipt preserves the missing design/status knowledge. It does not assert that the unavailable prototype has been restored.
