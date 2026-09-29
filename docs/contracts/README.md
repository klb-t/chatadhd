# T1 — Five-layer projection contracts (2026-09-29)

[P] Offline reference implementation, not a native schema migration or a new
knowledge store. Requested by [GPT_OFFLINE_TASKS](../GPT_OFFLINE_TASKS_2026-09-29.md)
T1, following [conceptual model §11](../architecture/LOOM_CONCEPTUAL_MODEL.md).
Base: `4508af5a67c5185d578ce81722bcbe9983cb066e`.

## Mapping to existing objects

| Schema | Meaning / native integration seam |
|---|---|
| `history_event.schema.json` | View of retained Sources/Units/Observations and conversation events. Source locator uses existing `model::Locator` keys. |
| `active_task_spec.schema.json` | Derived Product tied to a Goal, branch, task, optional knowledge run, source events and optional Claim ids. Never a quoted user message. |
| `request_snapshot.schema.json` | Auditable, redacted view of a captured, reconstructed or unknown request. Optional existing ContextSet id. |
| `evaluation_packet.schema.json` | Logical classifier input; history, target, ActiveTaskSpec, graph references, ContextSet and snapshot stay distinct. NOT a Jev API body. |
| `common.schema.json` | Shared local definitions. Graph references preserve native RefKind vocabulary; no new graph enum added. |

All schemas use draft 2020-12. `https://loom.invalid/contracts/` identifiers are
in-memory registry names, not endpoints. Unknown schema references fail; no
network resolver is allowed. Strict projection envelopes catch misspellings;
provider-specific fields remain in `provider_metadata`, `extension` events,
explicit `extensions`, and retained original sources. These schemas do not claim
to validate every vendor payload or replace lossless source retention.

[P] The concrete field layout is proposed for Claude's integration review.
`request_provenance` implements conceptual-model §11.2's `provenance` enum without
confusing it with evidence origins. Do not send it as a provider API parameter.
`knowledge_run=null` means no bound knowledge run, not a second store. Multiple
simultaneous tasks have distinct `scope.task_id` and product versions.

## Local use

Dependencies must already be installed (see `loom/tools/contracts/requirements.txt`).
From the repository root:

```sh
python -m unittest discover -s loom/tools/contracts -p 'test_*.py' -v
python loom/tools/contracts/validate.py your_document.json
python loom/tools/contracts/validate.py your_packet.json --for-submission
```

CLI exit codes: 0 valid, 1 rejected, 2 input/load failure. Output diagnostics omit
payload values. Default validation permits an authorization `review`/`deny` packet
for local inspection; submission validation requires explicit `allow`. This does
not verify that the referenced external authorization policy exists or is correct.
No request executor or graph writer is included.

## What is checked

In addition to schema types/enums: ordered per-branch event ordinals, unique ids,
explicit target presence/branch identity, named candidate references, independent
rather than exclusive subgraph relevance, temporal `known_at` boundaries,
source-to-statement maps, supersession references/cycles, and UTF-8 byte spans.
Inactive statements cannot be mapped as active compiled instructions. A complete
history view needs preceding tool calls for tool results; partial views may retain
unresolved call references without fabricating calls.

Snapshots distinguish `recorded`, `reconstructed`, and `unknown` with different
required evidence/nullable fields. The digest covers sorted-key, compact,
UTF-8 Python JSON **after redaction**, using finite numbers. It is not RFC 8785,
not the original transport-byte hash and not a signature. Float spelling must be
matched deliberately before C++ parity is claimed. RFC3339-style timestamps must
have offsets; leap seconds are explicitly unsupported by this reference validator.

## Limits — do not overclaim

Sources are referenced, not dereferenced: raw source hashes, database identities,
quote fidelity, truth of claims, entailment, semantic completeness of consolidation
and actual authorization need additional checks in the native system. A valid
source-map pointer can still point to a semantically wrong interpretation.
The credential-name guard covers named fields in snapshot bodies, **not secrets
hidden in prose, images or arbitrary encodings**. `secrets_removed=true` is an
attestation, not a DLP proof. Classifier data cannot become instructions merely
because this packet carries `source_text_is_data=true`.

[P] Corpus-specific statement kinds in ActiveTaskSpec are projection vocabulary,
not additional native EvidenceClass or Role values. Renderers must preserve
contested/open items and executor-only context; actual enforcement is not supplied
by shape validation. Source occurrences, not duplicated materialized prompts,
remain the epistemic basis.

## Examples and measured checks

`examples/valid.json`: 13 directly readable documents covering all 8 event kinds,
ActiveTaskSpec, all 3 snapshot states, and a two-candidate independent packet.
`examples/invalid.json`: 28 declarative one-edit mutations of named valid examples,
with expected rejection codes. `test_contracts.py::changed` materializes each
invalid document, avoiding repeated large payloads. These are negative examples,
not Jev observations.

Local run: **74/74 unittest methods pass** (13 positive examples, 28 negative
examples, 33 additional checks), Python 3.13.5, jsonschema 4.26.0,
referencing 0.37.0. Includes CLI exit-code tests, prohibited network-access tests,
unknown-reference rejection, UTF-8, redaction and temporal guards.
No CTest/C++/Android run, no model calls, no changed quality gates. Native
integration and confirmation remain Claude's responsibility.
