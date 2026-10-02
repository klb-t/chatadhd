# Graph-native conversational replies — DEV protocol, 2026-10-02

This experiment tests a reply that is already a graph: one response root, recursively ordered logical parts in the same node format, and optional links to earlier context entities or other new parts. Each parent's text must equal the exact concatenation of its children. The host computes UTF-8 spans; the model does not guess offsets. Logical roles and predicates are open labels. A valid structural link remains a model proposal, not an accepted canonical Claim.

The experiment operates on an existing `loom.graph_packet/1` projection. The host validates the reply and compiles an append-only projected diff. It does not write the native KnowledgeStore, replace ChatADHD's ordinary chat, or infer user approval from prose.

## Frozen DEV cases and comparison

`cases.json` contains eight authored synthetic conversations, current queries and valid native GraphPackets. It contains no expected answers. `expectations.json` separately freezes three semantic criteria per case before the first pilot samples. Generation workers receive only prompt-facing cases. Do not read `eval/real-holdout-key`.

| Case | Failure under examination |
|---|---|
| `correction_a_b` | An explicit replacement must not erase the historical decision or preserve it as current. |
| `branch_specific_choice` | A later mobile choice must not overwrite the independent desktop choice. |
| `context_not_requirement` | Context supplied for adaptation must not become a binding specification. |
| `negated_conditional` | Unknown validation must not turn a one-way prohibition into permission or its converse. |
| `quotation_not_endorsement` | Quoting a supplier must not endorse the quoted proposal. |
| `unresolved_conflict` | Two reported limits must remain unresolved rather than be selected or averaged. |
| `repeated_utf8_emoji` | Repeated identical text must retain occurrence identity and exact Unicode bytes. |
| `no_forced_semantic_relation` | A courtesy response need not acquire invented semantic links. |

The input comparison is **graph context versus flat context, both producing the same graph reply schema**. Its purpose is to isolate context presentation. Flat input must preserve all context text, source identifiers, branch/status/bindingness annotations and the same task; it must not silently lose facts available to the graph arm. This is not a graph-output versus prose-output experiment. Both arms use the same system instructions and current query, apart from context serialization. Save their exact messages, schema, request hashes and byte sizes.

The cases deliberately include authored annotations in entity attributes. This measures behavior under a supplied context projection; it does not test automatic extraction of those annotations from raw conversations. All conversations are synthetic DEV data. They are not real user history and are not held-out data.

## First samples and evidence levels

1. Freeze cases, expectations, prompt contract and sampling plan before generation. Workers should not receive gold. Save the exact first raw response for every case/arm before parsing or repair.
2. For the in-session pilot, preserve the worker's declared model/environment identity, where available. A generic inherited in-session agent is not a named external API model. Record the actual sampling mechanism. No API credential discovery, paid requests or live provider calls are part of this instrument.
3. Count parse or structural failures as first-attempt failures. Repairs, retries and protocol amendments are separate records, never substituted into first-sample denominators. Never pretty-print raw JSON and then claim the reconstructed bytes are the raw response.
4. Synthetic scripted replies exercise codec and storage mechanics only. Fresh model pilot replies exercise prompting on these eight DEV cases. Neither establishes performance on independent real chats, calibration or general model superiority.
5. Preserve mechanically rejected raw output as well as accepted output. Keep every attempted case, declared omission and unknown outcome in the ledger.

The sample envelope is `loom.graph_reply_samples/1`, with a `samples` array. Each entry contains `case_id`, `context_mode` (`graph` or `flat`), `attempt` (positive integer), `raw_response` (exact text), `sample_origin` (`in-session-model-pilot`, `synthetic-fixture`, or another explicit origin), `model_identity` (declared string or null), and `request_sha256` (hash or null). The scorer selects attempt 1 only and reports later attempts separately. It rejects duplicate first samples. Missing cases remain missing, rather than disappearing from the denominator.

## Mechanical scoring

