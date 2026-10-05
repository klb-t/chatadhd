# Execution profiles: contract for threads 1, 2, 3, 5, 9, 10 and 12

Canonical execution data are JSON documents in `loom/data/runtime/<domain>.pack`.
The extension is intentional: KB Pack recursively validates `.json` as KB schemas;
an execution descriptor must not masquerade as a lexicon or policy KB document.
Existing archive lexicons still use their existing KB schemas and user overlays.
The built executable embeds generated copies, so installation does not depend on
its working directory. The current generated registry contains the 19 historical
domains and three additional inspection-only import domains. Use
`RuntimeProfile::domains()` for the installed binary's actual domain list; a
registered descriptor does not establish that a consumer uses it.

`loom/data/runtime_sources.pack` (`loom.runtime_profile_sources/1`, described by
`docs/contracts/runtime_profile_sources.schema.json`) registers source projections
with `output`, a template `definition`, and `projections` containing `source`,
`pointer` and `target`. Paths are relative to `loom/data`; pointers are RFC 6901,
with an empty pointer selecting the whole document. A projection replaces an
existing template slot, rather than merging source maps. Standalone descriptors
remain canonical; a generated wrapper derives its preset from its declared
authoritative source. In particular, `runtime/usage_policy.pack` takes its
`defaults` from W2's sole canonical `policy/usage_policy.pack`.

Regenerate with `python3 loom/src/model/gen_runtime_profiles.py`; `--check` checks
both generated wrappers and the embedding without writing. `--data-dir` and
`--output` support isolated copies. The generator validates all recipes, source
documents and standalone definitions, then prepares all results before replacing
files. Malformed/duplicate JSON, nonfinite values, invalid Unicode, escaping paths
or symlinks, source cycles, overlapping projections, absent pointers and schema
errors leave previous artifacts unchanged. Final replacement is atomic per file;
it does not promise multi-file rollback after a filesystem failure during commit.
Source projection adds metadata without changing the historical definition/value
hash or the 19 existing embedded definitions.
JSON parsing preserves native signed/unsigned 64-bit integer tokens; wider finite
integer tokens become doubles as in nlohmann JSON. This is representation, not a
resource ceiling: open numeric extensions remain available, while a genuinely
integer setting rejects a double under its declared schema.

`docs/contracts/runtime_profile.schema.json` describes a definition:
`schema`, `domain`, positive `revision`, `defaults`, `value_schema`.
`docs/contracts/runtime_profile_overlay.schema.json` describes an overlay.
The generic interpreter supports only `type`, `required`, `properties`, `items`,
`additionalProperties`, `minimum`, `maximum`, `minLength`, `minItems`, `enum`;
`type` can also be a nonempty array of supported names (for example integer/null
for a nullable bound); unsupported assertions fail. UI annotations are `title`, `description`, `x-setting`
(RFC 6901 pointer; `<index>` and `<key>` are placeholders to instantiate for array
and dictionary entries), `x-unit`, `x-consumer`. Do not label an absent annotation as a
measured quantity. Numeric validation compares integer/double values exactly;
large unsigned integers do not wrap or round when checked against bounds.

## Effective data and changes

Order: embedded canonical defaults → `<data_dir>/profiles/<domain>.pack` →
explicit one-call overrides. Objects merge recursively; arrays and scalar values
replace. Arrays may be replaced with empty arrays unless the descriptor declares
an operational minimum. An object merge does not delete an existing dictionary
key. Optional `patch` applies an RFC 6902 operation array after `overrides`, so
users can remove or replace dictionary entries explicitly. Every final value
still validates against the domain descriptor; deleting a required field fails.
`null` is a value, not a deletion instruction. Unknown keys fail unless
that particular dictionary advertises `additionalProperties`.
The overlay envelope always requires `overrides`, including when only `patch`
changes values; use an empty object in that case. A patch-only envelope missing
`overrides` is invalid, not silently repaired.

Example `<data_dir>/profiles/selector.pack`:

```json
{
  "schema": "loom.runtime_profile_overlay/1",
  "domain": "selector",
  "overrides": {"tfidf": {"max_features": 0}}
}
```

Check the inspected domain schema for actual names and zero semantics. This
example's zero means unlimited vocabulary; it does not universally mean unlimited.
Missing overlay files use defaults. Invalid JSON, wrong domain/schema, unsupported
settings, invalid types/ranges, unreadable files and dangling symlinks return an
error. They do not quietly revert to builtin behavior. The common profile object
is immutable; creating an override does not mutate another operation or instance.

