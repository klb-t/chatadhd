# Method, version and run graph — packet-side contract v1

Status: a versioned **client data pattern**, with a synthetic native-DTO fixture.
It uses existing `make`, `empty_diff`, `compile_reply`, `apply_compiled_reply`,
`apply` and native GraphPacket store operations. It adds no dispatcher, host field,
algorithm-name branch or closed method/type/predicate vocabulary. W3 must adopt
the pattern in its registry and execution trace; its production adapter is not
implemented or verified by this document.

## Client envelope

`loom.method_graph/1` is a manifest for a caller's graph construction. It is not
a request operation accepted by `loom_packet`. Its fields are:

| Field | Meaning |
|---|---|
| `schema` | `loom.method_graph/1` |
| `vocabulary` | Caller-supplied entity-kind and predicate strings, indexed by the semantic roles below. Values are data, never dispatch instructions. |
| `bindings` | Native entity IDs for method identity, method version, prompt version, recipe version, preset version, combination version, run, model identity and compiler transform. |
| `definition_hashes` | SHA-256 of canonical JSON definitions; prompt hash separately covers exact UTF-8 prompt bytes. |
| `trace` | `loom.method_run_trace/1`: concrete method/version/run/model bindings, effective parameters, preset, combination, user overrides, input identity, response provenance and actual measurement availability. |

An input may register several methods or versions by repeating records and
bindings; there is no fixed method list or combination size. The single-method
fixture is a small regression case, not a product limit. IDs and vocabulary
strings are supplied by the caller. Unsupported execution mechanisms remain
unavailable; declaring a method entity never implements it or approves a call.

[`method-graph.schema.json`](method-graph.schema.json) checks the manifest/trace
shape and hash representation. The universal bindings are method identity,
specific version and run. A lexical method can omit model/prompt/recipe/preset/
combination/compiler roles or explicitly mark them null; it must not fabricate
these records. Applicable model fields remain typed. Unknown input identities
or measurements are explicit null. Hash equality and actual native graph edges
remain separate checks. `tests/check_method_graph.py` exercises both model and
lexical shapes, open caller data and invalid contracts.

## Distinct identities

| Role | Native representation and identity rule |
|---|---|
| Method identity | `model::Entity`; stable caller namespace/key. A later recipe does not overwrite its identity. |
| Method version | Separate Entity with the exact immutable definition and content hash in `attrs`. It identifies the effective recipe/model/parameters, not just a display name. |
| Prompt version | Separate Entity with exact text, byte hash and encoding; an Observation retains the captured definition bytes. |
| Recipe version | Separate Entity with canonical definition/hash, including the prompt binding and effective parameters. |
| Preset version | Separate Entity with declared defaults and hash. It is distinct from a run's one-call overrides. |
| Combination version | Separate Entity with ordered member-version IDs, weights, fusion/selection parameters and hash. No combination label invokes code. |
| Run | Separate Entity for a concrete attempt/replay, with input identity, effective settings and available instrumentation. Retry/replay semantics must be explicit. Missing measurements are null/unknown, not zero. |
| Model identity/content | Model identity is a descriptor Entity. Response nodes are the existing compiler's `node_ids`; supplied model output retains native `model_knowledge` and exact `model_origin`. |
| Compiler transform | Separate descriptor Entity identifying the existing projection implementation. It is not the model that produced the text and is not the caller's analysis method. |

Version hashes use `json::canonical` (the current native canonical JSON encoder),
not file formatting. A prompt byte hash uses its exact UTF-8 bytes. A caller must
bind the effective values actually consumed, including user overlays; a builtin
hash must not be stamped on output from a different effective recipe. Method and
run records describe provenance; their existence is not a quality measurement.

## Native DTOs and real edges

The fixture uses complete current `Entity.to_json`, `Claim.to_json` and
`Observation.to_json` shapes. Its entity kinds and claim predicates are fixture
data. Their semantic roles are listed in `vocabulary`; the kernel need not know
these names. Native evidence/origin/status enums and field shapes remain codec
contracts. For example, native Claim has no `candidate` status; candidate
acceptance is expressed by application policy, not an invented enum value.

| Required relation | Actual native Claim subject → object |
|---|---|
| Version of method | Method-version Entity → method-identity Entity |
| Uses recipe/prompt/preset | Version/recipe Entity → corresponding version Entity |
| Combination member | Combination-version Entity → specific method-version Entity |
| Run requested method | Run Entity → specific method-version Entity, with prepared/captured state explicit |
| Result produced in run | Each compiled result Entity → run Entity |
| Result produced by method version | Each compiled result Entity → specific method-version Entity |
| Result projected by compiler | Each compiled result Entity → compiler-transform Entity |

