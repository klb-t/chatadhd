# Native candidate boundary review — 2026-09-28

This is a read-only peer review of the native occurrence-graph validator and the optional semantic extraction adapter. It covers source and request boundaries, not the semantic correctness of model interpretations. The reviewer did not inspect independent evaluation fixtures or their outcomes, modify implementation files, build native targets, or call a model. Native execution and compatibility gates belong to the integration owner.

## Reviewed path

`Pack::policy("candidate_graph")` supplies the experimental five-operation vocabulary. The pure `validate_candidate_graph_vocabulary` function is shared by pack loading and extraction. The `occurrence_graph_v1` adapter prepares native Observation/Entity/Claim JSON, requests a schema-2 response, validates each bundle through `validate_candidate_graph_bundle`, and queues accepted proposals as `semantic_structure` candidates. The default remains `relation_v1`.

The review included `knowledge_candidate_graph.h`, `extract/candidate_graph.cpp`, `knowledge_semantic.h`, `extract/semantic.cpp`, the corresponding author-owned native tests, the root-owned catalog transport tests, and the shared response parser used by the adapter. The accompanying runtime policy and pack registration were also checked. The earlier Python A/B/C review is recorded separately in `CANDIDATE_BOUNDARY_REVIEW_2026-09-28.md`.

## Boundaries confirmed by source inspection

| Boundary | Behavior found in the implementation | Limit of the conclusion |
| --- | --- | --- |
| Packet identity | The adapter computes a noncircular snapshot identifier and a hash of the full packet. Schema-2 responses must echo the full packet hash before any bundle is accepted. | The pure validator's `snapshot_id` is a supplied label; it does not itself attest which request produced a response. |
| Original records | Graph mode uses native `to_json()` for selected Observations, Entities, and Claims, including full selected Claim Assessments and the Entity's own evidence/origin/confidence/status fields. Graph requests do not require preexisting Entities. | Native Entity has no Assessment object; its arbitrary attrs may retain external metadata. Context selection is bounded; omitted records are not evidence of absence. |
| Grounding | Support must reference a packet Observation, use an in-bounds nonempty UTF-8 byte span, end at character boundaries, and exactly equal the source substring. | A matching quote does not establish that its proposed operation or referent is correct. |
| Typed references | Local handles are unique across draft kinds; existing Entity and Claim IDs cannot collide. Subjects are local occurrences, edge objects are eligible Entity references, and Assessment premises use supplied Claim IDs. | External references already present inside original Claim Assessments or Entity attrs remain original context rather than newly resolved facts. |
| Scope and binding | Scope membership and qualifier scope must agree. Quantifiers own distinct child scopes and binders. Variables must name the nearest accessible same-symbol binder; escape and shadowing violations reject. | Formal well-formedness does not prove that the model read the source scope correctly. |
| Composition | Ports, targets, counts, and ordinals follow the supplied validated vocabulary. Expression roots cover all expressions; cycles, orphan terms/scopes, and excess depth reject. Shared DAG height is memoized. | This is a restricted five-operation representation, not a universal thought ontology. |
| Unsupported coverage | Coverage and unknown rows remain source-located. The report computes exact uncovered source spans and keeps `semantic_accuracy: null`. | `complete_declared` describes the bundle's coverage declaration, not measured semantic recall. |
| Partial Assessments | Draft Assessments contain only supported source spans and allowlisted prior Claim premises. No Entity/Claim parser supplies native confidence, origin, evidence class, or status defaults. | Accepted drafts are still unreviewed interpretations. |
| Promotion | Persistence inserts into the candidate queue with pending review, unvalidated logical semantics, and `promoted: false`; it does not write proposed draft Entities or Claims into canonical tables. | Downstream consumers must continue respecting the candidate status. |
| Request budgets | Byte, count, request, timeout, and output-token limits are explicit. Full packet validation precedes a paid attempt. Attempted prompt hashes and spent bytes are checkpointed before HTTP. | A failed uncached request needs an explicit new attempt; resume does not silently retry it. |
| Cache and resume | Cache identity includes exact input, model/provider, prompt, representation/schema, vocabulary, validator version, and response budgets. Cached responses are revalidated; duplicate candidates and graph counts remain stable. | A cached response does not retain a provider finish reason; the report records that field as unknown. |
| Truncation | Provider `length` and `max_tokens` finish reasons reject before caching or candidate persistence, even if the returned text contains valid JSON. | A missing finish reason is recorded as unknown, not asserted to be untruncated. |

## Review findings and owner fixes

