# Thread 2 — data preset and future runtime-profile handoff

W2 now carries a self-contained usage-preset migration based on current main.
Its authoritative source is
[`loom/data/policy/usage_policy.pack`](../../data/policy/usage_policy.pack),
containing the exact six historical values, including floating JSON `10.0`.
The owner instructed W2 to complete this migration without waiting for other
lanes. No C++ initializer containing those six values remains.

The earlier proposal at W2 `03b0c4e`, based on W11 `096028e`, depended on adding
a generic RuntimeProfile usage descriptor first. That was a proposed contract,
not an installed profile. It no longer blocks the current migration. Future
unification with W11's reusable engine remains separate work and must consume
one authoritative usage source rather than maintaining parallel default values.

## Implemented canonical data path

The `.pack` file contains UTF-8 JSON. Its extension avoids the KB loader's
recursive validation of `.json` documents against separate KB schemas. This
migration does not modify KB manifest, schemas, loader or embedded KB data.

From the repository root:

```bash
python3 loom/src/policy/gen_usage_policy.py
python3 loom/src/policy/gen_usage_policy.py --check
```

The committed `loom/src/policy/usage_policy_preset.inc` embeds exact source
bytes in independent chunks with octal byte escapes. This preserves BOM,
CRLF and UTF-8 bytes independently of compiler character encoding and does not
cap total document size. `--check` rejects a stale or missing embedding.
`--source` and `--output` allow isolated source-change verification. The source
identifier continues to name the canonical repository resource when using those
comparison arguments. Generation checks JSON syntax and finite numeric
representation; native validation checks the policy contract.

`usage_policy_preset()` is the shared checked decoder: parse the embedded
object, validate its known fields and require all six complete-preset fields.
It returns an error for malformed compiled data. The compatibility reference
`usage_policy_defaults()` throws on that error rather than resurrecting literal
values. Config's absent-key fallback, `loom_config_defaults()`, effective
options, settings and default ledger opening all use that decoder.

`UsagePolicy::open(path)` has an empty-object default argument and loads the
checked preset inside its `Result` path. Explicit options replace whole preset
fields after validation. Config's stored `loom_usage_policy` object has the
same shallow semantics: replacing `initial_baselines` must not restore removed
cohorts or resources. Unknown fields and nullable native values survive.
`Config::get` retains its raw stored-value behavior when a value exists;
validated complete policy consumers use `effective_usage_policy_options()`.
Reading the fallback neither stores the preset nor writes `config.json`.

Settings report `preset_source: "embedded_data"` and `preset_document` with
`path`, `schema`, `encoding: "utf8_json"` and `source_sha256` of original source
bytes. Their existing `hashes` remain canonical JSON hashes of preset, stored
override and effective options. Formatting can change the source-byte hash
without changing the canonical options hash. Neither hash grants execution.
`preview_settings` stays advisory, nonpersistent and ledger-free. Reopen a
policy instance to apply changed options; confirmation receipts continue to bind
exact options, estimate, baseline and projection.

The implemented owner overlay is `config.json`'s `loom_usage_policy`. There is
no current `RuntimeProfile::load("usage_policy", data_root)` call or
`<data-root>/profiles/usage_policy.pack` overlay in these consumers. Repository
source edits take effect after generation and rebuilding; deployed binaries
use their compiled canonical data rather than requiring a repository checkout.

## Future generic runtime-profile unification

W11's inspected `096028e` foundation supplies a generic RuntimeProfile engine,
schemas and generator. Integrate it when the dependency is available, using
W2's canonical usage document as the authoritative values source. A generated
RuntimeProfile wrapper may add domain/revision/schema metadata, but must derive
its defaults from that document rather than introduce independently maintained
values. Coordinate the generic generator/descriptor work with its owner; W2
owns only its config/policy adapter and consumers.

The adapter must preserve these contracts:

1. Load the generic builtin definition and its per-root profile overlay through
   the shared engine. Do not cache a mutable user-overlay file forever or invent
   a fallback when an existing overlay is malformed.
2. Validate complete loaded values with `validate_usage_policy_options()`.
   Required fields are `schema`, `growth_factor`, `baseline_window`,
   `ledger_busy_timeout_ms`, `include_reservations` and `initial_baselines`.
   Resource/cohort names and unknown extension fields remain open.
3. Read one coherent config snapshot, then replace whole top-level fields from
   stored `loom_usage_policy`. Keep this operation separate from recursive
   profile-file merging and revalidate the resulting complete policy.
