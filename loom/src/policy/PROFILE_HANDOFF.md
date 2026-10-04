# Thread 2 — authoritative runtime profile handoff

This is a proposed integration contract, not an installed preset. At W2
`03b0c4e`, usage defaults still come from C++; current main does not contain an
authoritative usage profile. W11 `096028e` supplies the existing generic
`RuntimeProfile` engine, schemas and generator. Reuse that engine; do not add a
second loader or generator, and do not modify another thread's files from W2.

## Proposed usage descriptor

W11 can add `loom/data/runtime/usage_policy.pack`, domain `usage_policy`, and
regenerate its existing `loom/src/model/runtime_profiles_embedded.inc` with
`loom/src/model/gen_runtime_profiles.py`. The `.pack` extension is intentional:
KB Pack recursively validates `.json` documents against its separate schemas.
This path needs no change to the KB manifest, loader or embedded KB pack.

The following complete descriptor works with W11's current schema vocabulary:

```json
{
  "schema": "loom.runtime_profile/1",
  "domain": "usage_policy",
  "revision": 1,
  "description": "Usage admission preset. The consumer must additionally run validate_usage_policy_options on all effective values. baseline_window and baseline quantities retain their nullable native contract; unknown extension fields remain open.",
  "defaults": {
    "schema": "loom.usage_policy/1",
    "growth_factor": 10.0,
    "baseline_window": 32,
    "ledger_busy_timeout_ms": 30000,
    "include_reservations": true,
    "initial_baselines": {}
  },
  "value_schema": {
    "type": "object",
    "required": ["schema", "growth_factor", "baseline_window", "ledger_busy_timeout_ms", "include_reservations", "initial_baselines"],
    "additionalProperties": true,
    "properties": {
      "schema": {"type": "string", "enum": ["loom.usage_policy/1"]},
      "growth_factor": {"type": "number", "minimum": 1, "x-setting": "/growth_factor", "x-consumer": "UsagePolicy"},
      "ledger_busy_timeout_ms": {"type": "integer", "minimum": 0, "maximum": 2147483647, "x-setting": "/ledger_busy_timeout_ms", "x-unit": "milliseconds", "x-consumer": "SQLite usage ledger connection"},
      "include_reservations": {"type": "boolean", "x-setting": "/include_reservations", "x-consumer": "UsagePolicy projection"},
      "initial_baselines": {
        "type": "object",
        "additionalProperties": {"type": "object", "additionalProperties": true},
        "x-setting": "/initial_baselines",
        "x-consumer": "UsagePolicy declared baseline"
      }
    }
  }
}
```

W11 currently accepts a single `type` string, without nullable unions or
`exclusiveMinimum`. Therefore `baseline_window` is required but intentionally
not assigned an incompatible integer-only schema. The native W2 validator must
check the complete loaded values before use: window is a positive int64 or
`null`; factor is finite and greater than one; named baseline quantities are
finite, nonnegative numbers or `null`; timeout fits SQLite's nonnegative int.
Profile schema validation alone does not certify a valid usage policy. A later
generic nullable-union extension belongs to W11 and its schema owner.

Keep `10.0` as a floating JSON value to preserve existing serialized preset
identity. There is no spending or quantity maximum. The timeout maximum is the
adapter's integer representation, and the positive-window/factor checks define
the implemented arithmetic. Resource names, cohort names and unknown extension
fields remain data. Protocol identities and canonical hash algorithms remain
engine contracts.

## One W2 adapter, one default source

After the profile foundation and descriptor are available, W2 should use one
checked helper in `core/config*` for every usage-preset consumer:

1. Load `RuntimeProfile::load("usage_policy", data_root)`: embedded canonical
   definition, then `<data_root>/profiles/usage_policy.pack` through the existing
   profile overlay mechanism. Do not cache a mutable user-overlay file forever.
