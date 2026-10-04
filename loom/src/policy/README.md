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
  "growth_factor": 10.0,
  "baseline_window": 32,
  "ledger_busy_timeout_ms": 30000,
  "include_reservations": true,
  "initial_baselines": {}
}
```

The authoritative document is
[`loom/data/policy/usage_policy.pack`](../../data/policy/usage_policy.pack).
It contains UTF-8 JSON; `.pack` keeps it separate from the KB loader's recursive
`.json` schema validation. The six preset values are no longer initialized in
C++. The owner-authorized migration is self-contained on W2's branch, based on
current main; it does not require thread 11's runtime-profile engine or changes
to the KB manifest/loader.

Regenerate the committed embedding after changing the source document, and
check that it matches before building:

```bash
python3 loom/src/policy/gen_usage_policy.py
python3 loom/src/policy/gen_usage_policy.py --check
```

The generator emits byte-exact, independently chunked C++ literals in
`usage_policy_preset.inc`. Octal byte escapes preserve UTF-8, BOM and CRLF bytes
regardless of compiler character encoding; chunks impose no total-data ceiling.
`--check` fails on a missing or stale embedding. The kernel reads the compiled
document, so editing the repository source requires regeneration and rebuilding.
Deployed kernels do not need the repository file at runtime.

`usage_policy_preset()` parses and validates the complete embedded object,
including all six required fields. Config's absent-key fallback,
`effective_usage_policy_options()`, settings and default ledger opening share
that checked decoder. Default `UsagePolicy::open(path)` supplies an empty
override and loads the preset inside its `Result` path. A malformed compiled
preset returns an error; the legacy reference-returning
`usage_policy_defaults()` and absent-key Config interface throw rather than
substituting C++ values. An explicit stored Config value keeps its historical
raw-value semantics; use the effective helper for validated complete options.

The implemented user overlay is `config.json`'s `loom_usage_policy` object,
with whole top-level policy fields replacing preset fields. RuntimeProfile's
per-root profile-file overlay is future integration work described below.

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
Submitting `{"loom_usage_policy": {}}` restores the active preset's values;
the empty override is still stored, so `source` remains `configured`. There is
no override-deletion command in this API, and submitting `null` as the whole
policy is invalid.

Each `UsagePolicy::open()` captures an options snapshot. Updating configuration
does not mutate an already-open C++ instance. Open a new instance for the new
options; recorded requests and confirmations remain bound to their own
snapshots. The C command API opens a policy with the current effective settings
for each execution/inspection command; settings capabilities report this as
`preset_application: "next_policy_open"`.

## Settings snapshot and advisory preview

The C++ helper
`usage_policy_settings(const Config&, const Json* proposed_override = nullptr)`
returns a validated settings snapshot without changing configuration or opening
the ledger. Its JSON fields are:

| Field | Meaning |
|---|---|
| `preset` | Current complete preset. |
| `stored_override` | Policy object in the current Config, or `null` when no override is stored. |
| `effective` | Preset with stored top-level policy fields replaced. |
| `source` | `preset` or `configured`, according to whether an override is stored. |
| `preset_source` | `embedded_data`: the checked compiled canonical data document. |
| `preset_document` | Canonical `path`, `schema`, `encoding: "utf8_json"` and `source_sha256` of the complete original document bytes. |
| `override_semantics` | `replace_top_level_fields`; nested objects are replaced as whole values. |
| `hashes` | SHA-256 of each snapshot's canonical JSON; fields `algorithm`, `representation`, `preset`, `stored_override`, `effective`. |

`hashes.algorithm` is `sha256`, and `hashes.representation` is
`loom.canonical_json`. The three snapshot hashes are hexadecimal strings;
`hashes.stored_override` is `null` when no override is stored. These identify
the parsed JSON snapshots, not original configuration-file bytes. They are
inspection metadata and do not authorize an operation or replace its receipt.
`preset_document.source_sha256` is separately an original-byte hash, including
formatting, UTF-8 BOM or CRLF if present; it is not the canonical options hash.

Supplying `proposed_override` validates a replacement policy object and adds a
`preview` object. It contains `override`, its resulting `effective`,
`hashes: {"override": "...", "effective": "..."}`, `effective_changed` and
`persisted: false`. Preview hashes use the same algorithm and representation as
the enclosing snapshot. The top-level fields still describe the currently
stored settings; only `preview` describes the proposal.

The JSON C command `{"action":"preview_settings","override":{...}}` exposes
this helper. Both `settings` and `preview_settings` return `ledger_path` and
`capabilities` and leave configuration and the ledger untouched. An invalid
proposal returns the normal error envelope. The preview is advisory: it is not
a compare-and-set write, and another caller may change settings before saving.
Save the intended override through the existing `loom_set_config_json()` and
read `settings` again to inspect the active result.

The snapshot reads the in-memory Config; it does not independently verify the
on-disk file. The existing generic config API can retain an in-memory change
after a save error. A settings hash identifies that active snapshot, not proof
that a failed save persisted it.

### Future runtime-profile unification

The earlier [profile handoff](PROFILE_HANDOFF.md) was prepared against W2
`03b0c4e` and W11 `096028e`, when usage defaults still lived in C++. That
dependency no longer blocks the implemented usage-preset migration. The handoff
now distinguishes the current self-contained embedding from future integration
with thread 11's generic `RuntimeProfile` engine and per-root profile overlays.

Future unification must consume the same authoritative source, rather than
introducing a second independently maintained set of usage defaults. Keep
native complete-policy validation and stored top-level replacement semantics;
the profile engine's recursive overlay merge is a different operation. Report
actual profile provenance only after that adapter exists and is verified.

The handoff also retains the startup/path and historical config presets from
DIC-0325–0329, and the Config-origin, header/bootstrap and public-ABI ownership
work needed for their migration. Those follow-ups remain open. The current
usage document is not registered in the KB manifest or loaded as a
RuntimeProfile descriptor.

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

### Method and run provenance in the graph

Analysis methods, versions, recipes, prompt hashes, parameters and runs are
graph entities under the owner's 2026-10-04 clarification. Execution adapters
retain references to those entities in estimate extensions. The complete
estimate, including nested extensions, is preserved and bound to its receipt.
Changing a method version or its parameters under an existing operation ID is
a conflict; a distinct execution needs its own operation ID and accounting.
Actual extensions are retained in recorded `actual` events and can be read
through `inspect().events[].payload`.

Threads 3 and 4 own the shared graph format and result-to-method provenance
edges. This ledger treats their references as opaque caller data and does not
create graph nodes or edges. No second method registry or vocabulary is added
here. Choose `baseline_key` deliberately when method versions or parameters
change comparability; references do not automatically determine the cohort.
The focused graph-provenance regression uses explicitly synthetic example
fields, checks receipt binding and conflicts, and verifies retained estimate
and actual-event references after restart.

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
`<data-root>/usage-policy.sqlite`; `settings` and `preview_settings` report that
path without opening the ledger. A first `preview` can therefore initialize the empty database, but
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
| `settings` | None. Returns the settings snapshot, source metadata, hashes, `ledger_path` and `capabilities`. |
| `preview_settings` | `override`, a proposed replacement policy object. Returns the same snapshot plus advisory `preview`; saves nothing and does not open the ledger. |
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
{"action":"preview_settings","override":{"baseline_window":null,"include_reservations":true}}
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

## Settings and confirmation screen — thread 10

Use `settings` to show preset, stored override and effective values separately,
with their source, canonical hashes and ledger location. Show `embedded_data`
and the canonical document's path and source-byte hash. Use
`preview_settings` to inspect and validate an edited override before saving;
display its effective result and whether it changes the active policy. Restore
preset values by saving an empty override, with the stored-source distinction
described above. Explain whether comparison includes
reservations, which cohort/units were selected, how much measured history is
available and which dimensions have an unknown baseline or estimate. Display
each triggering resource, its baseline and projected usage alongside the exact
receipt. Record approval or rejection through `confirm`, using an explicit
owner-decision reference.

On a confirmation conflict, present a fresh receipt obtained by repeating the
same pending request and obtain the owner's decision against that receipt.
Never reuse the old receipt for a changed projection. A terminal replay or an
unresolved reservation is recovery state, not permission to dispatch the
operation again. The execution adapter must own dispatch recovery and reopen
its C++ policy when it needs changed configuration.

The existing server links `loom_core`, so thread 10 can expose authenticated
HTTP adapters that call `loom_usage_policy_json()` through
`loom/usage_policy.h`, release strings with `loom_free_string()` and preserve
the normal error/decision envelopes. Those routes and the screen are not
implemented by thread 2. A client using the shared library or JNI still needs
the separate public ABI registration described above.

Changing settings is not itself an execution approval. A receipt is not an
authorization for another operation. Expose unresolved reservations for recovery
and distinguish measured, provider-reported, declared and unknown consumption.
This contract supplies the native API; the screen and production adapter wiring
are separate work.

## Reproduce the standalone static-kernel contract tests

From the repository root, with the normal development CMake build already
configured in `loom/build/dev`:

```bash
python3 loom/src/policy/gen_usage_policy.py --check
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
