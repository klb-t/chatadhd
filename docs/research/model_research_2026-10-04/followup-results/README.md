# Offline first-choice results adapter

`loom/tools/structure/programme_followup_results_v1.py` reads only public normalized output and caller-supplied frozen input artifacts. The first implementation and focused tests were prepared before inspecting actual stage3/stage4 outputs. Tests fabricate every response; they read only the previously frozen public request inventory, configuration and DEV references.

The caller chooses `frontier_comparison` or `graph_reply` in DATA. No stage name dispatches an evaluator. `stage3-policy.json` and `stage4-policy.json` are source-bound presets, with paths, parser envelope, representation adapters and SHA-256 bindings. Each policy pins the exact original manifest/config/reference bytes and all existing Python evaluator dependencies. Graph compilation also checks the immutable Git sources through existing `frontier_reply_followup_v1._sources`. The effective policy, adapter hash and dependency hashes accompany the result. The backend is a Python reference instrument; native C++ execution is explicitly false.

The adapter maps normalized `requests[].metadata.prepared_request_id` to the original prepared row ID and verifies canonical request body bytes/hash, original row hash and source-manifest hash. Duplicate physical operation IDs cannot serve multiple planned slots. The output retains one slot for every original prepared request, including missing captures, plus unmapped requests/responses. Duplicate records and every alternative remain visible. The source normalized envelope binds response hashes to the saved resource rows; it does not reconstruct the private HTTP envelope.

Each string choice is kept byte-for-byte as decoded UTF-8, with SHA-256, byte count and hex. Its original structured choice is also retained. JSON parsing rejects duplicate keys and non-finite values. Fences, prose, malformed JSON, non-string/ambiguous content and schema/compiler failures remain failures of that individual choice; later choices and requests still run. No stripping, repair, retries or selection takes place. A request's mechanical validity requires a nonempty choice inventory and every choice valid. Repeated alternatives are retained and counted, rather than deduplicated. Global inventory drift makes the input-integrity-qualified count zero; the local count remains available for diagnosis.

A JSON string containing a lone surrogate has no valid UTF-8 content bytes. Its original string is retained with an encoding failure and no fabricated UTF-8 hash. `serialize_report(result)` uses reversible ASCII JSON escaping, so this malformed choice cannot prevent valid siblings from being exported. The CLI serializes before opening its new output file.

For frontier analysis, parsed recorded content is passed to the unchanged, source-bound `frontier_comparison_v1.score_response`. Its `reference_agreement` is mechanical agreement with an authored DEV reference and is explicitly not semantic quality. A graph-completion result retains the raw proposal, measured-origin diff and candidate packet. Pattern results retain the existing instrument report without introducing a new rule for unused fields.

For replies, each choice invokes the pinned `compile_reply` against that request's exact base packet and the caller's resource limits. Valid output retains compilation, diff, candidate packet, spans and rendered text. Native graph bytes are supplied directly. A text-plus-JSON projection is available only when the prepared arm declares the matching component and parameters from DATA. The separate public text must exactly equal the compiler's tree rendering, including all whitespace. Field omission and schema projection are reported; the complete original content remains retained. The base packet is checked for mutation.

Billing, completion, HTTP status, cost, latency and observed identity flags remain in separate original resource rows. Mechanical validity does not certify billing, provider routing, model identity or semantic quality. Compiler provenance uses only a literal observed model field; configured-model fallback is forbidden. No method descriptor identity is invented, no winner is selected, and no graph store or graph export is written.

API:

```python
result = evaluate(
    normalized_raw, original_manifest_raw, config_raw, policy,
    references_raw=references_raw,  # required only by the selected preset
)
```

CLI:

```bash
python -m loom.tools.structure.programme_followup_results_v1 \
  --normalized PUBLIC_NORMALIZED.json \
  --source-manifest ORIGINAL_MANIFEST.json \
  --config FROZEN_CONFIG.json \
  --policy CALLER_POLICY.json \
  --output NEW_RESULTS.json
```

Add `--references FROZEN_REFERENCES.json` for a reference-scoring preset. The output must not already exist. Source/config/reference hash drift fails before evaluation. The adapter performs no networking, key access, account lookup, private-ledger read or paid call.

Focused verification uses `python -m unittest loom.tools.structure.test_programme_followup_results_v1 -v`. Integration and full CTest are coordinated by the owner of the research branch before any actual output is evaluated. Independent fake-only review evidence, including negative candidates, is preserved outside the accepted branch until archived by the branch owner.
