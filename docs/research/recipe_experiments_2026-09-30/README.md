# Recipe experiments: implemented without new paid synthetic inference

The initial checkpoint followed the synthetic-inference deferral recovered
from Claude's development handoff. Root then relayed a newer direct owner
clarification authorizing continued Jev/inexpensive-model experiments within
the existing shared USD2 cap; see `EXECUTION_CLARIFICATION_2026-09-30.md`.
No live model request was made by this workstream. This directory keeps offline
implementations and mechanism evidence; historical actual results are linked
only under their original recipes. Planned new arms remain unmeasured until
root recovers the existing credential and executes them.

| Recipe | Implemented | Model measurement | Offline evidence |
|---|---|---|---|
| Free source-only v1 | Existing frozen compiler | Actual prior 24 first calls, USD0.0263512; 18/24 compiled, strict edges TP6/FP43/FN54 | Original hashes, ledgers and raw first responses preserved in `../graph_free_extraction_v1/` |
| Free schema-hint v2 | One appended literal symbolic JSON grammar, unchanged v1 compiler/scorer | **None; zero live v2 calls** | 11 mechanism checks; 3 scripted outputs exercise a valid edge, duplicate key rejection and string-evidence rejection |
| Role-explicit Jev state v1 | Original source record nested unchanged plus a redundant endpoint-role card; active q01/q02 exact | **None; zero live role-explicit calls** | 10 renderer/integrity checks; 4 scripted outputs exercise support, refutation, unknown and conflict |
| Exact string turn-ID evidence decoder | Separate data-policy projection; original strict compiler unchanged | **No new model call**; same actual v1 raw24, strict edges38TP/11FP/22FN versus6TP/43FP/54FN | 12 mechanism checks, preserved six unavailable, independent manual source/count reconciliation |

`scripted_transport_v1.py` injects both transport and a noncredential sentinel
into the existing bounded runners. No network fallback is called. Copied public
endpoint snapshots remain provenance of identity configuration, while every
response/usage envelope is authored locally. `SCRIPTED_EXECUTION_KIND.json`
and `SCRIPTED_EXECUTION_RECEIPT.json` label fake usage and exclude it from actual
cost/model profiles. Timers measure local mechanism execution, not model latency.

`scripted_verified_evidence/` preserves the successful exclusive-write run.
`scripted_first_evidence/` preserves the first local run, which wrote all seven
scripted response artifacts and scores but stopped when the receipt writer
incorrectly looked for an aggregate cost field on the GPT runner's ledger.
The driver was corrected to sum per-attempt fake usage. No runner, model
adapter, first response or scorer was changed. The first local artifacts were
retained; the second run is a mechanism check, never another model sample.

The scripted confusion/precision/recall numbers verify planned denominators
and compiler behavior against deliberately assigned toy expectations. They
are **not estimates of model quality** and cannot establish that role cards
or grammar examples improve an actual model. Likewise the observed active
SourceView baseline's 45/48 cannot be compared as if any scripted output were
an observed successor recipe.

Reproduce offline from the repository root with a fresh destination:

```sh
PYTHONPATH=. python3 -B docs/research/recipe_experiments_2026-09-30/scripted_transport_v1.py \
  --output /tmp/new-scripted-recipe-check
python3 -B -m unittest loom.tools.structure.test_source_view_roles_v1 \
  loom.tools.structure.test_graph_free_schema_hint_v2 \
  loom.tools.structure.test_recipe_scripted_transport -v
```

The protocol/freeze in `role_explicit_v1/` retains the 48-query planning inputs;
`../free_schema_hint_v2/EXECUTION_STATUS_2026-09-30.md` explains the separate
unexecuted v2 freeze. Product configuration remains open; these fixed research
conditions are presets for a controlled experiment, not user restrictions.
