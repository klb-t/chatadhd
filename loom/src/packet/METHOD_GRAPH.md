# Method, version and run graph — packet-side contract v1

Status: the single W3/W4 **client data contract**, adopted by W3's production
registry at `03c670c` and exercised by its published synthetic execution golden.
It uses existing `make`, `empty_diff`, `compile_reply`, `apply_compiled_reply`,
`apply` and native GraphPacket store operations. It adds no dispatcher, host field,
algorithm-name branch or closed method/type/predicate vocabulary. This document
defines provenance representation; producer execution and native persistence
have separate proofs. The shared artifact is identified below.

## Client envelope

`loom.method_graph/1` is a manifest for a caller's graph construction. It is not
a request operation accepted by `loom_packet`. Its fields are:

| Field | Meaning |
|---|---|
| `schema` | `loom.method_graph/1` |
| `vocabulary` | Caller-supplied entity-kind and predicate strings, indexed by the semantic roles below. Values are data, never dispatch instructions. |
| `bindings` | Native entity IDs for method identity, method version, parameter-set version, prompt version, recipe version, preset version, combination version, run, model identity and compiler transform. |
| `definition_hashes` | SHA-256 of canonical JSON definitions; prompt hash separately covers exact UTF-8 prompt bytes. |
| `definition_records` | Exact definition values captured alongside the hashes; the corresponding Entity attrs and immutable Observation preserve them. |
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
| Parameter-set version | Separate Entity containing exact effective parameters and captured user overrides, with canonical definition/hash. The method version and run link to this Entity through actual Claims; a changed set gets a new version/identity. |
| Prompt version | Separate Entity with exact text, byte hash and encoding; an Observation retains the captured definition bytes. |
| Recipe version | Separate Entity with canonical definition/hash, including the prompt binding and effective parameters. |
| Preset version | Separate Entity with declared defaults and hash. It is distinct from a run's one-call overrides. |
| Combination version | Separate Entity with ordered method-version or nested combination-version occurrences, weights, fusion/selection parameters and hash. No combination label invokes code. |
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
| Combination member | Combination-version Entity → specific method-version or nested combination-version Entity |
| Run requested method | Run Entity → specific method-version Entity, with prepared/captured state explicit |
| Uses effective parameters | Method-version Entity and run Entity → exact parameter-set-version Entity |
| Run used combination | Run Entity → exact combination-version Entity, when a combination contributed |
| Result produced in run | Each compiled result Entity → run Entity |
| Result produced by method version | Each compiled result Entity → specific method-version Entity |
| Result projected by compiler | Each compiled result Entity → compiler-transform Entity |

Every relation is a real Claim with native `subject`, `predicate`, `object` and
`value:null`; it is not just a reference hidden in an attrs object. Relations
used for execution need active Claims and active endpoints of the declared
caller kinds. A rejected membership does not authorize execution. Native
sources supply reproducible supporting quotes/locators. Structural claims use
`confidence_scope: structure_only` and an identified projection operator.
Confidence in a verified binding does not confer confidence in model content.

Definitions, parameters, presets, combination membership and overrides also
remain inspectable in native entity attrs and immutable source bytes. In the
fixture, parameters are a separate addressable Entity and the run has a real
combination edge; traversal from a result reaches both without reading hidden
JSON references. Additional assertions about individual parameters can be
literal-valued Claims or parameter Entities using caller-supplied kinds and
predicates. This creates no closed list of parameter names or algorithms.

The versioned schema remains backward compatible with earlier manifests: the
parameter-set fields and exact `definition_records` are optional shape fields.
Current producer adoption must emit the applicable graph records and edges;
schema acceptance alone does not verify their equality or execution provenance.
Changing weights, order or composition creates a new combination version, even
when the member-method list stays the same. Old runs retain their original edge.

Version immutability protects `definition` together with `definition_sha256`,
and exact prompt `text` together with `text_sha256`. Changing or stripping those
protected fields requires a new version identity; changing them and restoring
them later cannot conceal identity reuse in packet history. Separate metadata
annotations can change under the same version identity, with ordinary history
and CAS. Captured `definition_records` still preserve the exact attrs snapshot
they describe; an annotation change does not rewrite an earlier Observation.

## Requested and observed models

The prepared trace's `requested_model` captures the model control actually
consumed by the instantiated request. When present, recipe/request/parameter
model controls must agree. It remains unchanged after execution, together with
the effective recipe, prompt, parameters and version hashes. The prepared
`model` is that request value; the final `model` describes observed native result
origins, rather than silently replacing the requested control in its definition.