2. Validate the complete resulting values with the native usage validator.
   Required fields come from the descriptor; a missing descriptor or invalid
   existing overlay returns an error instead of resurrecting C++ policy values.
3. Read a coherent config snapshot. Replace whole top-level fields from stored
   `loom_usage_policy`; do not pass that object into recursive profile merging.
   In particular, replacing `initial_baselines` must not restore removed cohorts
   or dimensions. Revalidate the final complete policy. Unknown fields survive.
4. Use this helper for Config's absent-key fallback, effective options, settings
   and settings previews, and ledger opening. `Config::get` may retain its
   historical raw-stored-value semantics when an override is present; callers
   requiring a complete policy use the effective helper. No separate hardcoded
   preset may remain behind another entry point.
5. Route default `UsagePolicy::open` through the checked loader inside its
   `Result` path. Avoid a throwing default-argument expression. For a custom
   ledger location, supply the actual profile/config data root explicitly;
   do not assume it equals the ledger's parent without documenting that choice.
6. Report the actual definition/domain/revision/effective profile hash and
   canonical hashes of preset, stored override and effective policy. A profile
   hash identifies both descriptor and effective profile values; W2's options
   hash identifies canonical options only. Neither is an original-byte hash or
   execution approval. Keep one snapshot coherent across values and source.

The existing `with_values()` profile API validates exact replacement values;
it is suitable after W2's shallow config override. Profile-file `overrides`
retain W11's recursive semantics and optional RFC 6902 `patch` for deletion.
`preview_settings` remains advisory, nonpersistent and ledger-free. Reopen a
policy instance to apply changed options; confirmations still bind exact
options/baseline/projection receipts. Explicit complete operation options retain
their precedence over presets.

`usage_policy_defaults()` can remain a compatibility view of the checked
builtin profile. It must obtain its values from the same descriptor, with no
literal growth/window/timeout/reservation fallback. Root-aware execution and
settings paths must load the user profile rather than relying on that builtin
view. Update the source label once this is real; until then retain the honest
`legacy_code_pending_pack_migration` label.

## DIC-0325–0329 — exact startup/config baseline

W11's inventory points to main `161cc22d`, before edits. These are presets or
path recipes to transfer into canonical runtime data; they are not additional
product restrictions. The proposed profile domains are `runtime_paths` and
`config`, matching the existing `RuntimeProfile` domain vocabulary.

| Inventory | Existing recipe/value |
|---|---|
| DIC-0325 | Android candidates, in order: `/storage/emulated/0/Documents/ChatADHD`, `/storage/emulated/0/Download/chatadhd_data`. |
| DIC-0325 | Desktop with XDG: `<xdg_data_home>/chatadhd`, then `<home>/.chatadhd`; without XDG: `<home>/.chatadhd`. Absent HOME uses `/`. |
| DIC-0325 | Resolution precedence: explicit override → nonempty `CHATADHD_DATA` → first candidate containing sentinel → first candidate. Existing `HOME`/`XDG_DATA_HOME` process inputs and explicit paths keep their published meaning. |
| DIC-0326 | Sentinel filename `.chatadhd_data`; exact created content `ChatADHD data directory\n` (one trailing LF). Initialization creates `attachments`, `exports`, `logs` in that order. |
| DIC-0327 | DataPaths mapping below. |
| DIC-0328 | The ordered historical 12-key config JSON below. |
| DIC-0329 | Loom defaults: `loom_event_log_types` = `["conv:created", "import:done", "graph:changed"]`; `loom_task_workers` = `1`. Compose usage fallback from its own authoritative profile rather than duplicating it here. |

DataPaths currently joins each suffix to the selected root:

| Member | Suffix |
|---|---|
| `db` | `chatadhd.db` |
| `fts_index` | `chatadhd.fts.db` |
| `config` | `config.json` |
| `secrets` | `secrets.json` |
| `memory` | `memory.json` |
| `models` | `models.json` |
| `github_sync` | `github_sync.json` |
| `attachments` | `attachments` |
| `exports` | `exports` |
| `logs` | `logs` |
| `blobs` | `blobs` |

