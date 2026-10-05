# Source-bound native receipts

`loom/tools/structure/programme_native_results_v1.py` is an offline adapter for
the public `loom.programme_results/1` projection. It takes the original prepared
inventory, original source inputs, candidate vocabulary and the caller-owned
`native-results-policy.json`. The prepared inventory supplies the denominator;
missing replies remain explicit slots. No model, account or private-capture reads
occur. This adapter adds no semantic criterion or quality threshold to the
existing stage2 reference and review rubric.

The adapter binds each operation to its prepared ID, method, exact canonical
request hash, embedded source packet and declared packet hash. It retains every
supplied choice, its exact content string and UTF-8 hash, and every bundle in
order, including duplicates and failures. It accepts the declared outer envelope
only as supplied: integer `schema_version: 2`, the bound `packet_hash`, and
`bundles`, with no additional fields. It never strips fences, repairs JSON,
chooses a preferred alternative or enriches a candidate. An empty envelope is
an explicit abstention and makes no native validation claim. A mixture of valid
and empty envelopes is `partial_abstention`, with aggregate validity unknown.

Each alternative is passed unchanged to the existing native executable using
its stdin API:

```json
{"packet": {}, "bundle": {}, "vocabulary": {}}
```

The existing C++ validator decides candidate structure, local references, exact
source support and UTF-8 span validity. The Python adapter checks JSON, hashes,
retained inputs, native report identity and declared source counts. It does not
replace that validator or compute semantic accuracy. It preserves native stdout
and stderr as exact hex plus SHA-256; UTF-8 display strings are supplementary.
Native exit failure, timeout, malformed report or contradictory hash fails
closed. The binary and optional provenance files are hashed before and after
each invocation. A supplied `NativeValidator` must match the effective caller
policy. Canonical-number disagreements between Python and the declared native
hash format fail closed; general JSON numeric equivalence is not claimed.

An injected callback requires an identity and is `callback_test_only` evidence.
It may exercise report-contract checks, but its native execution counts are zero
and its `native_contract_valid` and manual native eligibility stay unknown.
`validator_alternative_attempts` counts all attempted alternatives;
`native_bundle_invocations` counts captured actual native executions.
`native_verified_valid` requires actual execution and a valid bound report.

The adapter preserves the response hash supplied by the normalized bundle and
checks its row binding. It does not reread the private HTTP capture or certify
the normalizer's projection against those bytes. `response_projection` preserves
the supplied public projection; fields omitted upstream cannot be recovered.
The supplied transport/billing flags, native receipt and manual interpretation
remain separate evidence. A structurally valid native bundle can be incomplete,
unrepresented or semantically wrong. Coverage statuses may overlap; their byte
counts are not treated as an exclusive semantic score.

The output includes a manual skeleton for every planned request. Reviewer,
judgement and rationale are empty; no verdict is generated. When the existing
reference is supplied, its unchanged `validate_reference` routine checks the
reference inventory, packet binding, source locators and exact criterion spans;
the validator source hash and returned count are recorded. That checks authored
reference consistency, not truth. Every alternative gets an explicit choice,
local bundle and receipt pointer. Multiple choices are fully retained but their
skeleton `native_contract_valid` is unknown, so the unchanged single-envelope
stage2 scorer cannot treat the flattened list as eligible. Supporting that
bridge would require a separately declared review representation. The prepared
stage2 requests use the single-envelope contract.

Example invocation (public inputs and caller-chosen native binary only):

```sh
python loom/tools/structure/programme_native_results_v1.py \
  --normalized PUBLIC_NORMALIZED.json \
  --prepared PREPARED.json --source-inputs SOURCE_INPUTS.json \
  --vocabulary loom/tools/structure/candidate_graph_vocabulary.json \
  --policy docs/research/model_research_2026-10-04/native-results-policy.json \
  --native-validator /absolute/path/loom_candidate_graph_native_tool \
  --expected-binary-sha256 EXPECTED_SHA256 \
  --review-reference SOURCE_ONLY_REFERENCE.json \
  --output NEW_NATIVE_RECEIPTS.json
```

The reference is optional. Source files are loaded once; raw hashes identify the
exact consumed snapshots, while separate canonical hashes identify JSON values.
The complete effective policy, adapter hash, vocabulary hash and native binary
provenance are retained. Output creation is exclusive and never overwrites an
existing receipt. Stdout contains aggregate counts only.

Verification uses fabricated inputs and the existing native binary, never
collected study replies:

```sh
LOOM_CANDIDATE_GRAPH_NATIVE_TOOL=/absolute/path/loom_candidate_graph_native_tool \
  python -m unittest discover -s loom/tools/structure \
  -p test_programme_native_results_v1.py
```

The focused suite has 22 tests, including four tests that invoke the native tool
over invented Unicode packets. Without that environment variable, those four tests
are explicitly skipped; callback checks do not stand in for native evidence.
The normal `research.structure` discovery includes the new suite. Source and
policy are frozen and published before production export or collected-output
inspection.
