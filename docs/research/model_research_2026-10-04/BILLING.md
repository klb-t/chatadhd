# Offline billing reconciliation — 2026-10-04

The fresh reconstruction confirms the historical arithmetic. **$1.098135722
remains unassigned.** The saved evidence contains neither a complete provider
usage export nor independent proof that the later key metadata belongs to the
same key as the earlier campaign. Assigning that difference to particular
experiments would invent evidence. This lane made **zero provider calls** and
read no credential, private export or sealed evaluation content. The owner's
latest instruction requires fresh consent before paid continuation.

## What is reproduced

[reconciliation.json](billing/reconciliation.json) recomputes the amounts from
the 36 pinned nonsealed historical research snapshots. A separate instrument,
[billing_reconciliation_v1.py](../../../loom/tools/structure/billing_reconciliation_v1.py),
checks the frozen auditor's counters against per-attempt records. With `--repo`,
it rechecks actual public Git ledger hashes and response bytes, recovers
generation IDs and retains safe monetary checkpoint fields. It does not change
the frozen auditor or runner.

| Observation | Fresh result | Interpretation |
|---|---:|---|
| Saved ledger rows / unique attempts | 992 / 631 | 361 copied rows are counted once; repeated real requests at distinct times remain separate. |
| Verified response hashes / cost comparisons | 629 / 628 | Zero raw-cost mismatches; three costs are unknown. |
| Distinct generation IDs recovered | 628 | Enables exact offline joins; HTTP 400 and two no-response attempts have no generation ID. |
| Earlier saved usage costs | $0.033053722 | Part of the opening balance, not an additional campaign charge. |
| Opening observed key usage | $0.044083172 | Inferred from the first campaign preflight's $2 limit minus $1.955916828 remaining. |
| Opening amount without individual receipts | $0.011029450 | Separate historical reconciliation gap. |
| Campaign known incremental costs | $0.089848266 | 497 attempts on 2026-09-30 UTC, one unknown cost. |
| Opening plus known campaign costs | $0.133931438 | Arithmetic subtotal, not certified complete account spending. |
| Later observed key usage | $1.23206716 | Historical metadata at 2026-09-30 14:24:07.173647 UTC. |
| Later usage difference | $1.098135722 | Unassigned; already inside later observed usage, never add it again. |
| Unknown-cost reservations | $0.00738793 | Allowances, not known charges or guaranteed billing bounds. |
| Proposed resumption | 107 requests / $0.4023704 reserved | Allocation creation records zero resumed attempts; reservation is not evidence of execution. |

The opening gap is already inside $0.133931438. The older $0.033053722 is
already inside $0.044083172. Neither belongs on top of the later account usage.
Deduplicating copies does not prove that the provider never billed a request
twice; no complete provider bill is available for that assertion.

The three unknown costs remain identifiable:

| Experiment / start time UTC | Model requested | Retained reservation |
|---|---|---:|
| `live-structure-dev-v2`, 2026-09-28 16:01:15.122003 | Qwen3-30B-A3B-Instruct-2507 | $0.0030102 |
| `live-structure-dev-remainder-v1`, 2026-09-28 16:20:27.586269 | Qwen3-30B-A3B-Instruct-2507 | $0.00301173 |
| `graph-dev-gpt-judge-batch01-20260930`, 2026-09-30 02:34:27.279760 | GPT-4.1-mini, HTTP 400 | $0.001366 |

The two Qwen attempts have no raw response. The HTTP 400 response hash verifies
but contains no usage cost. Absence of a usage field does not mean zero billing.
Old uncertainty may already be reflected in the opening account observation.

Nine unique preflight checkpoints are retained. Six of the seven campaign
preflights exactly match opening usage plus completed campaign response costs.
The 02:42:57.453366 checkpoint differs by **-$0.003601200**; the next checkpoint
matches again. The tool retains this temporary discrepancy without assigning
a cause. Provider billing timing and key continuity are not established by
these receipts. These observations do not attribute the later $1.098135722.

## Reproduce without a key

Run from a full clone containing the historical commit objects. Use a new
output directory: the instruments refuse to overwrite existing evidence.

