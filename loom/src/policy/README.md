# Usage policy API and settings contract

Thread 2 provides a common admission and accounting API for execution adapters.
The public interface is [`usage_policy.h`](../../include/loom/usage_policy.h).
The C++ class and JSON C command function are callable by applications linked
against the static `libloom_core.a` kernel. The new command function is
deliberately not exported by `libloom.so`: its declaration has no `LOOM_API`,
and the existing shared-library export set remains unchanged. The existing
shared `loom_get_config()` / `loom_set_config_json()` APIs expose the policy
setting and preset. Registering the new command function in `loom.h` and the
shared ABI gate is follow-up work for the integrator outside thread 2's scope.
This API does not itself execute an operation, call a model or import an archive.
Production chat, context, packet and import callers must explicitly adopt its
lifecycle; the existence of the ledger is not evidence that those paths already
use it.

## Preset and owner overrides

`loom_usage_policy` is a Loom-only configuration fallback. Its preset is:

```json
{
  "schema": "loom.usage_policy/1",
  "growth_factor": 10,
  "baseline_window": 32,
  "ledger_busy_timeout_ms": 30000,
  "include_reservations": true,
  "initial_baselines": {}
}
```

The growth factor, window, reservation comparison and initial baselines are
settings. They are not maximum spending, context, corpus or model limits. There
is no money-only interpretation: resource names are caller data, for example
`money_usd`, `input_tokens`, `output_tokens`, `cpu_seconds` or `storage_bytes`.
Additional resource names and opaque extension data are preserved.

`ledger_busy_timeout_ms` controls waiting for the SQLite ledger lock; zero
disables waiting. Its range is the underlying SQLite signed-int representation,
not a spending ceiling. The connection timeout is an owner-changeable preset.

`baseline_window: null` selects all available measured history. An explicit
window selects the most recent eligible measurements for each resource; the
baseline is their arithmetic mean. Once measurements exist for a resource,
they supersede its declared initial baseline. With
`include_reservations: true`, projected usage includes outstanding reservations
in the same cohort; `false` requests an operation-only comparison. The caller
may supply owner-chosen initial baselines as a map from cohort to resource
amounts, for example:

```json
{
  "initial_baselines": {
    "archive-import/local/bytes-v1": {"storage_bytes": 1000000},
    "context-typing/provider/model/token-v1": {"input_tokens": null}
  }
}
```

The default contains no invented initial baseline or minimum positive floor.
`null` means unknown, never zero. Missing baselines are reported and allow
admission; this is not a measured claim that growth is small. A known zero
baseline followed by a positive projection requires confirmation, rather than
being hidden behind a synthetic floor.

Use the existing `loom_set_config_json()` to store a policy override:

```json
{
  "loom_usage_policy": {
    "schema": "loom.usage_policy/1",
    "growth_factor": 10,
    "baseline_window": null,
    "include_reservations": true,
    "initial_baselines": {}
  }
}
```

Configuration merging replaces the whole value of a top-level key. A nested
policy object is not recursively merge-patched; `null` is a stored value, not a
delete instruction. Settings clients should read the effective policy, edit it
and submit the complete intended policy object. Other configuration keys remain
open and their existing semantics are retained. Reading the fallback does not
write it into `config.json` or replace an owner's configured model.

Each `UsagePolicy::open()` captures an options snapshot. Updating configuration
does not mutate an already-open C++ instance. Open a new instance for the new
options; recorded requests and confirmations remain bound to their own
snapshots. The C command API opens a policy with the current effective settings
for each execution/inspection command; settings capabilities report this as
`preset_application: "next_policy_open"`.

## Comparable cohorts and measurements

The caller supplies `baseline_key`. It names a cohort of comparable operations,
not an automatically inferred task category. Choose its scope deliberately:
operation kind, provider/model, workload definition and resource-unit version
can all affect comparability. Do not compare milliseconds with seconds, bytes
with tokens, one item with a whole batch or unlike providers under one key
without an explicit normalization. An owner may choose a broader or narrower
cohort; the API does not impose the grouping.

The operation estimate is:

```json
{
  "operation_id": "import-example-001",
  "baseline_key": "archive-import/local/bytes-v1",
  "resources": {
    "storage_bytes": 2500000,
    "cpu_seconds": null
  },
  "extensions": {"estimate_method": "archive-audit-v1"}
}
```

`operation_id` is the execution identity. Repeating an identical request is
idempotent; reusing the ID for a changed estimate is a conflict. Retrying a
distinct operation requires its own identity and accounting, rather than
silently reusing a previous authorization.

After execution, only `instrument_measured` and `provider_reported` quantities
train the rolling baseline. `declared` quantities are retained as declarations
and do not become measurements. Unknown actual dimensions retain their
reservation until resolved by a later completion or explicit cancellation.
Cancellation records the reason; it never invents a zero measurement.
Repeated completion does not duplicate a measurement. A resolved actual
quantity is immutable; a later completion may resolve previously unknown
dimensions. Realized consumption above the estimate remains recordable rather
than being discarded because the earlier forecast was wrong.

Known quantities use finite, nonnegative JSON numbers backed by the native
double representation. Overflow, nonfinite values and invalid signs are
representation errors, not owner spending ceilings. This ledger is not an
exact-decimal billing authority and does not replace the research programme's
Python decimal ledger, historical account reconciliation or authorized budget.
Retain exact provider receipts in caller provenance when exact billing matters.

## Lifecycle

| Call | Effect and caller responsibility |
|---|---|
| `preview(estimate)` | Read-only estimate against current baseline and reservations. It does not reserve resources or authorize execution. |
| `request(estimate)` | Records the admission decision. An allowed request reserves atomically; a request requiring confirmation does not authorize execution. |
| `confirm(operation_id, receipt_id, approved, confirmation_ref)` | Records the owner's decision against the exact request receipt, options, baseline and projected usage that were presented. |
| `complete(operation_id, actual)` | Records actual usage, resolves known resource reservations and updates eligible measured history. Unknown dimensions remain unresolved. |
| `cancel(operation_id, reason)` | Explicitly releases the outstanding reservation with a retained reason and without pretending the operation measured zero. |
| `inspect(baseline_key)` | Inspects the cohort's recorded baseline and operation state for settings and recovery. |

Admissions and lifecycle writes serialize through SQLite transactions. The
ledger is local and separate from core/knowledge schema migrations.
Reservations survive process restart. An adapter must recover them by completing
known actual usage or explicitly cancelling an operation; opening the ledger
does not assume that interrupted work cost nothing.

Opening a new ledger creates its database/schema metadata. The C API uses
`<data-root>/usage-policy.sqlite`; `settings` reports that path without opening
the ledger. A first `preview` can therefore initialize the empty database, but
the preview itself records no request, decision, reservation or measurement.

An adapter must request admission before executing, stop for a
`requires_confirmation` result, present the returned receipt to the owner,
record that decision and execute only after admission succeeds. A changed
projection requires a fresh request, not a silently broader grant. Re-requesting
the identical pending operation refreshes its receipt when its projection or
options changed; changing the operation estimate itself requires a new identity.
Keep the
operation identity, request receipt, confirmation reference, actual measurement
and retained first/provider response references together in the caller's
execution provenance.

## Static-kernel JSON C API

Include `loom/usage_policy.h` and link `libloom_core.a` to call
`loom_usage_policy_json(ctx, command_json)`. Loading `libloom.so` with Python
`ctypes` does not expose this new function. Shared-library registration and
an exported-symbol ABI check are still required before that use is available.

The function returns allocated JSON. Free every
returned string with `loom_free_string()`. Ordinary admission decisions,
including `requires_confirmation`, are JSON results; failures use the existing
C ABI error envelope. The API returns no credentials.

Admission results contain `status` and `authorized`. An adapter must check
for the admitted `allowed` state and authorization, rather than treating any
successful JSON response as permission to execute. Completed, cancelled and
denied operations are unauthorized; replaying their ID does not grant another
execution. An `unresolved` operation may retain `authorized: true` for its held
reservation while awaiting measurements; it is not a new execution grant. The
ledger does not implement exactly-once external dispatch: callers must retain
and recover their own dispatch state rather than re-executing an already
dispatched operation after restart. Resource diagnostics expose baseline
availability/source, estimate,
reserved/projected usage and whether that dimension triggers confirmation.

