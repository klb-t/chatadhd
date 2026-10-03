# Composition transport and regression audit, 2026-09-30

The composition wrapper is frozen at SHA256
`dcc5202248546c468c058bc63bd590f5852cb6ea811d2360430da919ef57ff64`
(`freeze_before_validation.json`). Its frozen dependency inventory includes the
original panel/compiler, billing replay, validation repair2, public provider
snapshots, turn-reference binder, direct lookup, individual-source projection,
formal operator, their policy data, this protocol and its 20 synthetic tests.
No validation input/gold or older blind holdout was read; no key was accessed;
the wrapper makes no model/network calls and writes no canonical graph.

The independent release repair2 checks pass 20/20. Reviewed release admission
preserves the known spending lower bound and unknown reservation, fixed public
Jev aliases/snapshot identity, source-prefix shape, deterministic whole batches
and hard billing rejection. A root-authorized ticket must precede sealed loads.
The shared USD2 cap remains nonreset; this wrapper adds no spending permission.
The legacy Jev parser rejects explicit BYOK `true`, but an absent field is not
proof of explicit `false`; no frozen parser was altered here.

Composition checks pass **20/20**. The original compiler, citation binder,
individual-source projection, direct lookup and formal operator are reused
unchanged. Raw candidate events remain in the audit even when the view withholds
cross-source supersession. Semantic interpretation, source truth and model-claim
availability are not established by source binding. The first mistaken toy
counterexample and its correction are recorded in `FIRST_MECHANICAL_FAILURE.md`.

The original 24 preserved DEV response contents were replayed with the frozen
wrapper; all first predictions/graph variants were written before DEV gold was
supplied to evaluation. The entire judgment score objects equal their original
saved baseline objects, not only their accuracies:

| Frozen graph variant | Available / planned | Correct / planned | Exact baseline score equal |
|---|---:|---:|---|
| Original compiler + direct lookup | 96/96 | 88/96 | Yes |
| Citation binding + compiler + direct lookup | 96/96 | 92/96 | Yes |
| Individual-source view over citation graph | 96/96 | 95/96 | Yes |

This is a replay/parity check on reused development data, not a new independent
quality evaluation. DEV has **0 formal queries**, so no model-based formal-path
quality was measured. The synthetic formal tests establish separate scope,
conditional inference, all alternative paths, strict premise correspondence,
duplicate-path FP, missing-path FN and bounded/unavailable denominators.

Fresh broad regression uses `TMPDIR=/var/tmp` because `/tmp` has a Git ancestor,
and the existing declared contract dependency directory in `PYTHONPATH`:

| Suite | Passed / run | Recorded process time |
|---|---:|---:|
| Structure discovery | 685/685 | 17.22 s |
| Contracts discovery | 153/153 | 6.01 s |
| Eval discovery, including current archive estimator | 206/206 | 11.90 s |
| Nested composition targeted | 20/20 | 0.04 s |

The nested composition suite is not included in the structure discovery count.
Expected argparse rejection diagnostics within the structure suite are tested
guards, not failed tests. Logs preserve actual first invocations and must be
explicitly included despite the repository's generic `*.log` ignore rule.
These test counts refer to source present during that run, rather than an
unqualified claim about later concurrent changes.

The protocol's paid-synthetic deferral sentence records the instruction present
at its freeze. The owner subsequently clarified that existing cheap/Jev work
can continue within the original cap; root's current instruction governs paid
execution. No sealed new validation release has been issued or opened here.

Native baseline is independently rebuilt below in `native_baseline/`; missing
system SQLite development headers require the shipped vendored amalgamation.
The dev preset retains warning-as-error, shared ABI and OpenSSL, and builds the
server. Results are reported separately from the historical system-SQLite
baseline, with source snapshot and exact environment preserved.