Bounds in descriptors describe operation semantics or the representable C++ type,
not a spending policy. Preset counts can be raised. RuntimeProfile does not decide
whether growth needs confirmation: thread 2's usage-policy API is the authority
for estimates and the ×10 decision; thread 9 must wire that policy at operation
boundaries. This change does not claim that unconnected operations are guarded.

## API and expert editor

`RuntimeProfile::domains()` lists installed domain identifiers.
`builtin(domain)`, `load(domain, data_dir, overrides)`, `from_definition(definition)`
and `with_overrides(overrides)` return checked immutable objects.
`with_values(values)` validates an exact replacement; `with_patch(patch)` applies
RFC 6902 operations; `with_overlay(document)` validates the overlay envelope.
`inspection()` returns domain, revision, full `definition`, canonical `defaults`,
effective `values`, executable `value_schema`, content-derived `hash`,
`source_provenance` and `is_builtin`. `defaults()` and `source_provenance()` are
also available directly. Each source entry has `path`, `pointer`, `target` and
`raw_sha256`, calculated from the original bytes before BOM removal or newline
parsing. Derived presets include their manifest template and projected sources;
standalone presets identify their own pack.

These sources describe construction of the preset, not user-layer origin or a
method run. They remain attached through `with_values`, `with_overrides`,
`with_patch` and file overlays; `from_definition` has an empty source array because
it was not given a file source. `values()` exposes the validated recipe; `hash()`
still hashes only the canonical definition and actual effective values. Source
metadata are outside that historical hash. `is_builtin` means effective-value
equality with the preset, including an explicit choice equal to the preset; it
does not establish absence of user intent or activation of a configuration.
Consumers revalidate injected profiles against their own builtin descriptor before
reading required fields; a permissive caller-defined schema is not authorization
to supply an incomplete recipe. Consumer APIs preserve explicit legacy options.

An expert editor should fetch inspection, edit only supported fields, validate a
candidate, show the resulting effective values/hash, then atomically save an overlay.
Do not insert secret credentials into profiles. Provider authorization continues to
come from Secrets; these profiles describe non-secret endpoints, formats and headers.
Changes to an object-backed engine take effect when it is recreated or explicitly
reconfigured; file-backed operation loaders read changes for the next operation.
Selector reconfiguration rebuilds its derived index transactionally; failure retains
the prior working configuration and index. A new profile is not a live mutation of
an operation already in progress.

Immutable builtin descriptors are parsed, validated and hashed once for a binary;
user overlay files are read afresh. Archive legacy helpers share an immutable
builtin view instead of reconstructing dictionaries per token. Exact replacement
still validates every field: numerically equal floating values cannot bypass an
integer schema. Embedding index, query and reconfiguration share count, dimension
and finite-coordinate validation; malformed batches preserve the prior index.

CLI entry points:

```sh
loom --data-dir /path/to/data profile list
loom --data-dir /path/to/data profile inspect selector
loom --data-dir /path/to/data profile validate selector --file overlay.json
loom --data-dir /path/to/data profile save selector --file overlay.json
```

All four profile operations use read-only root discovery and do not open Runtime,
databases, config, a usage ledger or task recovery. Discovery preserves the existing
explicit/environment/sentinel/candidate precedence without initializing the root
or creating/changing its sentinel. `list`, `inspect` and `validate` create no data
files. `save` creates only directories needed for
`<root>/profiles/<domain>.pack` and atomically writes that checked overlay; it does
not initialize other application files. Stateful application commands retain
Runtime initialization. Command/subcommand aliases resolve before dispatch, and an invalid CLI
overlay still fails bootstrap explicitly. This discovery path preserves the
historical behavior while its shared public owner API remains pending.

`validate` and `save` also accept a partial overrides object from standard input.
The exact `schema: loom.runtime_profile_overlay/1` identifies an envelope;
ordinary open domain data may contain a key named `overrides`. One read of the
existing overlay supplies both checked effective values and saved user choices.
Missing files use builtin values; existing invalid, unreadable or non-UTF-8 files
fail. CLI treats `ENOTDIR` as no possible overlay file for a read, while saving to
that location returns a filesystem error. A malformed existing overlay must first
be repaired as a file; CLI deliberately does not silently ignore it.

