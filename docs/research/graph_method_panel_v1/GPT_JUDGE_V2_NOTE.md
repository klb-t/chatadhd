# GPT judgment v2 transport-format repair

The root reports the first v1 GPT judgment POST failed HTTP 400 because JSON
mode requires the word JSON in the request instructions. No successful semantic
response informed this repair. Preserve the v1 ledger and all 48 planned query
denominators as unavailable; do not retry or overwrite that body/response.
Retain its USD 0.001366 uncertain reservation in global accounting.

`gpt_judge_v2_recipe.json` changes only `Return exactly {` into
`Return one JSON object exactly {` in the system prompt. The frozen primary
adapter is untouched. Model/provider, ternary policy, source prefixes, endpoints,
attribution, thresholds, gold, budget and all other request fields stay fixed.
The wrapper validates both old and replacement hashes and the exact permitted
replacement; it rejects arbitrary recipe overrides or use on extraction/Jev.

New request specs are in `prepared_v2/gpt_judge_batch01_requests.json` and
`prepared_v2/gpt_judge_batch02_requests.json`. The root gives each new body its
own immutable manifest and ledger under the existing non-resetting cap.
These are new recipe executions after a transport-format failure, not reuse of
the failed v1 body or an opportunity to tune from model-quality outcomes.

Score preserved v2 responses with the same primary classifier/scorer:

```
python3 -m loom.tools.structure.graph_panel_score_run \
  path/to/v2/prepared/manifest.json path/to/v2/run \
  --recipe docs/research/graph_method_panel_v1/gpt_judge_v2_recipe.json \
  --output path/to/new/v2/score_directory
```

Wrapper FREEZE1/2 remain historical records. FREEZE3 includes this narrow
compatibility recipe and wrapper validation before any successful graph-model
source content is read by the fixture author. The transport diagnostic was
known; no claim is made that the entire wrapper predates the first paid POST.
