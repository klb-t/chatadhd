# Onboarding and graph defaults (W12)

The public C++ entry point is `loom::onboarding::OnboardingStore` in
`loom/onboarding_store.h`. It uses the existing `Database` and `KnowledgeStore`.
There are no network calls, provider credentials, C ABI additions or HTTP routes
in this increment. W10 owns navigation and transport; W3 owns production
selection/writing and method execution.

## Data and lifecycle

`loom/data/onboarding/scenario.pack` holds the interview questions, ordered
sections, prompt, reply shape, categories and method descriptor.
`loom/data/profiles/user.pack` holds defaults, open graph vocabulary, method
parameter precedence, scenario bindings and the RuntimeProfile descriptor.
Regenerate `builtin.inc` with `python3 loom/src/onboarding/gen_onboarding_pack.py`;
`--check` detects drift. `.pack` intentionally does not enter the separate KB
JSON manifest. Existing `profiles/self.json` is read through `kb::Pack` and
seeded as an application probe definition, never as a user's projects.

`open(user, legacy)` initializes a durable profile and graph immediately. All
personal fields begin `unknown`; application defaults remain separate graph
nodes. The optional legacy snapshot preserves existing fields/extensions,
privacy/settings and known communication preferences. `read(user)` returns
the native snapshot and derived inspection views. Every write uses the outer
`revision` as an optimistic concurrency token; the inner profile revision is
distinct. Do not substitute one for the other.

`apply(user, expected_revision, action)` accepts `target:profile` (default),
`target:layers`, or `target:model_reply`. Profile events need `id` and `time`;
`source_refs` carries real source identifiers. A review event has its own `id`
and a separate `candidate` ID. Form answers use `provenance:form`; interview
answers use `user_stated`; model propositions use `model_inferred`. Confirmation
does not rewrite a proposition's acquisition origin.

Profile operations are `answer`, `propose`, `review`, `status`, `delete`,
`settings`, `privacy`, `pause`, `resume`, `skip`, `repeat`, `confirm_section`.
`unknown`, `declined` and `never` are explicit states. A retained inferred value
can keep `question_disposition:declined` independently of `status:known`.
`never` requires the user's explicit reopening before new information is added.

## One resolver and policy authority

`DefaultLayers` resolves arbitrary keys, including preferences, methods,
prompts, privacy settings and application profiles. It supports `override`,
`clear_override`, `disable`, `exclude`, `reenable`, `accept_proposal` and
`set_area_mode`. Permanent ID exclusions survive disappearance/reappearance and
pack upgrades. New identities in an excluded area follow the chosen
`proposal`/`direct` setting; direct does not resurrect an excluded identity.
`resolve(key)` explains effective layer, identity, source and suppression.

The RuntimeProfile adapter delegates descriptor/value validation to W11's real
`from_definition`/`with_values`. No duplicate schema interpreter exists here.
Current main has not integrated W11; runtime inspection therefore reports
`available:false`, while native onboarding's operational contract remains
usable. The real W11 branch was separately linked and tested. Disabled required
operation settings remain inspectable and reversible; the affected operation
reports unavailable instead of restoring a preset. Invalid settings cannot
corrupt an operative profile.

Use **`OnboardingStore::policy_decision(user, request)`** for W3 selectors and
writers. Request: `{op:ask|store|send|infer,category,field?,provider?,detail?,
sensitivity?,provenance?}`. Response: `allowed`, reason, selected rule/retention,
effective layer resolution and `snapshot_revision` when operative. Missing,
disabled, excluded or unaccepted privacy policy denies use. Retained raw
`profile.privacy` is inspection, not authorization; native privacy nodes expose
`applicable` and `layer_status`. The standalone free `privacy_decision` assumes
its caller has already composed an active policy. Check the decision before
selecting context, storing facts or sending content, then verify its revision at
the actual operation boundary to avoid using an obsolete permission.