| Final trace field | Required interpretation |
|---|---|
| `actual_models` | Distinct observed `model_origin.model` values, including null when the model report is unavailable. Values derive from actual result records. |
| `model`, `actual_model` | The one distinct observed model; null when observations differ or are unknown. |
| `actual_model_origins` | Distinct exact native model-origin records retained on the results. |
| `model_origin` | The one distinct native origin; null when origins differ. |
| `requested_model_identity_id` | The prepared descriptor identity retained when the requested and observed models differ. |
| `model_identity_id`, `actual_model_identity_id` | If requested and observed models agree, `model_identity_id` may retain the prepared descriptor and `actual_model_identity_id` may be absent. Otherwise an explicitly supplied matching native observed descriptor is used, or both fields are null when unknown. A reported alias does not authorize reuse of the requested descriptor. |

A reported alias or version name is an observation, not proof that two model
descriptors are equivalent. No name table or inferred equivalence is installed
by this format. Requested and observed names, exact result origins and captures
remain separate. The prepared artifact contract retains its requested bindings;
the final trace may change its model descriptor only according to the observed
result origins and an explicitly supplied matching descriptor. Other method,
version, run and compiler bindings remain fixed. Lexical execution supplies no
invented model trace.

`response_sha256` for compiled model results covers the exact compiler input;
`response_hash_scope` records `compiler_input`. It is the common known hash from
native result origins, or null if hashes are unknown or differ.
`raw_response_sha256` covers transport response bytes and
`response_text_sha256` covers rendered text; neither substitutes for compiler
input. `request_sha256` covers canonical parsed sent payload, while
`request_bytes_sha256` covers its exact captured bytes. These scopes remain
distinct even when a particular payload happens to give equal hashes.

## Ordered nested combinations

A combination definition's `members` array preserves ordered occurrences. Each
member supplies exactly one of `method_version_id` or `combination_version_id`,
with optional signed finite weight, arbitrary parameter data and preset binding.
Each target needs a real active `includes_method` Claim; the target's caller kind
and canonical definition hash must match the selected version role. A nested
combination is another ordinary version Entity, not hidden executable JSON.

The membership graph is a DAG for executable combinations. Cycle detection uses
the ancestors of the current traversal path. A shared child reached through two
branches is valid, as are repeated members within one array. Repeated paths are
not deduplicated: their member indices, order, signed weights, parameter layers
and fusion data retain distinct occurrences. There is no preset count, depth,
weight range or fixed method vocabulary imposed by the format. Arithmetic
representation failures remain explicit errors.

W3's `selection_path` captures original resolved IDs, member indices and
definition hashes. Preparing a consumed leaf creates effective method/parameter/
recipe versions and then effective combination versions from that leaf upward.
Only that occurrence is replaced; repeated siblings continue to reference their
original versions. Optional `effective_combination_versions` contains the exact
new native Entity DTOs in bottom-up construction order. The final
`combination_version_id` and `combination_sha256` bind the effective root, and the
run has its real `uses_combination` edge. Original path captures and old versions
remain intact; they are not restamped with the new root hash.

## Composition with existing reply compilation

1. `make` registers the native method/version/parameters/prompt/recipe/preset/combination/run
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
5. Compare captured source bytes, model-origin attrs, effective parameter Entity,
   parameter hashes/edges, prompt/recipe hashes, preset, the run's combination
   edge and user overrides after composition. Definition captures retain values
   as well as hashes; their attrs must equal the native records after replay.
6. Verify the evaluation is a separate dated model-origin Claim with a retained
   evaluator Observation; it must remain explicitly unmeasured/unverified.
7. Accept all closed native rows through the existing store; read/replay the
   receipt and compare the entire packet, including history, source bytes and
   provenance. If desired, restart the store before the final read.
8. Point a provenance edge at an absent target and require native validation to
   reject the dangling reference while preserving the earlier packet. The five
   original pinned compiler comparisons continue to run unchanged.

This W4 reference is a native graph construction/persistence regression. W3's
separate actual execution export below is the shared execution artifact; the
offline reference builder does not replace that producer proof.

## Shared producer artifact and native verifier

This file is the single W3/W4 format reference. Both reports must link here;
the published W3 export is the shared artifact, and W9 reviews the producer proof
together with W4's native receipt before closing the owner's joint gate. The
offline reference builder is explicitly W4-only evidence, not a replacement
registry or evidence of W3 execution.

