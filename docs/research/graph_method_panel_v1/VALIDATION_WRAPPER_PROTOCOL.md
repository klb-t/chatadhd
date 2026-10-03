# Released whole-family graph-panel validation wrapper

This protocol adds input packaging and replay integrity only. It does not change
the frozen extraction/judgment prompts, compilers, relation classes, source-event
convention, Noul thresholds or primary metric algebra. The wrapper is
`loom/tools/structure/graph_panel_validation.py`; its synthetic checks are in
`test_graph_panel_validation.py`. Neither has network or key-access behavior.
The coordinator performs calls through the existing bounded runners and the same
nonreset shared USD2 budget; a manifest's `budget_usd:2` never creates a new budget.

The wrapper author authored this fixture and previously knew its contents; this
is not a claim of a blind fixture author. No sealed validation input/gold was read
again while developing this wrapper, and no method was tuned on validation
outcomes. Methods/scorers must be finalized and frozen before coordinator release.
Unit tests use wholly synthetic miniature repositories and one invented PL source
conversation, not validation files or model-quality tests.

## Release before any sealed read

Only the new `graph_methods_panel_v1` manifest is allowed, exact SHA256:
`cf564f652647dd93e3d5f66acd2741eb69d3e39e3070ec69884c8023dc0a0569`.
The fixed fixture paths are `inputs_validation.json` and `gold_validation.json` in
that folder. No path parameter accepts an older holdout or other dataset.
Manifest split/count must indicate24 validation conversations; IDs belong to
this new fixture and cannot be DEV IDs. Fixed manifest byte hashes, not guessed
input content, identify the sealed files before release. Their bytes/hashes are
read only after authorization.

`authorize` returns an internal `VerifiedRelease` ticket. The sealed input/gold
loaders require that exact verified type/private runtime identity and an unchanged
verified value hash; a caller-supplied dict with a `release_sha256` field is not
authorization. Pure packaging/scoring helpers can accept already supplied synthetic
case objects without a ticket because they perform no sealed-file access.

The coordinator creates `RELEASE.json` after all intended methods and this
wrapper are frozen, then independently pins its complete byte hash in
`--release-sha256`. It contains exactly:

```json
{
  "schema": "loom.graph_panel_validation_release/1",
  "issued_by": "/root",
  "authorized": true,
  "released_at": "ROOT_TIMESTAMP",
  "fixture_manifest_sha256": "cf564f652647dd93e3d5f66acd2741eb69d3e39e3070ec69884c8023dc0a0569",
  "methods_sha256": {"REPO_RELATIVE_METHOD_OR_DATA_FILE": "EXACT_SHA256"},
  "inherited_ledger_sha256": {"REPO_RELATIVE_COMPLETED_RUN/ledger.json": "EXACT_SHA256"},
  "global_budget_usd": "2",
  "batch_cap_usd": "0.10",
  "known_reported_spent_usd": "ROOT_NONRESET_ACCOUNTING",
  "retained_unknown_cost_reservation_usd": "AT_LEAST_0.001366",
  "remaining_reservation_allowance_usd": "REMAINING_NONRESET_ALLOWANCE",
  "no_retries": true,
  "session_budget_reset": false,
  "all_intended_methods_frozen": true
}
```

This is an externally pinned **coordinator attestation**, not a cryptographic
proof of actor identity. A self-asserted issuer field alone is insufficient: the
CLI requires the separately supplied root digest. Minimum method pins include
wrapper/tests/protocol, frozen panel/driver/runners, freeze4, GPTV2recipe, exact
DEV manifest templates and public snapshots. Root adds all finalized local,
direct-lookup, source-binding and formal methods, policy/recipe data, manifests
and relevant budget artifacts to this same map. Every pin is repository-relative,
inside the repository, immutable for this release and SHA-verified before sealed
access. Validation files themselves cannot be method pins, which would open their
bytes prematurely. Freeze4's primary dependency hashes are independently enforced
even if a coordinator were to pin new current replacements.

Inherited ledgers are hash-checked. Known reported costs and unknown attempted
request reservations are independently summed as lower bounds of root accounting.
Unknown attempts cannot disappear as zero. At least the retained0.001366 reservation
for the development V1 HTTP400 persists. The released known spent + uncertainty
reserve + remaining allowance must be≤2. Root accounting may include additional
older session costs not repeated in the graph-only report. Final execution remains
subject to the coordinator's existing shared guard, immutable no-retry ledgers and
dedicated key cap; this offline wrapper neither resets nor manages them.

## Inputs-only preparation and query scopes

After release only `inputs_validation.json` is loaded. GPT assisted extraction
receives source turns and node inventory only, with unchanged1536 output tokens.
No gold, judgment query, family, expected label or path annotation enters that body.

GPT and Jev supplied-edge judges receive only `scope:explicit_source` queries.
Frozen `panel.query_payload` physically filters `known_at<=as_of` per query before
serialization; every older query has its own prefix, never shares a body with a
future query. Frozen GPTV2's sole JSON-mode wording fix remains; output tokens64,
temperature0, JSON object mode, model`openai/gpt-4.1-mini`, provider`openai`, caps
prompt0.4/completion1.6 USD/million, usage accounting and no fallback stay identical
to DEV. Jev model/provider/price pins and hard typed dual Noul bodies stay identical
to DEV (`typesafe/jev-1.13`, Typesafe, prompt0.042/completion0).