Save preserves explicit scalar/array choices even when equal to the preset.
Earlier choices merge with the new input, then reflect the final validated values
after patch application. A corrective RFC 6902 diff preserves removals and
unmentioned settings; surviving explicit `add/replace/copy/move` targets are saved
as final `replace` choices, including equal values. Removed fields do not return,
and an earlier array-index removal is not replayed against a shorter final array.
The serialized overlay is checked to reproduce the validated profile hash before
atomic write. Atomic write provides no CAS between concurrent editors. Intent
already erased by an older diff-only file cannot be reconstructed; RFC 6902 removal
is not W12's durable identity exclusion.

CLI help, boolean flag names, command aliases and numeric presets use the CLI
profile. Aliases select existing operations; adding a label does not add arbitrary
executable code.

For `usage_policy`, CLI `validate/save` additionally call W2's
`validate_usage_policy_options` on the full proposed effective values before
writing. This catches semantic constraints such as `growth_factor > 1` and
nonempty cohort/resource names beyond the generic schema, while preserving native
nullable settings and open extensions. It is editor validation, not admission,
confirmation, execution authorization or guard activation. W2 still reads active
overrides from `config.json/loom_usage_policy` with its own shallow top-level
replacement semantics. Saving a profile overlay does not activate that Config
override or open the ledger.

Templates use inert `{{name}}` or `{{/RFC6901/pointer}}` substitution against real
operation values. Missing variables and unterminated placeholders fail. Inserted
text is never reinterpreted as template or code. Graph provenance classifications,
recorded source fields, wire schema identities, crypto algorithms and machine
instruction dispatch remain engine contracts, not presentation settings.

## Import recipes and capability boundary

`import_formats`, `import` and `import_audit` are explicitly **UNWIRED** in their
definitions and value schemas. They support inspection and checked data overlays;
native import, import CLI and native/Python audit do not consume these domains.
The exact recipes are captured from W5 source
`4e8c3dec29bb3aa1e343e09bee00b7768f0fc0f2`; see
[import data contract](implementation-import-profile-data-2026-10-05.md) for pinned
public source blobs and the data/Python-only comparison. Registration, an alias
or an inspected mode name does not create a parser capability.

| Domain | Recipe and current distinction |
| --- | --- |
| `import_formats` | Ordered extension/sniff/MIME tables, native mode names, legacy/provider roles and text/Markdown/OCR rules. JSON excludes `system`; SQLite maps it to `assistant`. |
| `import` | Library defaults plus separate optional CLI overrides: library `resume=false`, `include_result_metadata=true`; CLI `true`/`false`. Planned precedence is library → CLI caller layer when applicable → explicit per-call options. |
| `import_audit` | Shared estimator assumptions and separate native/Python callable/CLI scopes. Prices remain `null`; Python callable prefix is 8000, CLI prefix is 0 with scope `all`. |

Reader buffer positivity/native representation are validity constraints; depth 512
is a caller preset, and depth 0 or generic-inference bytes 0 mean unlimited in that
recipe. W5 must validate supported modes/parser capabilities after resolving data.
Existing native/Python audit validation remains required for finite
`low >= high > 0` and paired input/output prices; the supported schema subset
cannot express strict positivity or these cross-field constraints. Source hashes,
captured bytes, CAS, checkpoint atomicity, callback/cancellation operations and
parser/output schema identities remain native contracts. OCR preserves snapshot
bytes and the original declared format (`jpg`/`image/jpg`), not a normalized JPEG
alias. Archive shared streaming and actual import binding are separate owner tasks.

## Existing W12 layer adapter

Use W12's existing `DefaultLayers` and
`runtime_profile_values(definition, layers, bindings)`; do not add another resolver
or store. The adapter constructs `RuntimeProfile::from_definition`, applies
resolved values and validates an exact replacement with `with_values`, returning
inspection. Its source provenance is empty because no preset source file was
supplied; layer intent/history belongs to W12's resolver rather than the preset
source array or `is_builtin`.

