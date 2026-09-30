# First-response scoring driver

The primary adapter/scorer and requests were frozen before graph calls. This
new integrity wrapper and its secondary diagnostic are frozen before reading
any graph-panel response content. The root may have started transport already;
no model outcome informed the wrapper. Validation remains sealed.

The wrapper validates GPT manifests with `safe.plan_manifest` and ledgers with
`safe._validate_ledger`; Jev uses its own pinned manifest/ledger validators and
`parse_response`. It rechecks each frozen request against the input-only DEV
recipe, including physically truncated prefixes. Stored response hashes,
HTTP state, completed stop, exact model/provider identity, noul inventory and
billing envelopes must pass before a prediction can be compiled. GPT requires
explicit usage/cost/non-BYOK fields; both instruments' raw reported costs must
equal the corresponding ledger cost. A failed or
missing planned row remains unavailable; it cannot disappear from denominators.

Costs and latency come from the validated attempt ledger. Missing costs remain
explicit, so a partial reported-cost sum is not advertised as complete spend.
First compiled outputs, summaries and scores use exclusive-create writes.
No retry, API request, key access, source-only claim or canonical graph write
is performed. Gold is read only for DEV scoring after existing response evidence
is validated. Each judgment batch reports its planned query subset; combined
96-query reporting requires merging both distinct preserved batches.

Primary v1 metrics are unchanged. A separate diagnostic allows a correction
event to point at the same-turn explicit negative denial of the old relation,
instead of the positively stated alternative target preferred by the gold.
This convention ambiguity must not be misreported as unambiguous model error.
The diagnostic never mutates frozen gold or replaces primary scores.

Narrow quotes still require a later semantic audit: even a unique one-character
quote can satisfy exact-location/source-turn mechanics. The driver preserves
the primary adapter's review counts; it does not claim semantic evidence
adequacy from matching IDs or bytes alone. Source speaker attribution and
content truth remain separate.

Twelve synthetic integrity tests pass. They check response provider/stop/BYOK,
actual ledger validators, tampering, missing rows, request/prefix drift and
primary-gold preservation under the diagnostic. They are not model quality.

Billing integrity is a hard-failure path, separate from semantic compile
failures. All saved raw artifacts are checked for raw/provider-cost agreement
even if refused or truncated. A discrepant ledger cannot produce an apparently
verified low total by converting that discrepancy into an unavailable label.
Provider/ledger disagreement stops scoring before any output is written.

CLI (run separately for each preserved batch):

```
python3 -m loom.tools.structure.graph_panel_score_run \
  path/to/prepared/manifest.json path/to/run \
  --output path/to/new/score_directory
```

GPT track/instrument comes from manifest metadata. Jev is identified by its
pinned manifest schema and scored as supplied-edge judgment. Both remain DEV.
