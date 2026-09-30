# Independent semantic audit of the first GPT-assisted extraction

Reviewed 2026-09-30. This is a posthoc DEV audit of preserved LIVE responses,
not a new recipe, repaired score, blind evaluation, or measurement of world truth.
No API call, validation input/gold read, index operation, or frozen-file edit was
performed. The JSON companion records **every one of the 62 raw assertions and
their 62 quotes**, all nine raw status events, all four primary false negatives,
and all 24 billing receipts.

## Reproduction and denominators

An independent read-only replay of the raw responses exactly reproduced the
saved compiled objects, primary score object (including its separate event
convention diagnostic), and execution summary. All 24 response hashes matched
the ledger. The replay also checked the retained request bodies against the DEV
recipe and checked model/provider identity.

| Preserved primary measurement | TP | FP | FN | Gold | Predicted |
|---|---:|---:|---:|---:|---:|
| Source assertions | 56 | 6 | 4 | 60 | 62 |
| Status events | 0 | 9 | 6 | 6 | 9 |

The source-edge precision remains **56/62 = 0.90323** and recall **56/60 =
0.93333**. There were 24 complete cases and no unavailable cases. The compiled
file contains 58 accepted assertions, not 62: four rejected raw assertions still
count as primary false positives. Of the 58 accepted unique exact quotes, 37 are
complete turns and 21 are narrower subspans. This distinction prevents an audit
of only the compiled file from silently dropping the malformed raw predictions.

## Review of all 62 assertions

| Manual diagnostic category | Raw assertions | Meaning |
|---|---:|---|
| Adequate quote with turn metadata | 46 | The quoted clause states the typed relation, operands, direction and polarity; speaker comes from the turn or explicitly named quotation. |
| Adequate with supplied inventory aliases | 6 | DEV019–024 second-turn conditionals use aliases explicitly supplied for A and B. |
| Adequate only with the same-turn antecedent | 4 | DEV013–015 new positive causal clauses and DEV018's corresponding clause omit the subject. |
| Nonexact quote, typed reading matches full source | 4 | DEV016 has two; DEV017 and DEV018 have one each. Grounding rejection remains mandatory. |
| Unsupported negative from nonendorsement | 1 | DEV006 `sa2`. |
| Unsupported negative from absence | 1 | DEV007 `a2`. |
| **Total manually reviewed** | **62** | No assertion was excluded. |

These are audit categories, not a replacement quality metric. In particular,
reading the intended relation from the full source does not make a fabricated
quote valid. No direction reversal was found where the source actually asserts
a relation. The negated propositions in DEV007–012 remain the inventory's
opaque operands: “no error” and “not discarded” do not turn a positive
conditional into a negative relation. The explicitly denied reverse conditional
is represented separately. Quoted-speaker attribution is correct for the six
reported positive assertions, and independently asserted analyst denials remain
attributed to the analyst.

## Every primary edge error

| Case / local prediction | Error | Source-based diagnosis | Primary missed gold |
|---|---|---|---|
| DEV006 `sa2` | FP | “Przytaczam słowa osoby Iga; sam nie przyjmuję tego warunku.” declines endorsement. It does not assert negative `A implies B` by the reporter. This must remain unknown for that speaker. | None |
| DEV007 `a2` | FP | “This rule says nothing about whether the fan stops.” does not deny `B implies C`; it does not specify B as an antecedent of a denial. Silence is unknown. | None |
| DEV016 `a2` | FP + FN | The typed negative `A causes B` correctly reads the full correction, but its alleged quote ends with ASCII `"` where the source has `”`. | `gpv1_dev_016_e2` |
| DEV016 `a3` | FP + FN | The typed positive `A causes C` correctly reads the correction. Its alleged quote changes the closing quotation mark **and inserts A into the elliptical second clause**, producing text absent from the source. | `gpv1_dev_016_e3` |
| DEV017 `a2` | FP + FN | The typed negative correction is present, but the quote changes the closing quotation mark. | `gpv1_dev_017_e2` |
| DEV018 `a2` | FP + FN | The typed negative correction is present, but the quote changes the closing quotation mark. | `gpv1_dev_018_e2` |

Thus all four primary false negatives have a raw typed counterpart. They are
losses under the exact-evidence contract, rather than absent relation fields.
This is diagnostic information about the failure mode, not permission to count
them as primary true positives. In the first two rows, source uncertainty was
mistakenly converted into a negative relation; both are **false positive edges**.

## Narrow quotes: valid context versus standalone proof

Every one of the 21 narrower accepted quotes received a clause review.
Eleven directly state the needed relation with canonical operands. Six others
use exact inventory aliases in a complete conditional. For DEV019–024 the
inventory explicitly binds, for example, “the catch no longer holds” to A and
“the pin can move” to B; this mapping is an input premise. Omitting “To restate
the same rule…” does not invent that binding. This result cannot establish that
the model discovered or independently verified synonym equivalence.