The one canonical exported artifact is W3 `32e2381`:
[`docs/reports/chat-selector-2026-10-04-evidence/golden-consumer/input.json`](https://github.com/klb-t/chatadhd/blob/32e2381/docs/reports/chat-selector-2026-10-04-evidence/golden-consumer/input.json).
Its exact byte SHA-256 is
`8db8175c3b70c3947711ddf5daee5073b99bc5a5114ce0075e06e51932cec54e`.
The producer evidence beside it records 1 case / 97 assertions, one checked fake
HTTP call, zero paid calls and 3 results / 19 Entities / 30 Claims /
17 Observations. This artifact uses equal requested/observed model names and a
single-member combination. W3 has separate alias and nested-DAG regressions;
their existence does not make this artifact proof of those variant executions.

`loom.method_graph_fixture/1` is the test envelope already used by the synthetic
golden. A producer exports these additional concrete output fields:

| Field | Required captured value |
|---|---|
| `schema` | `loom.method_graph_fixture/1` |
| `contract` | Prepared `loom.method_graph/1` manifest, including graph bindings and exact definitions. |
| `trace` | Final `loom.method_run_trace/1`, with the actual effective parameters and complete declared `result_bindings`. |
| `packet` | Final native GraphPacket with definitions, run, sources, results, real provenance Claims and reversible history. |
| `result_entity_ids` | All outputs declared by this execution. Must equal the trace's result bindings without duplicates. |
| `definition_capture_source_id` | Native Observation whose exact text parses to `contract`. |
| `trace_capture_source_id` | Native Observation whose exact text parses to `trace`. |
| `producer_evidence` | Optional opaque diagnostic data; its presence does not verify execution. |

The current golden requires a parameter-set-version Entity and its two real
edges, even though earlier v1 shape schemas permit omission. Recipe/prompt/model/
preset/combination/compiler records apply only when those mechanisms contributed.
No model or prompt is fabricated for lexical execution. The versioned definition
attrs conventions checked by this profile are:

| Role | Captured attrs/definition |
|---|---|
| Method version | `definition`, `definition_sha256`; applicable parameter/recipe/preset hashes within the definition must identify the effective versions. |
| Parameter set | `definition`, `definition_sha256`; definition contains `effective_parameters` and captured `user_overrides`. |
| Recipe | `definition`, `definition_sha256`; definition contains effective `parameters` and applicable `prompt_sha256`. |
| Preset / combination | `definition`, `definition_sha256`; each ordered combination member identifies exactly one `method_version_id` or nested `combination_version_id`. |
| Prompt | Exact UTF-8 `text` and `text_sha256`. |
| Run | Final trace fields in attrs. `result_bindings` may live in the immutable trace Observation plus real edges; when also present in run attrs, it must match. |

Kinds, predicates, parameter names, combination sizes and weights remain caller
data. Method identity, method version and run have distinct native identities.
Protected version definitions, hashes and prompt bytes are immutable across
the packet's validated history; metadata annotations remain mutable as described
above. A change followed by restoration cannot conceal version identity reuse.

Run from the repository root, using a new evidence directory:

```sh
python3 loom/src/packet/tests/verify_method_graph_artifact.py \
  --library loom/build/dev/libloom.so.0.1.0 \
  --artifact /path/to/w3-golden-artifact.json \
  --evidence-dir /path/to/new-w3-w4-evidence
```

The verifier uses the existing offline validator dependencies in
`loom/tools/contracts/requirements.txt`. It checks schema/date formats, exact
canonical hashes and UTF-8 prompt hashes, applicable binding equality, actual
native Claims, source captures, nested membership and result model/compiler
provenance. Prepared and final traces retain fixed requested identities/settings;
observed model fields may change only as witnessed by native results, alongside
measured/projection phase fields. It then calls the real C ABI store directly, with
closed selection and expected-row CAS, restarts it, reads/replays the entire
receipt and verifies an identical acceptance retry. It makes no provider call.

`input.json` retains the exact supplied bytes. Success records `receipt.json`
and `verification.json`; failure records `error.json`. A nonempty evidence
directory is refused before writing, preserving earlier inputs and receipts.
The verification result always says `producer_execution_verified:false`:
`verifier_provider_calls:0` measures this consumer only, not the producer.
Declaring the complete output set is a producer responsibility. W3's joint test
must independently compare registry definitions/settings with the invocation,
actual execution/transport instrumentation and emitted result set before invoking
this consumer. W9 must inspect that producer proof and this native receipt before
closing the joint gate. No acceptance receipt establishes content truth.

## Pack and producer ownership

Builtin method definitions are pack data. W3 reads definitions and user
combinations from the graph and produces the run/result edges; W1 supplies
versioned prompt/recipe nodes. W7 records measured experiments and model
profiles as dated Claims using the same native evidence structure. W4 preserves
these ordinary records, references and history. The synthetic fixture installs
no production defaults and does not replace a pack. Pack/profile data and the
registry adapter stay with their assigned lanes. W9's joint review uses the
canonical artifact named above, its actual W3 execution proof and W4's fresh
native persistence receipt.