```sh
mkdir -p /tmp/chatadhd-billing-review
python3 docs/research/handoff_2026-10-01/audit_saved_costs.py \
  --remote-heads docs/research/handoff_2026-10-01/remote_heads_2026-10-01.tsv \
  --output /tmp/chatadhd-billing-review/saved-audit.json
git show 33fb30a5e8c226b64e67ee6531304046392b6ad2:docs/research/PAID_RESUME_ALLOCATION_2026-09-30.json \
  > /tmp/chatadhd-billing-review/allocation.json
python3 -B loom/tools/structure/billing_reconciliation_v1.py \
  --saved-audit /tmp/chatadhd-billing-review/saved-audit.json \
  --allocation /tmp/chatadhd-billing-review/allocation.json \
  --repo . --output /tmp/chatadhd-billing-review/reconciliation.json
python3 -B -m unittest discover -s loom/tools/structure \
  -p 'test_billing_reconciliation_v1.py' -v
```

All input hashes are recorded. Original attempt/cost/source receipts are pinned
in the auditor's JSON and preserved on the historical source commit
`af3c81a5ddce70bdf4346a2c8c808ce6cc97b4d0`. The later usage observation comes
from allocation blob at `33fb30a5e8c226b64e67ee6531304046392b6ad2`; the earlier
usage observation is independently recovered from the hashed preflight ledger.

Without `--repo`, amount/counter consistency is still checked but raw receipt
verification is explicitly labelled `upstream_saved_flags_only`. It is not a
new verification of actual response bytes.

## Future provider export join, still offline

Privately obtain a provider usage/generation export with its exact date range,
currency and key/account identity evidence. It must include both the earlier
opening period and the later campaign interval to close both gaps. Preserve
the original file privately; do not commit an account export or identifying
account fields to this public repository. No live retrieval capability is
implemented by this instrument.

The tool accepts a JSON array, a JSON object with `rows`, or CSV. Default fields
are `generation_id` and `cost_usd`; alternative provider field names are
explicit command-line options. Minimal illustrative input, not real billing:

```json
{"rows":[{"generation_id":"gen-illustrative-only","cost_usd":"0.0001"}]}
```

```sh
python3 -B loom/tools/structure/billing_reconciliation_v1.py \
  --saved-audit /tmp/chatadhd-billing-review/saved-audit.json \
  --allocation /tmp/chatadhd-billing-review/allocation.json --repo . \
  --provider-export /private/provider-export.csv \
  --export-id-field generation_id --export-cost-field cost_usd \
  --output /tmp/chatadhd-billing-review/provider-join.json
```

Matching uses exact generation IDs, never model names or guessed time windows.
Identical repeated export rows deduplicate; conflicting costs or a generation
ID bound to multiple saved attempts fail visibly. The output lists matching
generations, cost discrepancies, supplied costs for previously unknown
attempts and unmatched export amounts. Unrecognised input fields are omitted.
An unmatched exported charge remains evidence about the supplied file; it is
not automatically assigned to this key or this campaign. Same-key identity,
export authenticity, period completeness and billing timing remain independent
evidence requirements.

The two no-response Qwen attempts and the HTTP 400 attempt cannot currently be
joined by generation ID. Closing those uncertainties needs independent provider
receipts or sufficiently authoritative key-scoped reconciliation; matching by
time/model alone would not establish individual charges.

## Verification boundary

Sixteen fabricated-record regressions cover double-counting, repeated real
attempts, unknown reservations, generation identity ambiguity, conflicting
exports, monetary validation, exact decimal input, monetary/counter consistency,
hashed local Git sources, transient checkpoint discrepancies, CSV/JSON CLI
joins, omitted unknown fields and refusal to overwrite receipts. Their result
is recorded in [billing/test-results.txt](billing/test-results.txt). They test
the accounting instrument, not provider invoice authenticity or live model
quality.

## Addendum: separately authorized EUR 5 programme

The owner's subsequent instruction authorizes paid work within **EUR 5 on a
separate new OpenRouter key**. That authority does not reset the old USD 2
programme, spend its purported remaining balance, or assign its historical
$1.098135722 difference. Earlier consent requirements in this report describe
the older programme. Additional permission is not missing for this new scope;
the new private credential and fresh execution evidence are missing.