The remaining four are context-dependent: DEV013–015 quote `it causes …
instead.` and DEV018 quotes `powoduje natomiast …`. Each correctly names the
predicate and new target, but A appears only in the preceding clause of the
**same retained turn**. Full-turn reading resolves the shared subject clearly.
These are valid source readings with retained context, but are insufficient
standalone A-to-C quotations. A consumer that discards the full turn would lose
the warrant for the source endpoint. No primary score was changed, and no
separate heuristic repair was applied.

## All nine status events

| Raw events | Count | Compiler acceptance | Semantic finding |
|---|---:|---|---|
| DEV001, DEV003, DEV004 | 3 | Accepted | **Unsupported cross-speaker supersession.** An analyst's denial does not withdraw or correct Mira's/Priya's reported statement. Both attributions should coexist. |
| DEV013, DEV014, DEV015 | 3 | Accepted | Same-speaker correction is explicit. The model chooses the negative replacement of old A-to-B; primary gold chooses new positive A-to-C. A defensible convention difference. |
| DEV016, DEV017, DEV018 | 3 | Rejected | Same-speaker correction intent is defensible, but event quotes are nonexact and the chosen negative replacements were rejected. No valid compiled event exists. |

Cross-speaker overwrite is a genuine semantic model error, **not** the
correction convention ambiguity. The frozen compiler mechanically accepts
these three events because it checks referenced accepted IDs, ordering and
exact evidence, without proving correction authority or same attribution.
Gold catches them as event false positives. Downstream processing must not
promote mechanical acceptance into authority to erase another speaker's
assertion.

The preserved alternative-negative-replacement diagnostic credits only the
three valid English correction events: **3 TP / 6 FP / 3 FN**, gold 6 and
predicted 9. It remains explicitly diagnostic; primary event scoring stays
**0 TP / 9 FP / 6 FN**. Malformed Polish evidence and cross-speaker overwrite
are not excused by the alternative convention. All six old positive correction
assertions are retained with their original dates; no raw history was deleted.

## Time, observation and content truth

All 62 raw assertion timestamps and all nine raw event timestamps copy their
cited source turn correctly. All accepted coordinates reproduce the exact
retained UTF-8 and character slices. This is a provenance and time-binding
check, not a semantic entailment proof. For DEV006/007, an exact turn observed
at the correct time still does not support the emitted negative relation.

The compiler attaches `observed_source_assertion` and `content_truth:
unverified` to accepted output. `observed_source_assertion` is a proposed
classification of the extraction, not an independently established fact. The
raw response establishes what the instrument emitted; the retained source
establishes what was actually said; this manual review checks the connection.
Even a correctly extracted assertion does not establish the reality of its
conditional or causal content. World-truth accuracy remains unmeasured.

Extraction used the complete three-turn case. Although dates are copied
correctly and an `as_of` graph projection can hide later assertions, this is
**retrospective full-conversation extraction**. It does not measure a prefix-only
online extractor's behavior before a future correction or paraphrase is seen.
The extraction payload has turns and inventory, with no candidate judgment
queries or gold labels. The inventory and aliases are still supplied premises;
this is not open-world proposition discovery.

## Independent cost and artifact reconciliation

All 24 raw receipts identify `openai/gpt-4.1-mini`, provider `OpenAI`, completion
finish reason `stop`, HTTP 200, and `is_byok: false`. Each provider-reported raw
usage cost equals its ledger entry. Decimal addition of **24/24** raw usage
costs is **USD 0.0188832**, exactly equal to the saved execution summary. There
are no missing-cost attempts. This is receipt reconciliation, not an external
invoice audit or a claim that a future run has the same cost.

| Artifact | SHA-256 |
|---|---|
| `extraction/run/manifest.json` | `719c234b8930d1c5752a098539fb383424d56f1500f4c661fa12c6e79c679351` |
| `extraction/run/ledger.json` | `393fe2154ae74022788784f1c5ec359123a9833e4dbb782e087c33abea9bbb74` |
| `extraction/first_score/compiled_first.json` | `a2d11718695b3c7c5f0f57857aec5124e8dcf7c2700588867ff30408ac8ce0ed` |
| `extraction/first_score/score_first.json` | `d9d9744fe98156a1503ea9991d9bc66dece706afe150552506dcee6b85ee1f0a` |
| `extraction/first_score/execution_summary.json` | `2dd045bfaa38330acff657211fbaa217b842f68fafa1480a979b32bfcdaf2cd8` |

The JSON companion records all 24 raw hashes and individual costs, the exact
reviewed quotations and supporting full turns, compiled grounding, supplied
aliases, and replay/source-code hashes. It contains no validation material.

The useful next hypothesis is a **separately preregistered** recipe or semantic
validation policy targeting nonendorsement-versus-denial, silence-versus-denial,
same-speaker correction authority and complete correction quotations. Any
future variant must retain these first results and use fresh validation without
tuning on a sealed holdout. No such variant was run or promoted by this audit.