Formal implication queries are physically excluded from both direct-source paid
judge bodies. Their independent exact-cutoff prefixes/node inventory/query surface
are saved in `formal_query_inventory.json` for the coordinator's separately frozen
formal-path arm. They are never relabeled as direct observed source assertions.
The wrapper exports `explicit_cases`, `expected_gpt`, `expected_jev` and
`score_primary` for generic released-source workflows; local/direct/source-binding
methods consume explicit scopes only, formal methods keep their separate arm.

Batch order is deterministic fixture order, greedily adding unchanged exact rows
with exact frozen reservation estimates until the next row would exceed0.10 USD
or48 judge rows (24 extraction rows). A single row above the cap fails closed;
extraction splits when its exact total crosses the cap. This is packaging based
on pre-outcome input size, not outcome-adaptive model/recipe/query selection.
All three tracks' reservations together must fit the inherited released remaining
allowance before any output folder is created. Generated manifests and
`batch_index.json` are exclusively written and hash-bound to the root release.

Preparation CLI (no API and no gold access):

```sh
python3 -m loom.tools.structure.graph_panel_validation \
  --release docs/research/graph_method_panel_v1/validation/RELEASE.json \
  --release-sha256 ROOT_PINNED_DIGEST \
  prepare --output docs/research/graph_method_panel_v1/validation/prepared
```

Root runs every packaged batch once through existing bounded orchestration, with
inherited ledgers and same global cap. Failed or uncertain paid requests are not
retried under a new manifest/body. No paid operation is implemented here.

## Replay, first-output preservation and primary scores

Replay first verifies the coordinator release again, including all frozen hashes
and inherited accounting. Each supplied manifest must retain validation metadata,
root digest, globalcap2, declared batch cap, instrument and track. Its entire row
inventory must equal one deterministic packaged batch: individually valid rows
cannot authorize a shortened batch that erases missing planned denominators.
Request bytes, metadata, reservations, source prefixes and typed questions must
match exactly reconstructed frozen rows.

Ledger validators, first-response SHA/bounds, raw model/provider and explicit
GPT usage/nonBYOK checks are reused unchanged from freeze4. Jev's frozen parser
rejects explicit `is_byok:true` but permits absent BYOK metadata; that absence is
a retained reporting limitation, not evidence of explicit false. Before any
sealed source/gold load, Jev public identity must match the pinned snapshot's
model and exact `jev.endpoint_identity(snapshot)` alias list. Prepared and replayed
manifests must also carry exactly `safe.digest(snapshot)` as their endpoint hash;
an arbitrary syntactically valid dated alias cannot supply public identity.
Jev raw responses pass
`jev.parse_response(raw, exact_manifest_row, aliases)` before frozen compilation;
query IDs are manifest row IDs. Raw↔ledger billing mismatch remains a fatal
integrity error, not a semantic unavailable output with a falsely low cost.
Transport/shape/identity failures and missing attempts remain unavailable at their
planned IDs. Both Noul channels above0.50 remain conflicting/unavailable; neither
below/equal0.50 becomes unknown. No channel veto, threshold change or forced label
is introduced. Unknown-cost attempts retain their reservation in the summary;
`reported_known_cost_usd` cannot certify a total when unknown attempts remain.

The wrapper exclusively saves `compiled_first.json` and `execution_summary.json`
**before** loading authorized validation gold. Primary scoring then uses the same
frozen `panel.score_extraction`/`panel.score_judgments`; only report `split` metadata
is overridden to validation. Scope-filtered gold has exactly planned explicit
query IDs. Per-class P/R and unavailable denominators remain unchanged. Source
predicate labels never become world facts; content-truth accuracy staysnull and
no canonical graph store is modified. Clause adequacy/source expression typing
retain DEV limitations; mechanical quote matching is not semantic truth.

Replay CLI:

```sh
python3 -m loom.tools.structure.graph_panel_validation \
  --release docs/research/graph_method_panel_v1/validation/RELEASE.json \
  --release-sha256 ROOT_PINNED_DIGEST \
  score EXACT_PACKAGED_MANIFEST RUN_DIRECTORY --output NEW_FIRST_SCORE_DIRECTORY
```

To aggregate batches, root verifies disjoint planned IDs, complete explicit
inventory and their released manifest hashes. It scores all planned IDs, including
unavailable rows, against released gold using the same pure primary function. The
formal path arm is reported separately. Every first attempt/output remains saved;
there is no validation result-driven tuning or reset of development outcomes.

Revision2 preserves the first wrapper freeze and fixes two independently found
integrity defects before any validation release: regex-valid fabricated Jev aliases
with an arbitrary catalog hash, and trusted-dict sealed-loader authorization.
It changes wrapper guards only; primary methods/prompts/metrics remain unchanged.
Twenty synthetic wrapper checks and52 combined primary/integrity checks pass.