OpenRouter documents [key spending limits in USD](https://openrouter.ai/docs/api/api-reference/api-keys/create-a-new-api-key)
and [USD-denominated credits](https://openrouter.ai/support/). Therefore a key
field `limit: 5` means USD 5, not EUR 5. No EUR/USD rate or current model price
is asserted by this preparation. Convert the authorized EUR amount using
documented, dated FX evidence and configure a USD cap no greater than that
conversion. A reference FX quote is not proof of a payment card's settlement
rate; record the selected conversion basis separately.

The [new programme preset](billing/new-programme-5eur.json) records this
authorization with an empty, separate ledger. It contains no secret, actual
key fingerprint, guessed FX rate or guessed USD cap. The
[offline preparation gate](../../../loom/tools/structure/new_budget5eur_gate.py)
does not inspect environment values, load a key, contact metadata endpoints or
dispatch inference. It validates supplied private evidence and reports
`blocked` until the necessary records exist. Its input contract requires:

1. A private binding record identifying the new programme, a SHA-256 key
   fingerprint, binding to the future transport's loaded credential, and the
   owner's confirmation that this is the separate new key. Fingerprints and
   credential labels are omitted from public gate output.
2. A fresh, fingerprint-bound [current-key metadata observation](https://openrouter.ai/docs/api/api-reference/api-keys/get-current-api-key)
   with USD limit, remaining credit and actual usage, no reset, no management
   credential, and explicit BYOK accounting. Freshness windows are editable
   data presets in the JSON, not application-wide ceilings.
3. Dated EUR→USD evidence with a source reference and an explicit USD cap
   within the EUR authority. Missing FX blocks preparation without inventing
   a rate.
4. A frozen stage manifest hash and fresh model/provider-specific endpoint
   price evidence. Each price row includes currency, source reference, raw
   snapshot SHA-256 and all charge components with declared upper unit
   quantities. The stage reservation must cover their calculated projection;
   the tool cannot verify an externally supplied tokenizer upper bound or
   claim a reservation is guaranteed provider billing.
5. The separate cumulative ledger: unique operation IDs, programme/key
   binding, first-response hash, reported actual cost and credit-versus-BYOK
   status. Actual amounts are summed after each operation. Unknown costs keep
   their reservation; the preset stops continuation until reconciled. Account
   usage must match known cumulative actuals before proceeding.

The live executor must capture fresh evidence **before every stage**, write a
durable reservation before dispatch, retain the first raw response and append
actual cost after each operation. Report that stage's actual and cumulative
costs, remaining cap and unresolved amounts. Re-run the gate after every paid
operation and before the next stage. Do not retry an uncertain billed attempt
automatically. Preserve the original runner's secret checks, first-response
retention and uncertain-charge handling when designing a successor; this
prepared gate is not integrated paid execution. Its supplied binding record
does not independently authenticate the eventual transport.

The native thread-2 `UsagePolicy` can additionally record admission,
reservations, ×10 confirmation and actual resource usage when adopted by a
future executor. Its finite binary64 quantities do not replace this exact
decimal billing ledger or actual provider receipts. A native admission receipt
alone is neither credential binding nor paid-call permission.

Current preparation can be reproduced without credentials:

```sh
python3 -B loom/tools/structure/new_budget5eur_gate.py \
  --policy docs/research/model_research_2026-10-04/billing/new-programme-5eur.json \
  --output /tmp/new-programme-5eur-readiness.json
python3 -B -m unittest discover -s loom/tools/structure \
  -p 'test_new_budget5eur_gate.py' -v
```

With private normalized evidence, add `--evidence /private/new-stage.json` and
`--ledger /private/new-programme-ledger.json`. Outputs refuse to overwrite
existing files. The tool's JSON/CSV historical export join remains separate;
the [generation metadata endpoint](https://openrouter.ai/docs/api/api-reference/generations/get-request-&-usage-metadata-for-a-generation)
documents generation identifiers and `total_cost`, useful for future private
reconciliation. Public [model metadata](https://openrouter.ai/docs/api/api-reference/models/list-all-models-and-their-properties)
contains pricing fields, but examples in documentation are not current price
quotes for any planned model.

To execute, provide the new key through the environment's private credential
mechanism under `OPENROUTER_THREAD7_NEW_KEY`; do not paste it into public files
or research manifests. No additional budget-consent question is needed for
the already authorized EUR 5 scope. The old expired credential is not reused.

Eight new offline regressions cover the missing-key state despite existing
authorization, EUR/USD cap mismatch, identity/reset/management checks, price
freshness and charge coverage, cumulative actual costs, unknown reservations,
duplicate operation IDs and budget overflow. Their invented FX/prices are
test fixtures. Neither this preparation nor these tests made paid calls.
