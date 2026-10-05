# Execution profiles: contract for threads 1, 2, 3, 9 and 10

Canonical execution data are JSON documents in `loom/data/runtime/<domain>.pack`.
The extension is intentional: KB Pack recursively validates `.json` as KB schemas;
an execution descriptor must not masquerade as a lexicon or policy KB document.
Existing archive lexicons still use their existing KB schemas and user overlays.
The built executable embeds generated copies, so installation does not depend on
its working directory. Regenerate with `python3 loom/src/model/gen_runtime_profiles.py`;
`--check` rejects stale generated source. Canonical descriptors remain the source.

`docs/contracts/runtime_profile.schema.json` describes a definition:
`schema`, `domain`, positive `revision`, `defaults`, `value_schema`.
`docs/contracts/runtime_profile_overlay.schema.json` describes an overlay.
The generic interpreter supports only `type`, `required`, `properties`, `items`,
`additionalProperties`, `minimum`, `maximum`, `minLength`, `minItems`, `enum`;
`type` can also be a nonempty array of supported names (for example integer/null
for a nullable bound); unsupported assertions fail. UI annotations are `title`, `description`, `x-setting`
(RFC 6901 pointer), `x-unit`, `x-consumer`. Do not label an absent annotation as a
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
`inspection()` returns domain, revision, effective values, executable value schema,
content-derived hash and builtin status. `values()` exposes the validated recipe;
`hash()` hashes the canonical definition and actual effective values.
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

CLI entry points:

```sh
loom --data-dir /path/to/data profile list
loom --data-dir /path/to/data profile inspect selector
loom --data-dir /path/to/data profile validate selector --file overlay.json
loom --data-dir /path/to/data profile save selector --file overlay.json
```

`validate` and `save` also accept a partial overrides object from standard input.
Save atomically writes an RFC 6902 diff from builtin to effective values,
preserving removals and unmentioned settings. A malformed existing overlay must first be repaired as a
file; CLI deliberately does not silently ignore it. CLI help, boolean flag names,
command aliases and numeric presets use the CLI profile. Aliases select existing
operations; adding a label does not add arbitrary executable code.

Templates use inert `{{name}}` or `{{/RFC6901/pointer}}` substitution against real
operation values. Missing variables and unterminated placeholders fail. Inserted
text is never reinterpreted as template or code. Graph provenance classifications,
recorded source fields, wire schema identities, crypto algorithms and machine
instruction dispatch remain engine contracts, not presentation settings.

## Provenance and cache behavior

Default output and legacy input hashes are preserved for builtin-equivalent values.
Nonbuiltin materialize/knowledge settings enter fingerprints before cache lookup,
including orchestration task identity, so an overlay cannot reuse an old product.
Archive effective lexicons/settings enter its existing stage input identity only
when changed. Rendering inspection exposes content hashes even for builtin values.
A version hash is evidence of the recipe, not evidence that model statements are
correct. Regex output remains regex; model output must not be relabeled as recorded.

## Do wątku 1

Use `semantic_analyzer` for checked rules and inspectable analysis defaults. Semantic
LLM prompt/request composition is still your scope; its existing Analyzer reference
must be constructed with the effective data directory to use an overlay. Preserve
which analyzer actually produced a result when recording recipe hashes. Do not
stamp an overlay hash on output produced by a different builtin analyzer.

## Do wątku 2

Use descriptor pointers/values as estimates' inputs, and apply the shared usage
policy before operations. ArchiveConfig/CAPI clamps and callers still identified
in thread-2/thread-11 inventory need an explicit compatible configuration path.
Profile schema validation itself is not a confirmation guard.

## Do wątku 3

Legacy selector and graph-memory adapters now have recipe inspection and checked
configuration/context APIs. Use them during migration to the method registry;
method ids, combination semantics and per-hit traces remain your scope. A missing
embedding provider is an unavailable capability, not an embedding result backed by
keyword scoring. Explicit caller context fields win; omitted fields use presets.

## Do wątku 10

Build the editor from `inspection().value_schema`, not copied field lists. Display
units where declared, effective hash, validation errors and reload requirements.
Native C++ and CLI APIs exist; server/C ABI exposure is not added by this thread.
Thread 1 owns prompt preview/one-shot model query editing; RuntimeProfile templates
are not that query-preview API.

## Do wątku 9

Wire profile-aware constructors at runtime/application entry points outside thread
11; add server/C ABI adapters only in their owners' scopes. Integrate consumer
commits together with the profile engine and generated canonical data. Run full
ctest and web build on the rebased branch before fast-forwarding main. Inventory
anchors deliberately refer to the original baseline, even after source movement.
