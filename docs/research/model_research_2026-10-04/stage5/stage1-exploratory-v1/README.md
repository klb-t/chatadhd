# Stage1 exploratory stage5 selection, version 1

This additional DATA protocol was chosen after the completed stage1 scores were inspected. It is an exploratory choice, not a retroactive preregistration. The original default five-criterion protocol is byte-for-byte unchanged at SHA-256 `3810a869ea29a79334e2f889f6c36d980c5d7ec1a4c66d56407977be94fc3742`.

The reduced protocol uses only exact DEV source-commitment label correctness, usable ternary-label availability, and complete provider-reported first-attempt cost. It adds no semantic adequacy, grounding, rationale, mechanism, world-truth or held-out measurement. There are no backfilled aliases, quotas, thresholds or forced winner. Pareto ties and cost/accuracy tradeoffs remain visible.

All eight scored configurations share one exact 48-query group from 12 authored DEV cases. Original input bytes bind the dataset, the canonical sorted query-ID array binds the query set, and original gold bytes bind the source-view label rubric. This is author-independent development material with inspected cases and labels; correlated translations, temporal prefixes and direction queries are not independent trials. The split configuration sums all 96 physical first-attempt charges but retains 48 decision slots. Split components are not standalone candidates.

The unchanged generic selector selects `j_active, j_directed` under this explicit reduced protocol. These are nondominated observed configurations for this exact task and population, not general model winners or independent confirmation. The same summary under the original default protocol selects none: all eight records are missing the separately required semantic-validity and source-grounding dimensions. Their missing counts remain absent.

`derivation-receipt.json` binds the original public SCORE, NORMALIZED, REQUESTS and RESPONSES bytes, original preparation freeze, DEV input/gold, task prompts and scorer sources. The public projections were independently recompiled and all eight report objects match SCORE exactly. All 432 physical charges are retained once, sum to USD 0.052296100, and agree across public normalized, response and generation fields. Costs are provider-reported credit charges, not invoice reconciliation; private originals were not read.

`freeze.json` records exact protocol, summary, selection, default exclusion, receipt and this document hashes plus the selector/source identities. This preparation authors or inspects no new corpus. Selection must be frozen before any new-corpus authoring. Future repeat counts, order/seed, budget and corpus/rubric inventories remain unchosen and must be fixed before execution. No paid or network calls were made.

From the repository root, reproduce the two selections into new temporary files:

```sh
python -m loom.tools.structure.stage5_selection_v1 --summary docs/research/model_research_2026-10-04/stage5/stage1-exploratory-v1/neutral-summary.json --protocol docs/research/model_research_2026-10-04/stage5/stage1-exploratory-v1/protocol.json --output /tmp/stage5-exploratory-replayed.json
python -m loom.tools.structure.stage5_selection_v1 --summary docs/research/model_research_2026-10-04/stage5/stage1-exploratory-v1/neutral-summary.json --protocol docs/research/model_research_2026-10-04/stage5_protocol.json --output /tmp/stage5-default-exclusion-replayed.json
cmp /tmp/stage5-exploratory-replayed.json docs/research/model_research_2026-10-04/stage5/stage1-exploratory-v1/selection.json
cmp /tmp/stage5-default-exclusion-replayed.json docs/research/model_research_2026-10-04/stage5/stage1-exploratory-v1/default-protocol-selection.json
```

The selector validates neutral aggregates and comparison identities; it does not follow report links or independently verify raw scoring or billing. The separate public derivation receipt records this review's source audit boundary. Exact file SHA-256 and the selector's canonical selection-payload SHA-256 are different bindings and are both retained in the freeze.
