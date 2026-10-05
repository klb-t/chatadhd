# Context/chat presets: second W3 increment

Canonical inputs:

- `loom/data/runtime/context_goal_cues.pack`: DIC-0336 and DIC-0337.
- `loom/data/runtime/chat_reasoning.pack`: DIC-0368, DIC-0369 and DIC-0370.
- `loom/data/context/runtime_preset_layers.pack`: stable graph default identities,
  areas, RFC6901 bindings and historical identity hashes. It contains no copy of
  preset values.

Both runtime descriptors use W11's `loom.runtime_profile/1`. Regenerate the
source-private embedded copy with `python3 loom/src/context/gen_runtime_presets.py`;
`--check` detects drift. Generated graph entries take their values from those
same descriptors. Import that **entry contribution** into the user's existing
W12 pack through its normal forward update. Preserve its `pack_id`, stable
entry IDs and outer CAS revision. Do not combine an unrelated pack and state.

## Execution binding

`resolve_runtime_preset(definition, options)` accepts exactly one source:

- `{}`: generated built-in descriptor.
- `{"effective_values": {...}}`: complete exact replacement. No merging or
  fallback of missing fields. The caller supplies an already resolved snapshot.
- `{"layer_snapshot": {"pack": ..., "state": ..., "bindings": {...}}}`:
  actual W12 `DefaultLayers::create` and `runtime_profile_values`. Bind every
  top-level consumed setting; partial bindings cannot restore suppressed data.

With W11 present, validation uses `RuntimeProfile::from_definition` and
`with_values`. Before W11 integration, native consumers check their actual
fields and inspection explicitly says `validation_basis=native_consumed_fields`.
The binding contains no schema interpreter, file loader, merge, layer resolver,
or persistent state writer. Layer requests return `Unavailable` before both
W11 and W12 are integrated. Their implementations are tested only as explicitly
pinned isolated dependencies; this does not claim their presence on the base.

Inspection carries descriptor revision, effective-value hash, source, consumed
values, value schema and (for graph layers) effective resolution explanations.
The hash uses W11's canonical `{definition, values}` input. Configuration is
frozen before execution; callbacks cannot substitute another reasoning recipe.
With W11 present, inspection and `is_builtin` come from its actual factory;
`builtin_comparison=runtime_profile_exact_values`. Before that integration,
`builtin_comparison=canonical_json`: object-key order is immaterial, but numeric
representations such as integer `1` and floating-point `1.0` remain distinct.
The bridge does not implement another exact-number comparator.

## Owned entry points

`ContextEngine::type_goal` reads Config `context_goal_cues`. A synchronous scope
may replace its options through `goal_typing.goal_cues`. Forced goal types do
not run the unused cue recipe. Explicit settings retain inspection in
`goal.params.goal_cues`; their hash participates in the goal/context identity.
The historical hash in `legacy_identity_hashes` preserves existing goal JSON
and IDs for the current unconfigured preset. A different built-in descriptor
revision or value also retains inspection and changes identity. Keep the
historical anchor unchanged when upgrading presets; regenerating it from new
values would conceal that upgrade. The anchor is compatibility metadata, not
an effective value or an exemption from layer exclusions.

`ChatEngine::send` reads Config `chat_reasoning` once, before conversation or
transport mutation. It retains explicit inspection in both request-message and
assistant metadata, including request retention before a transport failure.
`ChatEngine::configure_reasoning` keeps its configuration-free compatibility
signature and uses the generated built-in recipe. No new public ABI is added.

Empty marker lists disable matching. Token budgets have no preset-derived
ceiling. Case folding of the model name for thinking-marker lookup and case-sensitive
matching of budget markers preserve the previous behavior, including incidental matches against
other provider names. Changing that classification is a separate quality task.

## Integration boundaries

Public settings/HTTP/UI and persistent user selection are outside W3. They must
pass the bound W12 pack/state pair or its validated effective values; they must
not reconstruct defaults after exclusion. Use `OnboardingStore::read`, not
`open`, for previews. The existing `loom.method_graph/1` / `loom.method_run_trace/1`
contract in `packet/METHOD_GRAPH.md` remains authoritative for executable method
and result provenance. A settings default entry is not a method execution or
evidence that a provider was called.

This increment does not finish the other 46 DIC groups, persistent resume wiring,
the default method catalogue, or the ordinary legacy chat usage-guard handoff.
It performs no paid requests and grants no provider-call authorization.
