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