The codec is authoritative for exact JSON keys, duplicate-key rejection, base snapshot hash, one response root, unique local IDs, acyclic single-parent coverage, exact child concatenation, and target namespace/existence. The compiler must preserve raw response bytes, use deterministic local-to-native identifiers, locate repeated substrings by ordered traversal, retain model provenance, and produce additions rather than edits/deletions to earlier records.

For every first sample the scorer reports parse/contract validity, compilation/apply status, rendered response, node/link counts, link targets, maximum nesting depth, source text size and output size. It separately checks that every original native record and its provenance remains unchanged, its order is retained, task data is unchanged, and history is extended. Mechanical success means only that the representation and append-only projection work.

The full-snapshot comparison is a deterministic transport/preservation control: compare bytes required to emit the entire updated GraphPacket with bytes for the compiled projected diff and the compact reply. A copy of the old packet with an edited or omitted old record is detected by old-record hash comparison. This control does not use a third model-generation arm and cannot measure a model's probability of corrupting a full regenerated snapshot. A GraphPacket diff carries the same rich native record/provenance information; it can be larger than a tiny context snapshot. Report observed sizes without promising a universal token saving.

## Semantic adjudication

No substring, predicate whitelist or valid target ID is treated as proof of semantic correctness. Each case has three inspectable boolean criteria in the separate expectations file. An adjudicator records criterion ID, `passed` (`true`, `false` or `null`), and rationale against the saved response text, segmentation and links. `null` means unassessed or unresolved, not success. Criteria requiring links examine what the proposed relation actually asserts and which occurrence/source it uses; mere membership in an expected target set is insufficient.

The scorer accepts `loom.graph_reply_adjudications/1` with an `adjudications` array keyed by `case_id` and `context_mode`, an explicit `adjudicator` identity, and `criteria` entries containing `criterion_id`, `passed`, and `rationale`. Report per-case and per-arm numerators/denominators, with missing/unknown judgments explicit. Content, logical segmentation and reference meaning must remain distinct from mechanical validity. An in-session independent adjudicator avoids gold leakage to generators but is still part of the same experimental environment; it is not a fully independent external evaluation.

## Reproduction and limits

The scorer is `loom/tools/structure/graph_reply_v1/experiment.py`; run `python3 -m loom.tools.structure.graph_reply_v1.experiment` from the repository root, or execute the file directly. `--cases`, `--expectations`, `--responses`, optional `--adjudications`, optional `--requests` for exact reported request-hash binding, and `--output` choose exact input artifacts. Output creation is exclusive, so reruns need a new filename. `--prepare --model caller-selected-model --output requests.json` creates both context arms without loading expectations. The programmatic APIs are `score_samples`, `prepare_requests`, and `compare_payloads`. It performs no network calls and no canonical store writes.

Report JSON byte counts as JSON byte counts. They are not measured tokens, monetary cost or latency. With one first sample per case/arm, eight cases, synthetic annotation and no external holdout, any graph/flat difference is exploratory and paired DEV evidence only.

## Separate V2 representation follow-up

The first pilot and its scoring remain V1. After saving the first V1 receipts, a separately versioned `loom.graph_reply/2` encoding can eliminate duplicate parent text: composite nodes carry `text: null`, leaves carry exact text, and the host renders composites by ordered recursive concatenation. A leaf-only root still contains its text. V2 is optional; it does not silently change V1's contract.

`compare_composed_payloads(cases_document, samples_document)` transforms saved V1 responses deterministically into V2 candidates, validates/compiles/applies them, and checks exact rendered text, span identity, unchanged links and old packet preservation. It records the converted raw candidate, source first-capture hash, normalized V1/V2 JSON byte sizes, and duplicated composite text bytes. Comparing normalized encodings isolates the representation change from raw whitespace. These are **not fresh V2 model samples** and do not establish that a model can comply with the V2 prompt. Preserve V1 raw samples and first scores unchanged. Any subsequent fresh V2 prompting needs a separately labelled first-sample ledger.