1. **Reduced packet policy was checked after a request.** A legal policy overlay could lower `max_packet_bytes` or a source count below the adapter's own transport limits. The request was then guaranteed to fail only after consuming its budget. The adapter owner added an empty-draft preflight against the full source packet and the shared vocabulary before cache selection or HTTP. A source packet that fails preflight is reported with `source_packet_validation` and explicit omitted Observations. The author regression lowers the policy packet byte limit and expects zero transport calls and zero paid attempts.

2. **Rejected deep inputs were copied into a report.** The pure validator initially performed a hard nesting preflight, but its error path subsequently copied the rejected tree into `retained_input`. The validator owner now performs hard bundle and packet preflights before policy validation and sets `safe_to_copy` only after both pass. A preflight rejection returns `retained_input: null` with an explicit caller-ownership retention explanation. Ordinary bounded invalid inputs remain retained. The author regression uses a 130-level nested metadata value.

3. **The response parser copied before bundle preflight.** The shared `SemanticLLM::parse_response_json` helper returns `*parsed`, which copies the parsed JSON tree before the native bundle validator can enforce nesting depth. A transport byte limit permits a deeply nested response. The adapter owner added a byte-level nesting guard before both outer HTTP JSON parsing and model-content parsing, including cached content. The scanner ignores brackets inside quoted strings and respects escaping. The author regression rejects 130-level nesting in both locations, then accepts source text containing 150 literal opening brackets and escaped characters. The reviewer re-read these changed paths.

4. **Context locality needs a precise statement.** Claim selection requires every source-support Observation to be in the current chunk. Entity selection admits an endpoint or any Entity with an overlapping `attrs.observations` reference, then retains the whole Entity record. Entity attrs and Claim Assessment counter/dependency/alternative metadata can reference external records, even though the initial packet metadata described one `same_chunk_source_support_only` policy. The reviewer's initial message incorrectly called the Entity's metadata an Assessment; the integration owner corrected this against `model.h`, and this review uses the actual model distinction. The adapter now names the two selection rules separately and reports known external Observation, premise, counter, consequence, and alternative-Entity references with counts. It explicitly marks retained metadata as opaque context whose chronology is unverified and external references as not Observation evidence. Original records are retained. Arbitrary attrs remain opaque; this inventory does not claim to find every possible embedded reference. Draft support still must resolve to a supplied Observation.

## Verification and remaining scope

The reviewer read the author regressions for source-byte mismatch, Unicode boundaries, missing Assessments, namespace/type errors, quantifier ownership, binding capture, shared DAGs, cycles, unsupported text, reduced policy limits, packet substitution, missing extracted Entities, whole Assessments, truncation, cache/resume, cancellation, and the actual catalog/extract transport path. These examples are author-owned and are not independent semantic accuracy evidence.

The reviewed author files declare 10 validator cases, 11 adapter cases, and 2 catalog-flow cases. No native pass count is claimed here before the integration owner's build finishes. `git diff --check` passed at the review snapshot. The code review does not establish multilingual extraction quality, broad thought-operation coverage, independent domain generalization, or truth of any candidate. The native packet is a bounded source chunk, not the temporal replay contract implemented by research `context_delta.py`; topic identity and chronology inside supplied context are not proved by this validator.

## Final review snapshot

All four findings above have a source-reviewed disposition. No further concrete blocker was found within this review's scope. Native runtime and compatibility validation remain integration gates, separate from this review.

| File | SHA-256 |
| --- | --- |
| `loom/include/loom/knowledge_candidate_graph.h` | `fca23892a95893e02b30fb40b4973f587280b8001ea946e7d02bb53f48b7f579` |
| `loom/src/extract/candidate_graph.cpp` | `5beca05fef288acbdd24afa8897a8408fe07288a47d622e6772180b167626f2e` |
| `loom/include/loom/knowledge_semantic.h` | `9a6dbdb80a3e59a60826a7c2e43e5290c360e9f6882699e666f8064159da0c77` |
| `loom/src/extract/semantic.cpp` | `385aadb8905eac8c611d0743bbb6aa03cf54c4387ea5d03640eba44551908313` |
| `loom/tests/test_knowledge_candidate_graph.cpp` | `6fccaf84963fabec2b589e26af2c794ea9b04fdb84a666404dfadf004b0f17cf` |
| `loom/tests/test_knowledge_semantic_graph.cpp` | `18aa7081e030fcb96db96694335f459b9810713cde9210fcd6423a4d0d178e75` |
| `loom/tests/test_knowledge_flow.cpp` | `0ee615c4d818410ba8e060ccc73b3bae0dab00815d109195727842dde5169bb8` |
| `loom/data/policy/candidate_graph.json` | `8f1fc07eeea427d71ef4f05e646709b23ed744141ff61f4014781ea1f614e736` |
