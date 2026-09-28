# Actions quota: preserve work without new runner usage

On 2026-09-28 the owner reported 2,000 of 2,000 included Actions minutes used,
with renewal on 2026-10-01. This is distinct from the dedicated OpenRouter
credit budget. No account budget or billing setting was changed.

## Immediate response

- Do not launch or rerun Actions while this quota stop applies. Every repository
  checkpoint must include `[skip ci]` in its commit message. This skips push and
  pull_request workflows; it is not a command to cancel an already running job.
- Both current model request files have `enabled: false`. Completed request IDs
  and first-response evidence remain preserved; do not reactivate old IDs.
- Run suitable deterministic tests locally. Keep failed and unrun gates visible.
- The 48-pair Jev comparison corpus is frozen but **unrun**. Its separate request
  `openrouter-jev-pairs-request.json` is disabled. No new credential is required
  for Jev and no BYOK setup work is pending.
- Retain artifacts in the repository so expiry of Actions artifacts does not
  erase the measured work. Git commits and read-only artifact retrieval do not
  require starting a workflow.

GitHub documents `[skip ci]` for push/pull_request events. Required skipped checks
can remain pending; this checkpoint is not a claim that CI passed or a merge-ready
change. The token does not suppress workflow_dispatch or pull_request_target.

https://docs.github.com/en/actions/how-tos/manage-workflow-runs/skip-workflow-runs

## Why previous checkpoints started more builds than intended

The Loom workflow listened to both branch pushes and pull-request updates.
For the same checkpoint `8dc01c717ef20b1c166a30ef1560b4e06b26ce56`, GitHub
created a push run 36449503968 and PR run 36449511733. Each workflow defines
three native configurations. This demonstrates duplicate scheduled work; it does
not establish that this repository accounts for all 2,000 account-wide minutes.

The minimal correction restricts automatic Loom pushes to main while retaining
PR coverage and manual dispatch. No native test or matrix configuration is removed.
Research tools and evaluation fixtures remain covered: CMake registers their
unittests as research.structure, so excluding those directories would drop a gate.
Python sanity is scoped to the paths it compiles and cancels stale runs.

PR path filters use the cumulative PR diff. A documentation-only checkpoint in an
existing code PR may still trigger builds; path filtering is therefore not the
quota stop. The explicit per-commit skip instruction is required for current work.

## Current evidence

- Jev: `JEV_RESULTS_2026-09-28.md`, raw archive and proposed `JEV_USAGE_RULES_2026-09-28.md`.
- Native extraction: `OPENROUTER_NATIVE_DEV_2026-09-28.md` and
  `OPENROUTER_NATIVE_REMAINDER_2026-09-28.md`, retaining both ledgers and archives.
- Next direct pair test: `loom/tests/fixtures/eval/jev_structure_pairs_v1/`.
  It reuses inspected material and must be reported as exploratory, not a new
  holdout. Its disabled request is not connected to an automatically enabled run.

The GitHub connector exposes workflow reads and reruns but no cancellation
operation. At inspection, native build run 36449503968 was still in progress;
the two inference workflows had already completed. Do not mistake a new skip
commit for cancellation of that older build. Check its final status separately.

## Local verification of this checkpoint

Both edited workflows parse as YAML. A focused offline check exercised 44
path/event examples. The three native matrix entries and all build/test steps
are unchanged. All 48 new pair request bodies pass the actual Jev runner body
validator without a transport call, and all three model activation files are
disabled. The last full research-suite result remains 350/350; no new full C++
build or Actions pass is claimed.