Preserve key order and JSON values in the proposed `config` descriptor:

```json
{
  "base_url": "https://openrouter.ai/api/v1",
  "default_model": "anthropic/claude-sonnet-4-20250514",
  "semantic_model": "",
  "temperature": 0.7,
  "max_tokens": 4096,
  "theme": "dark",
  "system_prompt": "You are a helpful assistant with access to the user's hierarchical memory.",
  "auto_title": true,
  "stream": true,
  "semantic_analysis": true,
  "graph_memory_depth": 2,
  "graph_memory_max_nodes": 20
}
```

Use the existing inert `render_profile_template()` for data-defined candidate
recipes, rather than branching on product/app names. Root discovery is a
bootstrap boundary: load builtin candidate recipes before the root is known;
load a per-root overlay only after locating its directory. An overlay inside an
unknown root cannot choose its own initial discovery location. Agree an explicit
startup-profile input if owner-controlled pre-root recipes are needed; do not
pretend the existing per-root loader already supplies that input.

Preserve fresh Config's 12-key order, no automatic config-file write, existing
model choices and existing-file bytes. Changing default filenames requires an
explicit existing-data compatibility plan rather than silently relocating files.
Pass the selected root when filenames may include nested paths; inferring it
from `Config::path().parent_path()` then loses the actual root.

The public `kSentinelFile` constant and Config declarations are in
`loom/include/loom/config.h`, outside W2's literal `src/core/config*` scope.
Changing their contracts or adding root-aware Config constructors requires an
assigned header owner. Runtime construction is in `loom/src/runtime.cpp`, also
outside W2. The existing public config getter lives in `capi_core.cpp`: its
static `loom_config_defaults()` fallback must use the actual root-aware preset
before claiming that shared config reads respect overlays. Coordinate that
config-only edit with the assigned CAPI owner; do not silently change unrelated
core commands.

## Verification before admission

- Compare builtin usage values against the historical six-field golden object,
  including `10.0`, and preserve the existing policy lifecycle groups.
- Compare config key order/values, candidate ordering, sentinel bytes, all
  DataPaths suffixes and file-write behavior against the baseline above.
- Demonstrate source-data changes: alter a canonical profile in an isolated
  test checkout, regenerate through W11's generator and rebuild. The same new
  value must reach Config's fallback, default open, settings and effective
  options. Restore the source afterward; retain original and changed receipts.
- Independently edit the user profile overlay without rebuilding. Reopening
  all root-aware entry points must expose changed options and source hashes.
  A malformed existing profile must fail rather than use an invented fallback.
- Verify stored shallow overrides win; `baseline_window: null` and open
  extensions survive; replacing `initial_baselines` preserves deletions;
  preview neither persists data nor creates a ledger. Full CTest and focused
  policy checks must pass, without changing existing thresholds or tests.

## Do wątku 11

Publish the reusable RuntimeProfile foundation and proposed `usage_policy`
descriptor as a small dependency commit, regenerate the existing embed and
verify its source equality gate. Coordinate nullable schema support if desired;
the descriptor above already preserves nullable values through native W2
validation. Provide canonical `config`/`runtime_paths` descriptors with the
exact values above so W2 can implement the renewed DIC tasks. W2 will implement
only its adapter/owned consumers; this document creates none of those data files.

## Do wątku 9

Assign and integrate the shared loader/data dependency before admitting W2's
adapter; nominal lane order must not install code without its required default
source. W2 cannot independently remove its only preset while the authoritative
descriptor and loader are absent. Coordinate the `config.h`, runtime bootstrap
and public-config-getter ownership where needed. Then rebase W2 on that actual
dependency, run its full and focused gates plus the edited-data comparison, and
update readiness status from verified results. No KB-pack or another lane's
implementation files are edited by this handoff.
