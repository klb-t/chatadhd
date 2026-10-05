# Actual programme normalization, pure scoring and graph export

Run offline after a pause, stop or stage completion, using new output paths:

```bash
python -m loom.tools.structure.programme_study_results_v1 \
  --manifest /path/to/unchanged/stage1-prepared/manifest.json \
  --ledger-directory /path/to/private/run-ledger \
  --prepared docs/research/model_research_2026-10-04/study/prepared \
  --output docs/research/model_research_2026-10-04/programme-actual/stage1-new-snapshot \
  --observed-on YYYY-MM-DD
```

The generic [`programme_results_v1.py`](../../../loom/tools/structure/programme_results_v1.py) snapshots selected SQLite attempt fields and only known operation request/response/generation captures. It never queries the key, account binding, fingerprint table, key metadata, authentication headers or the full private proof payload. Public response projections retain synthetic model output and selected model/resource fields, drop other provider metadata, preserve the exact original capture hash, and declare projection loss. Original private bytes are never repaired or overwritten. If an exact sent-body capture is available, its bytes must equal the unchanged programme request artifact; its absence remains explicitly recorded.

An append-only `attempt_resolutions` projection may update the effective report state after a read-only late-generation proof. The normalizer reads only named projection scalars and the final late-generation filename/hash/HTTP status. It restricts that filename to the generation-capture pattern, checks its hash and 200/no-transport-error state, keeps the original 404 capture/hash and original attempt state, and requires the original response hash to remain unchanged. It independently replays the existing transport billing verifier against the first response and selected generation proof. Unknown billing remains null. Duplicate generation identities cannot double-count a billed attempt.

[`programme_study_results_v1.py`](../../../loom/tools/structure/programme_study_results_v1.py) maps operation metadata to the exact original arm/query/body and validates the entire frozen request grid, including unattempted requests. It calls the existing pure judgment compilers and `programme_study_results_v1.score_predictions`, an aggregation derived from the frozen study score function and using its unchanged pure helpers; it does not use the legacy admission/ledger wrappers, rewrite the old USD 2 headers, substitute a historical key limit or invent alias admission. Historical source drift is reported separately while frozen manifest/request bytes remain unchanged.

Study denominators are distinct: **432 planned HTTP operations** across nine execution arms, and **384 planned semantic decisions** across eight scored arms after composing the two split-question arms. Each scored arm retains all 48 independent authored DEV query slots. Missing, invalid or conflicting outputs stay unavailable; the existing strict greater-than-0.5 Jev threshold remains unchanged. A saved answer can be semantically scored while its billing is unresolved; its cost remains null. Source-commitment accuracy does not establish world truth or held-out quality.

The graph uses the existing generic `method_graph_export_v1` and shared ModelProfile metric fields without inventing profiles. It keeps requested aliases separate from observed model/provider IDs and their verification availability. Exact recipe/prompt hashes, effective generation/provider settings, parameter versions, dated metric Claims, source Observations and actual `produced_by` edges are retained. Every saved attempt gets an event, including an uncertain first attempt. Unattempted configuration populations remain explicit with observed model null. Combined split scoring and all language/family slices remain in the exact `SCORE.json` source Observation; individual split components have no standalone semantic accuracy. Costs, tokens, latency and transport states retain their available-attempt cohorts and unknown counts.

For other studies, call `normalize(manifest_path, ledger_directory)` and `export_results(bundle, output, observed_on=..., evaluator=...)`. The evaluator receives the exact planned configuration group and its saved rows and returns shared-shaped metrics. This generic path has no study/stage dispatcher or closed model preset list. Existing Stage 2/3/4 scorers or separate documented semantic reviews can be injected; the module does not duplicate their compilers or criteria.

Validation uses the existing native DTO JSON schema/Python packet codec. No native CABI execution, canonical store acceptance, paid call, retry or automatic model-profile promotion occurs here. Each snapshot is immutable; preserve earlier partial outputs separately when publishing a later final population artifact.
