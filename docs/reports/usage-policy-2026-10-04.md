# Thread 2 — usage policy and settings, 2026-10-04

Branch `gpt/usage-policy-2026-10-04`; draft [PR #10](https://github.com/klb-t/chatadhd/pull/10).
Base/current main: `161cc22dfb84fe863389d6b90323bd44516a68dc`.
Interface published first (`c794c3c`), implementation `682af21`, original
receipt `34cc920`, settings continuation `03b0c4e`. Same branch, additive API.
No main/STATE/README/web/profile implementation files edited. Paid calls and
GitHub Actions executed: **0**. Readiness: **blocked by authoritative profile data**.

## Delivered

- Durable native `UsagePolicy`: estimates, rolling measured baselines, atomic
  reservations, exact-receipt confirmation/rejection, actuals, cancellation and
  inspection. Window, growth factor (preset ×10), seeds, reservation comparison
  and SQLite lock waiting are settings. Resources/cohorts are caller data.
  Unknown stays unknown; exact ×10, including `0.1 → 1.0`, requires confirmation.
- Coherent settings and read-only `preview_settings`: preset, stored override,
  effective values, source, replacement semantics and canonical SHA-256 hashes.
  Preview never saves or opens the ledger. Existing config APIs validate policy
  patches; historical defaults/file bytes/model choices remain unchanged.
- Owner's method-in-graph clarification: opaque method/version/run references,
  prompt hashes and parameters are receipt-bound estimate extensions. Actual
  extensions remain in events. Added restart/conflict/confirmation regression;
  graph entities/edges use the shared3/4 format. [API contract](../../loom/src/policy/README.md).
- Reviewed W11's five renewed startup/config groups. The exact
  [profile handoff](../../loom/src/policy/PROFILE_HANDOFF.md) contains six usage
  values, twelve historical settings, path recipes and ownership/dependency
  work. This proposal installs no authoritative data.

## Before / after

| Check | Before continuation (`34cc920`, retained) | Fresh continuation |
|---|---:|---:|
| Full CTest, server/CLI/shared enabled | 108/108 | 108/108 |
| Focused actual-kernel policy groups | 19/19 | 24/24 |
| Read-only settings command variants | 1 | 2 |
| Canonical current-settings hash fields | 0 | 3 |
| Usage-preset fields remaining in C++ | 6 | 6, blocked |
| Renewed DIC startup/config groups migrated | 0/5 | 0/5, blocked |
| New paid provider calls | 0 | 0 |

Original [receipt](../../loom/src/policy/tests/evidence/2026-10-04/manifest.json)
remains unchanged. [Continuation receipt](../../loom/src/policy/tests/evidence/2026-10-04/settings/manifest.json).
Focused tests link the actual
built kernel; fake HTTP sentinels assert zero calls. WERROR, Debug/O0, assertions
and bundled SQLite remain enabled; symbols disabled (`-g0`) for storage pressure.
Standalone `.cc` groups supplement the unchanged 108 CTest entries.

Negative attempts remain complete: first full CTest **95/108** (11 generated
helper permission failures and two 60-second research timeouts), serial retry
**106/108** (same two timeouts), focused retry **1/2**. Diagnostic structure on
full shared storage recorded ENOSPC; unchanged structure with RAM temporary
files passed **859/859**. Contracts diagnostic passed **209/209 in 65.811 s**,
exceeding the CTest gate and therefore not a green CTest result. Final CTest
uses dedicated RAM `TMPDIR` and the helper's restored executable permission.
No test, timeout or threshold changed. Timings are environmental observations,
not a matched performance benchmark. Failed/interrupted build logs are retained.

## Not delivered and why

Preset source remains C++, honestly labelled `legacy_code_pending_pack_migration`.
W11's reusable `RuntimeProfile` engine and authoritative usage/config/path
profiles are absent from main. Removing the only current preset before that
foundation arrives would leave default callers without data. The handoff reuses
W11's loader and records required header/bootstrap ownership; no parallel loader
or out-of-scope data/model changes were made. DIC-0325–0329 remain open.

The command is static-kernel only; public ABI/CTest registration is an ownership
gap. Generic config writes lack CAS/rollback; save errors may retain RAM changes,
so snapshot hashes do not prove persistence. Quantities are finite binary64,
not exact-decimal billing records. Caller lanes own dispatch; accounting receipts
do not ensure exactly-once execution or grant paid-call permission.

## Other-lane awareness

Fresh fetch reviewed W1 `f407a7c`, W3 `36ec2a8`, W4 `3caa6b4`, W5 `9a8b88b`,
W6 `e0caa3b`, W9 `0bbea7b`, W10 `2f25145`, W11 `b88154c`. W4/5 consume the API. W10 now has HTTP/panel
integration using `preview_settings`; its older pending report is not treated
as current code. W9 archived the same-baseline research timeouts separately.
No holdout/blind data was read. The starting [limits audit](../LIMITS_AND_WIRING_2026-10-01.md)
is historical `33fb30a`, not proof another lane's latest limits remain unchanged.

Post-evidence fetch: main stayed unchanged; W3 advanced to `ed9fe75` (embedding
and goal-typing receipts), W4 to `fb859f5` (durable dispatch/settlement recovery),
W9 to `125a043`. Their new implementation commits were inspected as dependencies,
not relabelled as W2 integration-test results. Final rebase was up to date.

## Do wątku 1

Own extraction lower-only budgets, checkpoints and candidate-graph maxima as
editable data/presets. Preserve evidence invariants; declared estimates do not
train measured baselines.

## Do wątku 3

Use the shared policy for embedding/model typing, index/context work and large
rendering. Configure invocation/byte/token/time/concurrency presets. Dispatch
only allowed status with authorization. Preserve method/version/run references
in ledger extensions and write graph edges with4's shared format. Cohorts remain
caller-defined; changing versions may change comparability.

## Do wątku 4

Keep the shared ledger, packet request hashes and graph-method/run references.
Verify `fb859f5` against the integrator's archived unresolved-replay regression:
unresolved may retain authorization for a held reservation, but its recovery
status cannot permit another dispatch. W2 has not certified the new dispatcher.
Own KB query/page presets and the shared graph-method provenance format. Usage
data now proposes W11 RuntimeProfile, replacing the earlier usage-specific KB
schema proposal.

## Do wątku 5

Keep measured source-byte accounting and source/parser/options extensions with
exact-receipt CLI confirmation. Audit estimates do not train measured baselines.
Configure depth512/inline1,000,000-byte presets; preserve source/completeness and
recovery evidence.

## Do wątku 6

Own catalog stem/sketch/Bloom/hash/expansion presets, preserving truncation and
coverage evidence. Lane2 did not inspect blind/holdout data.

## Do wątku 9

Assign public ABI/CTest registration, `config.h`, runtime bootstrap, config
persistence/CAS/origin metadata and core first64 scheduling ownership; these gaps do not expand
the integrator's implementation scope. Coordinate W11's foundation/data before
W2's adapter, then return W2 for edited-data equivalence verification. Keep W2
blocked until that migration is real. Record these handoffs in INDEX and preserve
existing interface work.

## Do wątku 10

Keep the new HTTP/panel integration. Show preset/source/hashes and replacement
semantics; preview is advisory, `{}` restores values but remains configured.
Approval binds the exact receipt; projection conflicts need a fresh request.
Require allowed status plus authorization; unresolved/terminal replay is recovery.
Shared/JNI needs the assigned ABI owner.

## Do wątku 11

Publish the reusable profile foundation and usage/config/path descriptors from
PROFILE_HANDOFF; regenerate existing embed and retain equality/schema gates.
Return the dependency to2 for its adapter, preserving shallow stored overrides
and nullable/open native validation. Own guard calls before search/index rebuild
and memory render; coordinate embedding/context with3. Archive/search/semantic/
media/GitHub/worker/net/CLI policy remains in your scope; provider-list/core
scheduling ownership still needs9's assignment.

Your renewed Config-origin request needs the assigned header/storage API.
Historical `contains/all` includes defaults and cannot prove explicit intent;
legacy serialized defaults have unknown intent. The concrete distinction is
recorded in PROFILE_HANDOFF; it remains open with the shared-profile dependency.