4. Route every root-aware default consumer through that checked adapter,
   including Config fallback, settings/previews, effective options and ledger
   opening. Retain a checked builtin compatibility view without literal values.
   For a custom ledger path, pass the real config/profile root explicitly;
   do not silently infer it from the ledger's parent.
5. Report actual definition/domain/revision/profile provenance only after this
   adapter exists. Preserve canonical options hashes and original canonical
   source-byte identity as distinct values. A profile hash may identify both
   descriptor and effective profile values; it is not execution approval.

Native validation permits `baseline_window: null` or a positive int64, a finite
factor greater than one, finite nonnegative baseline quantities or `null`, and
a nonnegative SQLite signed-int timeout. W11's earlier inspected schema accepted
a single `type` string without nullable unions or `exclusiveMinimum`; an
integer-only nullable-field descriptor would change this contract. Coordinate
schema capabilities with W11 and retain native validation regardless of generic
schema support. The timeout bound is adapter representation, not a spending
ceiling. There is no quantity or spending maximum.

The generic engine's `with_values()` replacement operation can validate values
after W2's shallow override; profile-file recursive `overrides` and optional
RFC 6902 `patch` remain a separate layer. User-profile behavior requires its own
root-aware verification and must not be inferred from the current Config overlay.

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
| DIC-0329 | Loom defaults: `loom_event_log_types` = `["conv:created", "import:done", "graph:changed"]`; `loom_task_workers` = `1`. Compose usage fallback from its own authoritative data source rather than duplicating it here. |

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

W11's newer `b88154c` semantic-consumer handoff also needs Config origin
information for `default_max_nodes`/`default_depth` precedence. `Config::all()`
and `contains()` include seeded historical defaults; their presence does not
prove an explicit user choice. Track preset values, persisted presence and
explicit choices separately when adding the assigned header/storage API.
Legacy files may have serialized defaults without retaining user intent;
report that origin as unknown instead of inferring intent from equality or
presence. Usage-policy snapshots already distinguish their unseeded stored
override from the fallback, but this is not a general origin API for the
twelve materialized historical settings.

## Verification before admission

- Compare builtin usage values against the historical six-field golden object,
  including `10.0`, and preserve the existing policy lifecycle groups.
- Run the current generator's equality check and demonstrate source changes in
  an isolated build using `--source`/`--output` and the actual native config
  implementation. New values must reach Config's fallback, default open,
  settings and effective options, with the correct source-byte hash. Verify
  malformed compiled policy data fails; retain original, variant and negative
  receipts. Restore canonical data before the full build and gates.
- Verify stored shallow overrides win; `baseline_window: null` and open
  extensions survive; replacing `initial_baselines` preserves deletions;
  preview neither persists data nor creates a ledger. Full CTest and focused
  policy checks must pass, without changing existing thresholds or tests.
- For future DIC migrations, compare config key order/values, candidate order,
  sentinel bytes, DataPaths suffixes and file-write behavior against the baseline
  above. These checks do not claim the recorded startup presets have migrated.
- For future RuntimeProfile integration, independently edit a user profile
  without rebuilding. Reopening root-aware entry points must expose changed
  options and actual profile provenance. A malformed existing profile must fail.

Earlier committed verification receipts describe their recorded pre-migration
source revisions. They do not establish that this new data migration passed;
its gate results belong in the updated W2 report and new evidence receipt.

## Do wątku 11

Continue the reusable RuntimeProfile foundation. Future usage integration must
derive its descriptor's defaults from W2's authoritative
`loom/data/policy/usage_policy.pack`, preserving its numeric identity and native
validation. Agree how the generic generator consumes that source, then retire
the standalone embedding path when all consumers use the shared engine. Do not
install a competing usage-default document or treat generic profile overlays as
already implemented in W2. Coordinate nullable schema support as needed.

Provide canonical `config`/`runtime_paths` descriptors with the exact DIC values
above so W2 can implement those remaining tasks after ownership is assigned.
The current usage-preset migration does not wait for that follow-up.

## Do wątku 9

Admit the current self-contained usage migration only after its rebase, full
and focused gates, generator equality check and edited-data comparison pass.
Its canonical document and generated embedding travel with W2; the usage preset
no longer depends on W11 integration. Set readiness from those actual results.

Coordinate future generic-engine unification and the `config.h`, runtime
bootstrap, Config-origin and public-config-getter ownership needed by DIC
follow-ups. Public registration of `loom_usage_policy_json` and its shared ABI
gate remains a separately assigned change in `loom.h` and ABI tests; linking the
static kernel does not register that shared symbol. No KB-pack or another lane's
implementation files are edited by this handoff.
