# Native graph pilot: reconciled partial continuation

Date: 2026-09-28. This is the combined result of the two disjoint native-prompt
development runs. **22 of the original 32 requests were started: 20 returned
responses, two have uncertain outcomes, and ten remain untouched. No returned
candidate passed the full graph contract.** The native baseline remains partial.
This experiment does not test JEV and does not establish whether structural
classification is inferior or superior to a lexical classifier.

## Preserved evidence

| Evidence | Original run | Continuation |
|---|---|---|
| Experiment | `live-structure-dev-v2` | `live-structure-dev-remainder-v1` |
| GitHub Actions run | [36447676519](https://github.com/klb-t/chatadhd/actions/runs/36447676519) | [36449504132](https://github.com/klb-t/chatadhd/actions/runs/36449504132) |
| Preparation commit | `dd85acca6189e7c98906680640fef119eb6b05fa` | `8dc01c717ef20b1c166a30ef1560b4e06b26ce56` |
| Archived result | [openrouter-native-dev-2026-09-28.zip](inputs/openrouter-native-dev-2026-09-28.zip) | [openrouter-native-remainder-2026-09-28.zip](https://github.com/klb-t/chatadhd/blob/af3c81a5ddce70bdf4346a2c8c808ce6cc97b4d0/docs/research/inputs/openrouter-native-remainder-2026-09-28.zip) |
| Manifest request count | 32 | 26 |
| Requests actually started | 6 | 16 |
| Returned responses | 5 | 15 |
| Uncertain outcomes | 1 | 1 |
| Requests untouched in that manifest | 26 | 10 |
| Sum of returned cost fields, USD | 0.00636045 | 0.01981174 |

Archive SHA-256 values:

- Original: `da619121ff8d4ed2fd28ea9a1753cb1c9f4d1da414447647ad1ae3acb2afed48`.
- Continuation: `0fe70aa08572f46e4d12640e046925bb62960bbfa6c415f9d070eb6c9bedc792`.

Canonical manifest hashes used by the ledgers:

- Original: `e7c047d815e92172c183e4da5c0cc598e3babeb32bdeec6f4970a389a71c2a06`.
- Continuation: `ff5f0e59859b0853336543277bc9f708e10c2799e85ed3e52bb1f9e80fd84756`.

Both archives contain their original manifests, plans, ledgers, raw responses,
and frozen score reports. The reservation JSON separately records the SHA-256
of the manifest's file bytes; that differs from its canonical JSON hash by
design. Do not substitute one for the other.

## Reconciliation and denominator

Offline inspection verified both archive hashes, both ledger-to-manifest
bindings, every attempted request's body hash and reservation, and every stored
response hash using the runner's ledger validator. The continuation manifest is
exactly the original ordered suffix after six attempted requests: all 26 IDs and
canonical request bodies match. The attempted ID sets are disjoint. The native
prompt, scorer, graph validator, vocabulary and runner hashes are unchanged;
`live_pilot.py` changed to support this explicit continuation.

The original score report retains its historical 26 `not_attempted` rows. For
the combined view, the continuation's rows replace those same IDs; the two
report denominators must not be added to produce 58 requests. The denominator
remains 32: 16 authored PL/EN development cases, each requested from both model
and provider pairs with `native_v1`.

| Outcome over the original plan | Count | Share of 32 |
|---|---:|---:|
| Returned response, duplicate JSON keys rejected | 6 | 18.75% |
| Returned response, candidate evaluated but contract rejected | 14 | 43.75% |
| Started; outcome and final charge unresolved | 2 | 6.25% |
| Never started | 10 | 31.25% |
| Full contract accepted / structural exact / semantic exact | 0 / 0 / 0 | 0% for each |

`scored` means the candidate validator returned a result, not that the graph was
accepted. All 14 such results have `contract_valid: false`. Consequently this
sample contains no valid candidate on which to estimate semantic accuracy
conditional on passing the contract. The end-to-end exact-success count is
0/32; the ten missing results and two uncertain results are not observations of
incorrect semantic reasoning.

| Model and explicit provider | Planned | Responses | Uncertain | Untouched | Duplicate JSON | Candidate contract failures | Returned cost sum, USD |
|---|---:|---:|---:|---:|---:|---:|---:|
| `openai/gpt-4.1-mini` via `openai` | 16 | 11 | 0 | 5 | 6 | 5 | 0.0222088 |
| `qwen/qwen3-30b-a3b-instruct-2507` via `siliconflow/fp8` | 16 | 9 | 2 | 5 | 0 | 9 | 0.00396339 |

No provider/model identity mismatch was recorded for the 20 returned responses.
No source text, candidate, or graph was automatically promoted to stored
knowledge.

## What failed

All six `invalid_response` cases were replayed through the strict JSON parser
offline. Each failed because the model repeated `entity_drafts` and/or
`claim_drafts` inside a JSON object. Ordinary JSON parsing that silently keeps
the last value would hide this failure and discard content. The original bytes
and frozen scores are retained; no response repair was used for these metrics.

For the other 14 responses, the first reported contract failure was:

| Failure code | Count | Meaning |
|---|---:|---|
| `quote_mismatch` | 9 | Claimed source support did not equal the cited source bytes |
| `handle` | 2 | A local identifier failed the contract |
| `subject` | 1 | A structural claim did not use a local occurrence as its subject |
| `scope_membership` | 1 | A non-scope occurrence lacked exactly one local scope |
| `utf8_boundary` | 1 | A cited byte range split a UTF-8 code point |

These are first failures, not a census of every defect in every response. The
experiment currently exposes substantial serialization and grounding friction
before it can meaningfully compare the recovered structures.

Both runs stopped after a roughly 60-second Qwen attempt whose result could not
be confirmed:

- Original: `r1c6d4cb41e50c3405bbbfbd2`, `case_002_en`,
  16:01:15–16:02:15 UTC, reservation USD 0.0030102.
- Continuation: `r53d37c7680bb73cd6c0708c2`, `case_006_en`,
  16:20:27–16:21:27 UTC, reservation USD 0.00301173.

Both ledger rows say `attempt_outcome_uncertain_no_retry`. The enclosing stop
reason is `byok_billing_unknown_stop`: the runner could not inspect that missing
response's billing fields. This is not evidence that BYOK was used. All 20
returned responses explicitly reported `is_byok: false`. Neither uncertain
request was repeated.

## Cost interpretation

The 20 returned responses report **USD 0.02617219** in total. This is the sum of
available response cost fields, not a fully reconciled account bill. The two
uncertain attempts have no returned cost fields and retain USD 0.00602193 of
combined reservation. Reservations are conservative accounting allowances, not
guaranteed provider billing bounds.

The continuation's initial key check reported USD 1.99325201 remaining from the
USD 2 cap. That is USD 0.00674799 below the initial balance, USD 0.00038754 more
than the first run's returned cost sum. The available evidence cannot assign
that difference conclusively to a particular request. No final account-level
reconciliation was performed here.

The continuation reserved USD 0.24142525 for its 26 untouched requests. It is
part of the original USD 0.29711246 planned reservation, not an additional full
32-request experiment. Across both runs, started requests reserved
USD 0.20425422. The ten remaining untouched requests account for
USD 0.09285824 of the original reservation.

## Remaining work and use of these results

The ten untouched requests are both models on `case_006_pl`, `case_007_en`,
`case_007_pl`, `case_008_en`, and `case_008_pl`. The two uncertain requests remain
unresolved, not eligible for automatic retry. The current continuation code
deliberately rejects chained continuation until these disjoint runs are
explicitly reconciled.

The owner reported exhaustion of GitHub Actions minutes with a reset on
October 1. This analysis made no model calls and started no Actions jobs.
Further runs are paused; that operational limit does not prevent offline
analysis and implementation. These partial results must not be described as a
completed 32-request benchmark.

The owner's priority is JEV and synonym-invariant structure. This native export
baseline supplies concrete failure cases for that programme, not a reason to
make word matching the primary classifier. A useful next design separates the
model's semantic structural decisions from deterministic identifier allocation,
UTF-8 coordinate calculation, schema construction and source checks. Such
mechanical assistance must not infer the semantic class from keywords, merge
different structures, or silently select among ambiguous source anchors.

For a later comparison, measure synonym/paraphrase stability separately from
sensitivity to changed roles, quantifiers, negation, conditional direction and
scope. Preserve first-response results and report any later deterministic
adapter as a separately named method. JEV's classification quality, its usage
rules, and lexical checks as a secondary omission detector require their own
evidence; this report makes no claim that those experiments have been completed.

The independently completed Jev Noul diagnostic and its limits are reported in
`JEV_RESULTS_2026-09-28.md`; the new 48-pair direct-comparison follow-up is unrun.