Bindings map non-root RFC 6901 pointers to stable layer keys; overlapping pointers
are a conflict. Disabled, excluded, proposal or missing values are removed before
native validation. Suppression of a required field returns an explicit schema
error and never restores a preset. Stable IDs/areas and production bindings belong
in data; array position alone is insufficient for an exclusion that must survive
reordering. Snapshot persistence/CAS through existing `OnboardingStore` and actual
consumer wiring remain separate integration work. The existing source manifest
can register a future `runtime/onboarding.pack` from
`profiles/user.pack:/runtime_definition`; no such recipe is active until its
authoritative source and registration are actually present. That registration uses
`definition: {}` and a projection with `source: profiles/user.pack`,
`pointer: /runtime_definition`, `target: ""`; it does not create a second resolver.
See the
[layers/methods contract](layers-methods-contract-2026-10-05.md).

## Provenance and cache behavior

Default output and legacy input hashes are preserved for builtin-equivalent values.
Nonbuiltin materialize/knowledge settings enter fingerprints before cache lookup,
including orchestration task identity, so an overlay cannot reuse an old product.
Archive effective lexicons/settings enter its existing stage input identity only
when changed. Rendering inspection exposes content hashes even for builtin values.
A version hash is evidence of the recipe, not evidence that model statements are
correct. Regex output remains regex; model output must not be relabeled as recorded.

Knowledge and materialization verify their planned recipe hash before execution,
after handlers and before each product write. A profile changed during an operation
returns conflict instead of storing a result under the earlier cache key. Materialize
products are ordered entries from the four registered renderers. `on_error: error`
propagates an error; `omit` records `complete: false` and structured
`omitted_products`. Error-free default products retain their historical shape.

## Do wątku N

**Do wątku 1:** Use `semantic_analyzer` for checked rules and inspectable analysis defaults. Semantic
LLM prompt/request composition is still your scope; its existing Analyzer reference
must be constructed with the effective data directory to use an overlay. Preserve
which analyzer actually produced a result when recording recipe hashes. Do not
stamp an overlay hash on output produced by a different builtin analyzer.

**Do wątku 2:** Use descriptor pointers/values as estimates' inputs, and apply the shared usage
policy before operations. ArchiveConfig/CAPI clamps and callers still identified
in thread-2/thread-11 inventory need an explicit compatible configuration path.
Profile schema/native editor validation itself is not a confirmation guard.
Keep `policy/usage_policy.pack` authoritative; agree a shared read-only discovery
and explicit activation path with the Config/runtime owners rather than treating a
saved overlay as an active policy.

**Do wątku 3:** Legacy selector and graph-memory adapters now have recipe inspection and checked
configuration/context APIs. Use them during migration to the method registry;
method ids, combination semantics and per-hit traces remain your scope. A missing
embedding provider is an unavailable capability, not an embedding result backed by
keyword scoring. Explicit caller context fields win; omitted fields use presets.

**Do wątku 10:** Build the editor from `inspection().value_schema`, not copied field lists. Display
units where declared, effective hash, validation errors and reload requirements.
Native C++ and CLI APIs exist; server/C ABI exposure is not added by this thread.
Thread 1 owns prompt preview/one-shot model query editing; RuntimeProfile templates
are not that query-preview API.
Display full definition/defaults/effective values and preset source hashes, while
keeping user intent and activation separate. An equal-value choice may have
`is_builtin:true`; required-field suppression is a visible error. Mark the three
import domains UNWIRED and do not advertise their aliases as working capabilities.

**Do wątku 9:** Assign profile-aware runtime/application entry points outside thread11 to their
implementation owners; server/C ABI adapters must also stay in their owners' scopes.
Integration ownership does not grant permission to edit otherwise unassigned code. Integrate consumer
commits together with the profile engine and generated canonical data. Run full
ctest and web build on the rebased branch before fast-forwarding main. Inventory
anchors deliberately refer to the original baseline, even after source movement.
Check projected wrappers and embedding with generator `--check`; scoped proofs do
not establish full gates, production layer bindings or activation of import/usage
profile overlays.

**Do wątku 5:** Consume the three import domains through the existing RuntimeProfile
loader in your own code scope, preserving native library/CLI defaults, explicit
caller precedence and audit semantic validators. Validate real parser/mode
capabilities, default output/provenance/checkpoint parity and effective recipe
hashes before activation. Shared archive streaming remains a separate adapter;
do not silently inherit the import depth preset for its previously unlimited path.

**Do wątku 12:** Use the existing DefaultLayers/runtime_profile_values adapter with
actual canonical definitions and data-defined stable bindings. Do not restore
suppressed required values, infer user provenance from `source_provenance`, or
create a second resolver. Register projected onboarding data only when its real
source is present; persistence/CAS and actual consumers remain separate owner work.
