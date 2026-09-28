# First real native extraction results

Run: https://github.com/klb-t/chatadhd/actions/runs/36447676519
Activation: `dd85acca6189e7c98906680640fef119eb6b05fa`.
Original results ZIP: `inputs/openrouter-native-dev-2026-09-28.zip`.
GitHub artifact ID 10982325453; verified SHA-256
`da619121ff8d4ed2fd28ea9a1753cb1c9f4d1da414447647ad1ae3acb2afed48`.

## Observed execution

The owner-corrected key reports a nonresetting USD 2 limit. The replacement
billing check passed. There were **6 attempted requests out of 32 planned**:
5 complete provider responses and 1 uncertain request after exactly 60 seconds.
Every complete response explicitly reports `is_byok: false`. Their reported
credit charges total **USD 0.00636045**. The sixth request's charge is unknown;
its full **USD 0.00301020** reservation remains accounted for, without a retry.
This is not a claim that the unknown request was free or that an estimate is a
guaranteed billing bound. The remaining 26 requests were not attempted.

The runner stopped after the uncertain response. GitHub's successful job status
means the partial ledger, score and responses were saved; it does not mean all
32 planned requests finished or that a graph was correct.

| Case | Model | Duration, seconds | Reported USD | Extraction outcome |
| --- | --- | ---: | ---: | --- |
| case_001_en | GPT-4.1-mini | 26.08 | 0.00180600 | Duplicate `entity_drafts` JSON key |
| case_001_en | Qwen3-30B-A3B | 45.01 | 0.00043431 | Source quote/byte range mismatch |
| case_001_pl | GPT-4.1-mini | 11.88 | 0.00159600 | Structural claim subject is not an occurrence |
| case_001_pl | Qwen3-30B-A3B | 20.83 | 0.00025014 | Required scope membership missing |
| case_002_en | GPT-4.1-mini | 22.05 | 0.00227400 | Duplicate entity and claim arrays |
| case_002_en | Qwen3-30B-A3B | 60.00 | unknown | Deadline; response not retained |

Five complete replies, **zero accepted candidate graphs**. The two repeated-key
JSON responses are not repaired with last-key-wins parsing. The other three reach
the candidate scorer but fail its contract. This is useful evidence that the
current one-call native prompt is not ready for automatic graph construction.
It is not a population accuracy estimate or a comparison of the two models:
most planned cases are still missing, and the observed subset is very small.
No graph updates were promoted.

## Completion and next priority

The owner asks us to finish the started check, then prioritize live Jev tests.
Prepare a separate continuation from this authenticated artifact, excluding all
six already attempted IDs, including the uncertain one. Recheck price/key limits
and submit only the 26 untouched requests. Preserve both ledgers and combine
coverage by original request ID; do not erase this interruption from the result.

The separately registered coordinate-aided and validation arms have not run.
Their scheduling is deferred in favor of the owner's clarified Jev priority.
The coordinate table supplies source locations only; it is not intended as a
primary lexical classifier and does not itself demonstrate structural invariance.

The primary target is a structure that survives synonyms, paraphrases and topic
changes while distinguishing relations, direction, scope and quantification.
Lexical/regex signals belong in secondary omission diagnostics. The next Jev
corpus therefore pairs equivalent wording/domain changes with same-vocabulary
structural counterexamples, rather than treating word overlap as success.

## Verification

The billing change passed **321 research tests** before activation. The artifact's
hash matches GitHub metadata; its response hashes and request inventory are
preserved. Local inspection confirmed the repeated JSON keys without selecting
or repairing a model reading. Original scoring must use the activation's frozen
code; subsequent continuation code changes do not rewrite this score.
