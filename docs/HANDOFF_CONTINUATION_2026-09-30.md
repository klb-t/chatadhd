# Continuation handoff — 2026-09-30

## Start here

Read `STATE.md` for the current verified state and `coordination/README.md` for
six independently assignable work packages. The integration branch is
`gpt/research-2026-09-30`; fetch it before starting. Do not use the old PR6 branch
or a previous conversation's remembered HEAD as the current source.

This continuation recovered `fafc77f8eeebdff4c32897b5e8ef94dd26d4ec38` and merged
Claude's history through `46066308ac6306a1b30f65b98e19a40da6df7e9c`. The full
implementation candidate is `a79862837c6726f534471f8dbcdccfe749dfb942`, also saved
on `wip/continuation-2026-09-30`. Its WIP message records the verification status
at that time, not the result of later tests. The final verification receipt is
`research/continuation_2026-09-30/`.

## Work already implemented

- Native ContextEngine now supplies selected knowledge to real chat requests.
  Memory, legacy graph and history remain independently configurable. The web
  chat exposes the options and exact persisted compilation trace, including
  inspection after provider failure and after reload.
- Context scope (`relation_hops`) and detail (`detail_resolution`) are separate
  controls. This does not yet implement selection per thesis of a plan.
- Reindexing preserves node metadata, traces and provenance. Latest completed
  knowledge-run selection survives many newer unfinished runs. An unset stream
  option respects configuration.
- User confidence is preserved during calibration; authority in conflict
  resolution remains a separate property.
- Active source projection checks byte hashes, UTF-8 boundaries, JSON Pointer
  identity and ambiguous metadata. Frozen research instruments remain frozen.
- Durable local coordination provides task leases, fencing, recovery receipts,
  explicit unknown outcomes, coherent backup and exact saved-workflow replay.
  It is usable through `python3 -m loom.tools.coordination`.
- New GitHub sync defaults exclude root and nested `secrets.json`. Existing
  explicit profiles remain configurable; this is not a universal secret scanner.

Contracts and evidence links are in `STATE.md`. Do not reimplement these changes
because a work package mentions their longer-term successor.

## Next independent work

| Package | Next usable result |
|---|---|
| W1 | Versioned ActiveTaskSpec consumed by the real request path, preserving corrections and source references |
| W2 | Selection per plan/thesis, omission diagnostics and actual semantic candidate channels |
| W3 | Recipe/model improvements on identified DEV failures, with first responses and non-resetting cost accounting |
| W4 | Additional real consumers of graph operations, with recovery and provenance |
| W5 | Durable independent/coupled workspace views over existing native data |
| W6 | Independent counterexamples, real authorized export fidelity and end-to-end validation |

One integrator owns shared interfaces, `STATE.md` and the integration branch.
Separate conversations use their own branch/worktree and package-specific
receipts with exact base/head SHAs. Local SQLite coordination requires a shared
database; it does not coordinate isolated conversation filesystems by itself.
The package README contains a ready-to-copy starting prompt.

## Preserve these distinctions

The repository is public according to GitHub metadata checked in this session;
older claims that it is private are stale. No visibility change was made. No
private owner archive, credential, paid model call or sealed holdout was needed
for this implementation. More ChatGPT tokens do not reset the existing USD 2
model-call programme. The owner can configure policies; defaults are presets.

A stored compilation trace records the messages the runtime constructed. The
local-provider end-to-end test verifies what that test provider received; neither
is a claim that every remote provider necessarily received every stored trace.
Synthetic mechanism tests do not establish model quality or real-export fidelity.
The historical 851 structure-test result included 30 tests whose unpublished
source was lost; this work implements new coordination code instead of claiming
to recover that source. Keep earlier failed runs alongside corrected results.

Known follow-up limits include query caps/error visibility, whole-request token
accounting, historical overlapping memory-peak accounting, real archive scale,
Android device checks and semantic selection quality. See `STATE.md` rather than
silently promoting a prototype or a historical metric into a current guarantee.
