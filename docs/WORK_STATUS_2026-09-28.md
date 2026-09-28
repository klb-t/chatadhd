# Current work checkpoint

Native base checkpoint: `b818366ffabdd6b1cbe14aab6e062805115a780b` on
`codex/loom-handoff-2026-09-28`, draft PR #6.
Recovery/audit checkpoint: `52d50d151d93d631245ce7a7fa1372f3f5f9726f`.
The commit containing this updated file adds the verified offline experiments.

The user supplied two Gemini research reports after another reported interface
stall. Both reports were read; text snapshots and original DOCX hashes are under
`docs/research/inputs/`. Primary-source audits are
`GEMINI_LOGIC_AUDIT_2026-09-28.md` and `GEMINI_JEV_AUDIT_2026-09-28.md`
under `docs/research/`. Incident evidence and recovery are in
`WORK_RECOVERY_2026-09-28.md`. Exact platform root cause remains unknown.

## This checkpoint

- Completed: recover 80 selected repository files, verify their remote Git blob
  hashes, audit the two reports, retain sources and correct exaggerated claims.
- Completed: bounded propositional consequence checking over existing candidate
  occurrence graphs, with consistency checked before entailment. 13 author tests.
- Completed: provider-neutral replay of independent context relevance scores,
  with multiple simultaneous selections, abstention and permutation diagnostics.
  21 author tests, including CLI behavior.
- Completed: independently authored protocol and cases for both mechanisms.
  First run 13/13 test methods passed; its complete report remains unchanged.
  A subsequent source-review fix rejects a whole preparation wrapper before
  traversing sidecars; the same independent suite passes on the final version.
- Full restored research suite: **260/260 tests**, 1.221 seconds. Inputs are
  supplied graphs and authored scores, not real model responses.
- No real Jev/model calls, credentials or provider switching; no native code,
  schema, UI or canonical graph mutation in this increment.

The prior native build result remains 71/72 gates (catalog recall 13/45 below
0.55). It has not been rerun in the restored partial workspace. New contracts,
results and limitations: `research/GEMINI_RESEARCH_INTEGRATION_2026-09-28.md`
and `research/GEMINI_EXPERIMENTS_INDEPENDENT_2026-09-28.md`.

## Next direction

The bounded increment is finished. No child implementation is left pending.
The larger research programme remains open: independently labelled source-to-
structure extraction, Polish/English relevance scoring under equal budgets,
late-topic/return selection, and raw context versus graph-cache/source-refresh
comparison. Live provider evaluation and a Jev HTTP adapter are not implemented
or claimed by the saved-score replay. Preserve the existing user-configured
semantic model and explicit provider opt-in when continuing.