Rules govern category storage, detail/sensitivity, provider destinations,
inference and explicit-only acquisition. A builtin permission preset is labelled
`builtin_preset` with no consent event. User choices remain authoritative.
Retention currently covers full/metadata/no history, configurable metadata keys
and nullable event counts. Deletion with `purge_history:true` removes historical
copies; deletion always clears unstructured cached summaries and current derived
graph values. Time-based expiry is not implemented in this increment.

## Conversation and imported seeds

`model_request(user, provider)` prepares a filtered request and exact native
method/version/prompt/recipe bindings. It does not authorize a provider call.
W10's adapter passes only prompt, section, allowed questions/context/candidates,
policy and reply schema to its existing model adapter. The request token, outer
revision and graph metadata stay local. W2's resource preflight and expected
×10 confirmation remain the host's execution authority.

The host overlays the original provider and request token onto the received
reply; the model cannot choose these. Apply with `target:model_reply` and the
prepared outer revision. State, privacy or scenario changes reject the reply.
Native ingestion validates the whole response before committing anything and
requires individual candidate and section confirmation. Persisted run metadata
records canonical received-reply hashing; raw provider-byte hash and resource
measurements remain explicitly unavailable unless another instrument captured
them. This is provenance of received output, not verified content or quality.

Optional imported-archive seeding uses the same `propose` action with
`model_inferred`, real archive `source_refs`, classification and explicit review.
It never changes imported observations. Existing extraction methods run in W3/5;
this module does not implement another extractor or initiate bulk import. A host
with an actual method execution can supply its exact native definition/run
receipt as `method_execution` on the `propose` action. The store saves it in
`profile.method_executions` and binds `result_candidate_ids` to the proposed
candidate; the large receipt is not duplicated inside the candidate. Projection validates definitions and
links surviving results to the concrete method version/run. Never invent a run
for a method merely installed from the pack.

## Persistence and integration

All mutable profile/layer state lives in `loom_onboarding_*` tables. Schema
guards run on every read/write and reject newer schemas. A stable per-user
knowledge run is replaced within the same outer transaction, using native
Entity/Claim/Observation codecs and nested KnowledgeStore savepoints. Existing
conversation bytes and unrelated knowledge runs are untouched. No immutable
GraphPacket receipt retains private deleted snapshots. Source observations from
imports remain preserved in their own runs.

W10 consumes `loom/web/src/onboarding/` components, native snapshot normalizer
and action envelopes. Its README describes navigation, native bridges and the
model callbacks. Expert scenario editing calls `update_pack(user, revision,
pack, scenario)`; method hashes cover the effective prompt, questions, fields,
categories and recipe through data-defined copy bindings. No arbitrary limit
on methods, providers or user-defined keys is introduced.

### Classification caps (2026-10-09)

A request referring to a saved field cannot omit or understate that field's
`detail` or `sensitivity` to bypass a selected category cap. Both the saved
classification and any explicit request classification must satisfy the cap.
Without a field, a capped dimension requires explicit classification; missing
classification returns `InvalidArgument`, rather than authorization. Uncapped
rules keep their existing behavior. `policy_decision` still returns the effective
R40 resolution/revision; `model_request` uses the same check before including
profile values. This does not extend policy coverage to ordinary chat transport.

### Runtime analyzer consumer snapshot (2026-10-09)

The existing `semantic_analyzer` RuntimeProfile is also consumed at Runtime
startup and before each live graph operation / complete worker drain. A worker
drain pins one analyzer even if the source overlay changes during ingestion;
the next drain reads the newer data. Lexical supplementation and SemanticLLM
fallback receive that same object. An invalid overlay fails before transport or
marking queued messages done, so repair and resume can process the pending input.
Changed profiles retain `analyzer_profile_hash` in message metadata across
storage/reopen. Built-in result JSON remains unchanged. The legacy standalone
`SemanticLLM::analyse(text)` keeps its constructor analyzer; callers which pin an
operation use the explicit analyzer overload. This wiring does not change legacy
prompt truncation, provider budgets or admission of inferred relations.
