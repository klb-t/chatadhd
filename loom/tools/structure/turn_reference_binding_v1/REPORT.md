# Turn-reference binding v1: first DEV replay

The deterministic source-binding arm recovered four mechanically rejected
source assertions without changing any typed assertion field. This is a
post-hoc development experiment on the original 24 GPT responses, not a
confirmatory model-quality result. The transform and 41 source/dependency
hashes were frozen before replay; derived inputs, raw model-content strings,
provenance and compiled outputs were preserved before this driver loaded DEV
gold. There were no new API calls or model changes. Validation remained sealed.

| Frozen primary metric | Original quote binding | Full turn-ID binding |
| --- | ---: | ---: |
| Strict edges TP / FP / FN | 56 / 6 / 4 | 60 / 2 / 0 |
| Strict edge precision | 56/62 = 90.32% | 60/62 = 96.77% |
| Strict edge recall | 56/60 = 93.33% | 60/60 = 100% |
| Strict status events TP / FP / FN | 0 / 9 / 6 | 0 / 9 / 6 |
| Strict event precision / recall | 0/9 / 0/6 | 0/9 / 0/6 |
| Accepted / invalid source assertions | 58 / 4 | 62 / 0 |
| Accepted / invalid status events | 6 / 3 | 9 / 0 |

The 62 raw assertions and nine raw status events retain their denominators.
The four newly accepted assertions are DEV016 e2/e3, DEV017 e2 and DEV018 e2
(raw local IDs are separately preserved in the compiled files). All are Polish
correction cases. Mechanical recovery adds two, one and one strict edge TP
respectively. The two unsupported negative edge FP remain; binding does not
repair semantics. None of the previously accepted 58 assertions was lost.

All 71 evidence entries had valid turn IDs and were bound; none was refused.
Thirty-three quotes changed to whole turns. Seven original hints were not
exact, unique source substrings: four assertion hints and three status-event
hints, all in DEV016–018. Every original hint remains in the sidecar together
with the complete original evidence entry, its raw-response hash, JSON pointer,
source ID, source speaker and known_at, whole-turn char/UTF-8 byte coordinates,
source-text hash and `full_source_copied_by_turn_id` derivation. All 24 raw
response byte hashes remained unchanged; comparison outside evidence quotes
found zero field drift.

The three additional accepted status events are still strict FP. All six
correction cases use a model supersession reference to the negative denial of
the old edge, while the frozen gold uses the new positive edge to a different
target as replacement. The primary convention remains unchanged. The other
three event FP are unsupported supersessions in attribution/speaker-change
cases. No actor filter, event-ID correction or gold-derived patch is applied.

The frozen scorer measures typed edge identity plus exact source-turn binding.
It does not measure whether an entire cited turn entails a model claim.
Its narrow-quote flag falls from 21 to zero by construction: that does not mean
clause adequacy improved or that semantic review can be removed. A mechanism
counterexample retains a positive assertion attributed to Alice while citing
Bob's explicit denial; compilation still marks source content truth
`unverified`. Another retains a cross-speaker supersession. Source evidence
binding and semantic assertion validation are separate tasks.

This replay uses retrospective whole-case extraction. It supplies no evidence
for independent past-prefix judgments or causal availability at an earlier
query cutoff. It makes no graph promotion and no production changes.

Decision: **keep** the turn-ID binding helper as an opt-in research mechanism,
with original hints and derivation preserved; **investigate** semantic support,
actor consistency, event conventions and prospective prefix extraction before
production adoption. The exact DEV strict score cannot establish generality.

Mechanism tests: 12/12. Existing graph adapter/replay regression: 32/32.
The current broad offline structure regression passed 606/606 in 13.33 seconds
with `TMPDIR=/var/tmp`; its complete output is preserved in
`structure_regression.log`. No test gate or existing source file changed.
These are mechanism and integrity checks, not model-quality measurements.

Reproduction uses a clean checkout of the same source files, without this
folder's generated `first_results` or freeze. From the folder run
`python replay.py freeze` and `python replay.py run`.
Both commands require the exact original repository-relative response,
baseline and source artifacts. Existing outputs are never overwritten.

The primary machine-readable results are in
`first_results/comparison_first.json`, with per-case metrics in both
`score_*_first.json` files. `freeze_before_replay.json` and
`first_results/freeze_before_gold.json` preserve execution order and hashes.