Every relation is a real Claim with native `subject`, `predicate`, `object` and
`value:null`; it is not just a reference hidden in an attrs object. Native
sources supply reproducible supporting quotes/locators. Structural claims use
`confidence_scope: structure_only` and an identified projection operator.
Confidence in a verified binding does not confer confidence in model content.

Definitions, parameters, presets, combination membership and overrides also
remain inspectable in native entity attrs and immutable source bytes. Additional
assertions about them can be separate literal-valued Claims when needed.

## Composition with existing reply compilation

1. `make` registers the native method/version/prompt/recipe/preset/combination/run
   records and captured manifest sources. Alternatively, append their ordinary
   add/update records using `empty_diff` and `apply` against an existing packet.
2. Request the existing graph-reply format using that packet's exact ID. Use the
   unchanged `compile_reply` host fields. The effective recipe hash may populate
   the existing `host.recipe_sha256`; no extra host fields are introduced.
3. `compile_reply` and `apply_compiled_reply` retain their current deterministic
   compilation, model captures, turn structure and model-origin attrs.
4. A separate ordinary `empty_diff` proposal captures the run trace and adds the
   three required result provenance edges using the actual returned `node_ids`.
   Updating run attrs to `response_projected` is an ordinary identity-preserving
   update. Do not mutate the compilation, its diff or a retained source record.
5. Preview/apply remains the caller's explicit policy. Canonical persistence is
   the existing `loom_graph_packet_store` / `/api/graph/packets/store` boundary,
   with closed selection, explicit acceptance and expected-row CAS values.

The two-step composition leaves all five historical reply fixtures unchanged.
Bindings are additional packet records/history. It preserves the distinction
between method execution, model content and the compiler's structural transform.
The projection API makes no provider call and does not approve paid execution.

## Dated evaluations are separate claims

An evaluation of a model/method/version is a separate literal-valued native Claim,
not a confidence rewrite on provenance edges or response nodes. Its qualifiers
include the assessment date/scope; its value identifies the evaluated version,
model, evaluator, dataset/input identities, dimensions and measurement provenance.
Exact evaluator output/instrument receipts are separate Observations with support
quotes and hashes. Revisions create new records/history rather than erasing the
earlier assessment or pretending it applied to a newer method version.

The fixture deliberately contains an **unmeasured synthetic model judgement**:
native `assessment.origin:model_knowledge`, explicit evaluator `model_origin`,
`content_verification:unverified`, and `measurement_status:unavailable`. Its score
is labelled a model judgement, not a measured accuracy result. Recording output
is evidence that those bytes were captured; it is not evidence that the assessed
method is better. Acceptance never establishes that judgement as world truth.

## Fixture and minimal compatibility regression

`tests/method-graph-fixture.json` contains the client envelope, native
`base_make_request`, unchanged-format `reply_request` (the test supplies `packet`),
the deterministic `binding_diff`, explicit `apply_policy`, and expected IDs/hashes.
All source/model data are synthetic, with zero provider calls.

The existing `tests/compat/test_graph_packet_store.py` suite now exercises this
pattern through the real packet C ABI and KB store:

1. Make the base and check `expected.base_packet_id`.
2. Compile the supplied reply against that base; apply the compiled reply using
   the fixture policy. Check `expected.reply_packet_id` and actual `node_ids`.
3. Apply `binding_diff`; check `expected.bound_packet_id` and native validation.
4. For every result ID, find actual Claims linking it to the fixture's exact run,
   method-version and compiler-transform IDs with the caller's predicates.
5. Compare captured source bytes, model-origin attrs, effective parameters,
   prompt/recipe hashes, preset, combination and user overrides after composition.
6. Verify the evaluation is a separate dated model-origin Claim with a retained
   evaluator Observation; it must remain explicitly unmeasured/unverified.
7. Accept all closed native rows through the existing store; read/replay the
   receipt and compare the entire packet, including history, source bytes and
   provenance. If desired, restart the store before the final read.
8. Point a provenance edge at an absent target and require native validation to
   reject the dangling reference while preserving the earlier packet. The five
   original pinned compiler comparisons continue to run unchanged.

This is a native graph construction/persistence regression. It becomes a
cross-lane execution regression only when W3's actual adapter produces this
envelope/trace from its registry and the same checked effective recipe. Until
then, the contract is supplied to W3 and production adoption remains open.