If an estimate or reservation is unknown, `projected` remains `null` and
`projection_status` is `partial_unknown`. Diagnostics retain
`reserved_lower_bound` and `projected_lower_bound`, calculated from known
nonnegative quantities only. A known lower bound that already reaches the
growth threshold still requires confirmation: an unknown pending amount cannot
hide known growth of at least ×10. A lower bound below the threshold does not
establish that the full projection is below it. For fully known projections,
`projection_status` is `estimated`.

The command discriminator is `action`:

| Action | Fields besides `action` |
|---|---|
| `settings` | None. Returns `preset`, `stored_override`, `effective`, `source`, `ledger_path` and `capabilities`. |
| `preview` | `estimate` with operation ID, cohort and resource amounts. |
| `request` | `estimate`, in the same shape as preview. |
| `confirm` | `operation_id`, `receipt_id`, `approved`, `confirmation_ref`. |
| `complete` | `operation_id`, `actual` containing `resources` and `provenance`. |
| `cancel` | `operation_id`, `reason`. |
| `inspect` | `baseline_key`. |

Example commands (completion and cancellation are alternative paths):

```json
{"action":"settings"}
```

```json
{
  "action": "request",
  "estimate": {
    "operation_id": "import-example-001",
    "baseline_key": "archive-import/local/bytes-v1",
    "resources": {"storage_bytes": 2500000, "cpu_seconds": null}
  }
}
```

```json
{
  "action": "confirm",
  "operation_id": "import-example-001",
  "receipt_id": "REPLACE_WITH_RETURNED_RECEIPT_ID",
  "approved": true,
  "confirmation_ref": "owner-decision-reference"
}
```

```json
{
  "action": "complete",
  "operation_id": "import-example-001",
  "actual": {
    "resources": {"storage_bytes": 2498000, "cpu_seconds": 1.8},
    "provenance": "instrument_measured"
  }
}
```

```json
{"action":"cancel","operation_id":"import-example-001","reason":"owner cancelled before execution"}
```

```json
{"action":"inspect","baseline_key":"archive-import/local/bytes-v1"}
```

## Future settings and confirmation screen

Use `settings` to show preset, stored override and effective values separately,
with their source and ledger location. Explain whether comparison includes
reservations, which cohort/units were selected, how much measured history is
available and which dimensions have an unknown baseline or estimate. Display
each triggering resource, its baseline and projected usage alongside the exact
receipt. Record approval or rejection through `confirm`, using an explicit
owner-decision reference.

Changing settings is not itself an execution approval. A receipt is not an
authorization for another operation. Expose unresolved reservations for recovery
and distinguish measured, provider-reported, declared and unknown consumption.
This contract supplies the native API; the screen and production adapter wiring
are separate work.

## Reproduce the standalone static-kernel contract tests

From the repository root, with the normal development CMake build already
configured in `loom/build/dev`:

```bash
cmake --build loom/build/dev --target loom_core
c++ -std=c++20 -pthread \
  -Iloom/include -Iloom/third_party/nlohmann \
  loom/src/policy/tests/test_usage_policy.cc \
  loom/build/dev/libloom_core.a \
  loom/build/dev/libloom_sqlite3_amalgamation.a \
  loom/build/dev/libloom_miniz.a \
  -lssl -lcrypto -ldl -lm \
  -o /tmp/loom-usage-policy-contract
/tmp/loom-usage-policy-contract
```

This links the C++ policy against the same static kernel and bundled
SQLite/miniz libraries used by the development build. It uses offline scripted
quantities and temporary local ledgers; it is not a live provider-cost or
performance measurement. The standalone source is deliberately `.cc`, so the
kernel's `.cpp` source glob does not incorporate its test `main()` into the
library. These focused tests supplement the full existing `ctest` gate.
