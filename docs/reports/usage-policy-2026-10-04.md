# Thread 2 — usage policy and authoritative preset, 2026-10-04

Branch `gpt/usage-policy-2026-10-04`; [PR #10](https://github.com/klb-t/chatadhd/pull/10).
Rebased and pushed on current main `7282437b1c88933977f64b3468b9f42f7b400494`.
**Ready for W9's independent acceptance of the usage-policy increment.**
The authoritative-preset dependency is resolved without waiting for W11.
Renewed startup/config work remains a separate follow-up below.

## Delivered

- Durable native estimates, rolling measured baselines, atomic reservations,
  exact-receipt confirmation/rejection, actuals, cancellation and inspection.
  Growth factor, window, seeds, reservation comparison and SQLite waiting are
  settings. Resource/cohort names remain caller data; declared estimates do not
  train measured baselines. The unchanged preset requires confirmation at ×10.
- Authoritative JSON preset profile
  [`loom/data/policy/usage_policy.pack`](../../loom/data/policy/usage_policy.pack),
  preserving all six historical values. `.pack` avoids changing the separate KB
  manifest/recursive JSON validator outside this lane. The deterministic
  generator embeds original bytes; `--check` rejects drift. No active manual
  C++ copy remains. No total source-size ceiling was introduced.
- One checked decoder feeds `Config::get` fallback, `loom_config_defaults`,
  effective options, settings and default `UsagePolicy::open`. Existing stored
  `loom_usage_policy` uses validated shallow replacement. Read-only snapshots
  and previews report stored/preset/effective values, canonical hashes and the
  source-byte hash. Reading defaults creates neither an override nor a config
  file. Bad compiled presets return errors; legacy reference APIs throw rather
  than substitute values.
- Opaque graph method/version/run/prompt/parameter references remain bound to
  receipts and recorded events; the actual graph contract belongs to W3/W4.
  [API/settings contract](../../loom/src/policy/README.md).

Code/data commit `a3b127c`; native tests, isolated replay tool and documentation
`5a73a36`. Each commit was immediately pushed with `[skip ci]`. No main, STATE,
root README, web, KB manifest or other lane's implementation was edited.
Paid provider calls and Actions requested: **0**.

## Before / after

| Check | Previous W2 checkpoint | Authoritative-preset increment |
|---|---:|---:|
| Full CTest, server/CLI/shared enabled | 108/108 | 108/108, 274.38 s |
| Focused actual-kernel policy groups | 24/24 | 25/25; original 19 retained |
| Usage-preset fields initialized manually in C++ | 6 | 0 |
| Default paths verified against edited data | — | 5/5 |
| Isolated real-kernel source/validation variants | — | 4/4 |
| Generator byte-preservation/drift cases | — | 5/5 |
| Renewed startup/config groups migrated | 0/5 | 0/5, separate follow-up |
| New paid provider calls | 0 | 0 |

[New receipt and reproduction](../../loom/src/policy/tests/evidence/2026-10-04/preset/README.md).
WERROR, Debug/O0, assertions, bundled SQLite and all existing thresholds remain
enabled. Symbols use `-g0` for storage pressure. Dedicated RAM temporary files
avoid shared-storage failures. Timings are observations, not a matched benchmark.

Actual case-count guard: **107 executed CTest entries, 659 native cases,
24,465 assertions, 1,276 Python cases, zero Python skips**. The existing opt-in
`unit.test_catalog_scale` entry is explicitly unexecuted. Original JUnit stdout
was capped in 16 entries and its first coverage guard was rejected; both are
retained. Matching full `LastTest.log` restored only those stdout fields, leaving
test outcomes/metadata unchanged. The unchanged W8 guard accepts that full
evidence. No test rerun or result rewriting was used to repair the capture.

The source-edit proof recompiles the actual preset-consumer translation unit
and links it before the fresh actual kernel archive. Synthetic edited values
(12.5, window7, timeout123, reservations false, named/null baselines and an
extension) reach all five paths. Config overlay replacement/save/restart works
without rebuilding. Missing-field and factor1 presets produce four Result
errors and three legacy exceptions, without creating config/ledger/ledger parent.
Watched repository inputs and libraries remain unchanged throughout that proof.

Earlier [original receipt](../../loom/src/policy/tests/evidence/2026-10-04/manifest.json)
and [settings receipt](../../loom/src/policy/tests/evidence/2026-10-04/settings/manifest.json)
remain intact, including failed/time-out/ENOSPC attempts. The complete pre-preset
series is preserved at `archive/2026-10-04/usage-policy-before-preset` (`910a1d6`).

## Not delivered and why

DIC-0325–0329 cover startup directory recipes/names, twelve legacy defaults,
event-log/worker presets and origin metadata. W11 publishes config/runtime-path
descriptors and its generic engine on its branch, not current main. Wiring also
needs assigned `config.h`/runtime-bootstrap/storage ownership, which is outside
W2's editable files. [Concrete handoff](../../loom/src/policy/PROFILE_HANDOFF.md).
These follow-ups do not block the now independent usage-preset migration.

The current user overlay is `config.json`'s `loom_usage_policy`; generic per-root
RuntimeProfile file overlays are not claimed. Shared ABI/CTest registration
needs its assigned header/build owner; current command is static-kernel only.
Config writes lack CAS/rollback; hashes alone do not prove durable save. Finite
binary64 quantities are not exact-decimal billing. Admission receipts neither
grant paid-call permission nor ensure exactly-once dispatch/external cancellation.

## Other-lane awareness

Fresh final fetch: main remains `7282437`; W3 `15c0c08`, W4 `1377e20`,
W11 `d3488a6`, W8 `a29534a`. Reviewed their reports/Do2 sections. W3/W4 keep
the existing effective-options helper and `(ledger_path, options)` signatures;
this increment preserves both. Their branches are not relabelled as merged W2
integration results. No blind/holdout data was read.

## Do wątku 1

Own extraction/model budgets, checkpoints and candidate maxima as editable
data/presets. Declared cost estimates must not train measured baselines.

## Do wątku 3

The builtin data preset is ready; retain existing Config/effective/open calls.
Keep cohort/input-byte semantics stable and unknown quantities null. Dispatch
only when status is allowed and authorized; write method provenance with W4's
agreed graph format, preserving opaque references in estimates/actuals.

## Do wątku 4

The shared data preset is ready without a W4 loader. Preserve actual root and
explicit ledger path. Unresolved authorization can hold a reservation but cannot
permit redispatch. Packet dispatch/cancellation atomicity remains your separate
durable execution contract; this policy alone cannot abort external work.

## Do wątku 5

Retain source-byte accounting and parser/options extensions with exact-receipt
CLI confirmation. Audit estimates do not train baselines. Own configurable
depth512/inline1,000,000-byte presets and checkpoint recovery evidence.

## Do wątku 6

Own catalog stem/sketch/Bloom/hash/expansion presets and truncation/coverage
evidence. W2 did not inspect blind/holdout data.

## Do wątku 9

Take this usage-policy increment first through your fresh build/full CTest/web
gate. Remove the obsolete authoritative-preset/W11 blocker from INDEX after
independent acceptance. Keep renewed DIC startup/config work explicitly separate;
assign config/header/bootstrap/origin/persistence/public ABI/CTest registration
ownership. The complete older series is archived; no main changes were reverted.

## Do wątku 10

Use settings/preview as before. Display `preset_source: embedded_data` and
`preset_document` source-byte identity separately from canonical options hashes.
`{}` restores current preset values while remaining a configured override.
Preview is advisory; confirmation binds the exact receipt. Shared/JNI consumers
still need the assigned ABI owner.

## Do wątku 11

Current usage defaults already come from one authoritative W2 data profile.
Your future `loom/data/runtime/usage_policy.pack` wrapper must derive from that
source rather than maintain a second manual set of defaults. Preserve native
nullable/open validation and shallow Config replacement when unifying loaders.
Config/runtime_paths foundation and descriptors can enable the later DIC adapter
after header/bootstrap ownership is assigned. Config preset, saved-file presence
and explicit user choice require distinct origin metadata; legacy serialized
default equality cannot establish intent. Archive/search/memory/worker/media/
network limits and their guard wiring remain your scope.
