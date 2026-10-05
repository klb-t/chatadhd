# Independent new-label source/gold peer review v1

Review completed: 2026-10-05T13:05:21.433344Z.

**PASS.** All 24 sources (123 turns), 55 operand propositions and all 96 gold rationales were manually read. All 163 evidence references and both 96-row prepared method inputs passed independent exact span, source, timestamp, query inventory and frozen recipe checks. No mistaken label or open ambiguity remains. The author resolved one endpoint wording finding before root DATA freeze/collection; all 96 labels are now consistent with the source-commitment target.

This is author-visible newly authored synthetic source-commitment DATA, not a blind sealed holdout or a world-truth evaluation. The 12 PL / 12 EN inventory and four views per family are authored DATA choices, not algorithm ceilings or universal quotas. No engine criterion, exact answer text or source quota is added.

## Current reviewed hashes

| File | SHA-256 | Bytes |
| --- | --- | ---: |
| `inputs.json` | `8cfd43c19f2603b6cc6adcf0187115fe17f4e4cb2c157eb23308a99ad11e290d` | 71246 |
| `gold.json` | `2f6e041024658543ffacc20f123ee9ef047ac32858638b8eecb49ff3c9d4c6e2` | 124015 |
| `hash-manifest.json` | `d00fbb5e20670b44247437d462a9850d57795806946a5a7e4002d60d45370bb7` | 2167 |

Selection/preset freeze: `7aed09b9df6b8d2fc0afc3a399f12016795ca687` at 2026-10-05 12:36:53 UTC (git metadata checked). Authorship log begins at 12:39:40 UTC, with complete DATA first written at 12:43:33 UTC. This is recorded post-freeze provenance, with no claim of pretraining independence.

## Resolved pre-freeze finding and exact before history

**NL14-MODAL-ENDPOINT — closed.** The original `inputs.json#/cases/13/node_inventory/1/text` said “The tile may enter the final round.” Source t01 says “Entry of a tile into the final round requires a crescent mark on that tile,” and t03–t05 likewise discuss entry itself. Permission/eligibility and an entry occurrence can denote different endpoint propositions. The peer sent the exact pointer/quote finding to root and author before freeze.

The author changed only that node text to **“The tile enters the final round.”** This matches the actual-entry source proposition. The peer re-read all 5 source turns and all 4 query/gold rationales in family 014: q01 unknown (question only), q02 refuted (Quinn’s explicit reverse denial), q03 supported (Rhea’s adoption of the forward requirement), q04 unknown (Quinn’s reverse-denial withdrawal without reverse affirmation). All labels remain unchanged.

The exact prior draft is preserved at `new-label-corpus-v1-drafts/pre-peer-014-correction-20261005T130057Z`. All 12 preserved file hashes independently matched the original peer review/hash manifest. Source JSON differs at exactly `/cases/13/node_inventory/1/text`; the other 23 families, all source turns, every query and the whole gold file are identical. Both prepared arms change only the node text in four family 014 payloads; their 92 other rows and all question/criterion fields remain unchanged. All 192 source/role/time payload bindings and all 163 evidence spans were independently rechecked after the correction.

| Version | Inputs SHA-256 | Gold SHA-256 | Manifest SHA-256 |
| --- | --- | --- | --- |
| Before peer correction | `a8581461879c88bf2c46b7bdbfe526b3d41ea191bcaaac2b28ceeb7a137d7371` | `2f6e041024658543ffacc20f123ee9ef047ac32858638b8eecb49ff3c9d4c6e2` | `9308b6129be8f7578f94d30898b072009ef982ea6a2198df799f7ee310c0ee60` |
| Corrected and reviewed | `8cfd43c19f2603b6cc6adcf0187115fe17f4e4cb2c157eb23308a99ad11e290d` | `2f6e041024658543ffacc20f123ee9ef047ac32858638b8eecb49ff3c9d4c6e2` | `d00fbb5e20670b44247437d462a9850d57795806946a5a7e4002d60d45370bb7` |

Correction receipt: `new-label-corpus-v1/pre-freeze-correction-014.json`, SHA-256 `9daf2758f64698f085a607e983e0eb3eb0a0ccb173d6236539de9ee220b33bca`. The JSON companion preserves the original review hashes/manifest, all12 exact draft hashes, original conditional reasons and the author/delta history. The peer changed only this report pair. Readiness is complete for root’s DATA freeze before collection.

## Counts and boundary checks

| Inventory | PL | EN | Total |
| --- | ---: | ---: | ---: |
| Families | 12 | 12 | 24 |
| Queries / gold judgments | 48 | 48 | 96 |
| Supported | 17 | 17 | 34 |
| Refuted | 13 | 14 | 27 |
| Unknown | 18 | 17 | 35 |

Relation queries: implies 20; requires 24; causes 23; prevents 12; supports 17. These are DATA counts, not observed model scores.

All 12 current manifest entries and byte counts matched. Every gold query has exactly its input query, every evidence quote is a unique turn substring, Unicode code-point and UTF-8 offsets agree, every source/turn binding is correct, and no evidence turn is after `as_of`. Gold fields stay outside source/prepared inputs. Both frozen methods preserve query identity, language, inventory, attribution and exact physical timestamp prefix. The only directed-method recipe delta is the frozen append to `questions.q02.criteria.false`.

## Semantic trace for all 96 queries

The JSON companion holds each query’s full operand text, source/query/gold JSON pointers, requested speaker/time, whole visible prefix and all 163 exact quoted evidence checks. The compact table records independent reasoning. Source time matters; current world plausibility does not. Evidence IDs name the checked gold references; unknown is reviewed against the whole visible prefix.

| Query | Peer label | Speaker / as_of | Ordered relation | Evidence turns | Independent semantic reason |
| --- | --- | --- | --- | --- | --- |
| `new_label_001_q01` | refuted | Maks / 2026-09-01T09:03:00Z | n01 implies n02 | t01, t02 | Maks personally denies the lamp-to-curtain implication; Iga’s earlier affirmation has a different attribution. |
| `new_label_001_q02` | refuted | Iga / 2026-09-01T09:06:00Z | n01 implies n02 | t03 | Iga withdraws her own lamp-to-curtain positive, so the latest source commitment is a positive withdrawal. |
| `new_label_001_q03` | supported | Iga / 2026-09-01T09:09:00Z | n01 implies n02 | t03, t04 | Iga’s new lamp-to-curtain affirmation replaces her earlier withdrawal. |
| `new_label_001_q04` | unknown | Iga / 2026-09-01T09:12:00Z | n02 implies n01 | t01, t03, t04 | No visible Iga turn asserts or denies curtain-to-lamp implication; all her relation speech runs the other way. |
| `new_label_002_q01` | refuted | Ada / 2026-09-02T09:00:00Z | n01 requires n02 | t01 | Ada explicitly denies that digitisation requires the exhibition table. |
| `new_label_002_q02` | unknown | Ada / 2026-09-02T09:09:00Z | n01 requires n02 | t01, t02, t03, t04 | Ada removes her own denial and says she takes no position; Olek’s affirmation and her cover-colour note do not create a commitment for Ada. |
| `new_label_002_q03` | supported | Olek / 2026-09-02T09:06:00Z | n01 requires n02 | t03 | Olek personally affirms digitisation requiring the exhibition table. |
| `new_label_002_q04` | unknown | Olek / 2026-09-02T09:12:00Z | n02 requires n01 | t03, t05 | Olek’s positive and its withdrawal concern digitisation-to-table; no visible turn supplies table-to-digitisation commitment for him. |
| `new_label_003_q01` | unknown | Tomasz / 2026-09-03T09:03:00Z | n01 causes n02 | t02 | Tomasz expressly asks rather than asserting the token-to-gate causal relation. |
| `new_label_003_q02` | supported | Tomasz / 2026-09-03T09:09:00Z | n01 causes n02 | t04 | Tomasz explicitly adopts the token-to-gate cause as his own rule. |
| `new_label_003_q03` | refuted | Ruda / 2026-09-03T09:12:00Z | n01 causes n02 | t03, t05 | Ruda’s new token-to-gate denial supersedes her affirmative rule. |
| `new_label_003_q04` | unknown | Tomasz / 2026-09-03T09:12:00Z | n02 causes n01 | t02, t04 | Tomasz adopts token-to-gate only; no visible turn gives him gate-to-token causation. |
| `new_label_004_q01` | supported | Bruno / 2026-09-04T09:03:00Z | n03 causes n02 | t02 | Bruno positively asserts not-closing-the-valve causing flow-to-basin; source-operand negation is inside the asserted relation. |
| `new_label_004_q02` | refuted | Bruno / 2026-09-04T09:09:00Z | n03 causes n02 | t04 | Bruno retracts precisely his positive cause with the not-closed source operand. |
| `new_label_004_q03` | supported | Kaja / 2026-09-04T09:12:00Z | n01 prevents n02 | t01, t03, t04, t05 | Kaja retains closure preventing basin flow; Bruno’s different cause and withdrawal do not replace her commitment. |
| `new_label_004_q04` | unknown | Bruno / 2026-09-04T09:12:00Z | n02 causes n03 | t02, t04 | Bruno discusses only not-closed-valve-to-basin-flow; no visible turn commits him to basin-flow-to-not-closed-valve. |
| `new_label_005_q01` | unknown | Nela / 2026-09-05T09:00:00Z | n01 supports n02 | t01 | Nela explicitly presents Mira’s line as a quotation and excludes it from her own position. |
| `new_label_005_q02` | supported | Mira / 2026-09-05T09:00:00Z | n01 supports n02 | t01 | The quotation expressly assigns the positive spiral-to-admission support line to Mira. |
| `new_label_005_q03` | supported | Nela / 2026-09-05T09:09:00Z | n01 supports n02 | t03, t04 | Nela later adopts the spiral-to-admission relation and explicitly retains it in the table note. |
| `new_label_005_q04` | unknown | Jan / 2026-09-05T09:12:00Z | n02 supports n01 | t02, t05 | Jan’s denial and denial withdrawal run spiral-to-admission only; no visible commitment runs admission-to-spiral. |
| `new_label_006_q01` | supported | Emil / 2026-09-06T09:00:00Z | n03 implies n02 | t01 | Emil asserts not-blue-sign-to-sector-B implication positively. |
| `new_label_006_q02` | refuted | Emil / 2026-09-06T09:06:00Z | n01 implies n02 | t03 | Emil’s separate explicit whole-relation denial has the blue-sign source, distinct from his not-blue rule. |
| `new_label_006_q03` | unknown | Emil / 2026-09-06T09:09:00Z | n01 implies n02 | t01, t03, t04 | Emil retracts the blue-sign denial; the surviving affirmative rule has a different, not-blue endpoint. |
| `new_label_006_q04` | unknown | Lidia / 2026-09-06T09:12:00Z | n02 implies n03 | t02, t05 | Lidia declares no rule and later reports Emil without adoption; no own sector-B-to-not-blue implication appears. |
| `new_label_007_q01` | supported | Zofia / 2026-09-07T09:03:00Z | n01 causes n02 | t01 | Zofia’s bell-to-motion positive remains active before her withdrawal; Rafał’s white-indicator relation is distinct. |
| `new_label_007_q02` | refuted | Zofia / 2026-09-07T09:06:00Z | n01 causes n02 | t03 | Zofia retracts her own positive bell-to-motion causal declaration. |
| `new_label_007_q03` | supported | Zofia / 2026-09-07T09:12:00Z | n02 causes n01 | t04, t05 | Zofia explicitly supplies motion-to-bell causation; Rafał’s later minutes report does not alter it. |
| `new_label_007_q04` | unknown | Rafał / 2026-09-07T09:12:00Z | n02 causes n01 | t02, t05 | Rafał refuses adoption of Zofia’s motion-to-bell cause and asserts only a different support relation. |
| `new_label_008_q01` | unknown | Pola / 2026-09-08T09:00:00Z | n01 requires n02 | t01 | Pola initially asserts print-run-to-frame requirement; frame-to-print-run is absent at this prefix. |
| `new_label_008_q02` | refuted | Pola / 2026-09-08T09:06:00Z | n01 requires n02 | t03 | Pola explicitly denies frame-to-print-run requirement. |
| `new_label_008_q03` | supported | Pola / 2026-09-08T09:09:00Z | n02 requires n01 | t01, t04 | Pola retains print-run-to-frame requirement, unaffected by her distinct reverse denial. |
| `new_label_008_q04` | supported | Bartek / 2026-09-08T09:12:00Z | n02 requires n01 | t02, t05 | Bartek both withdraws the earlier denial and independently affirms print-run-to-frame; the new positive supplies support. |
| `new_label_009_q01` | refuted | Orson / 2026-09-09T09:03:00Z | n01 supports n02 | t02 | The quoted negative pine-chord-to-finale support line is expressly assigned to Orson. |
| `new_label_009_q02` | unknown | Wit / 2026-09-09T09:03:00Z | n01 supports n02 | t02 | Wit explicitly excludes the reported narrator’s negative support line from his own view. |
| `new_label_009_q03` | refuted | Anka / 2026-09-09T09:06:00Z | n01 supports n02 | t03 | Anka retracts her positive pine-chord-to-finale support. |
| `new_label_009_q04` | supported | Anka / 2026-09-09T09:12:00Z | n02 supports n01 | t03, t04, t05 | Anka explicitly adopts finale-to-pine-chord support, distinct from her withdrawn forward relation. |
| `new_label_010_q01` | unknown | Daria / 2026-09-10T09:00:00Z | n01 prevents n02 | t01 | Daria says the relation is absent from her account and that she has not evaluated it; this is an explicit lack of position. |
| `new_label_010_q02` | refuted | Daria / 2026-09-10T09:06:00Z | n01 prevents n02 | t03 | Daria later personally denies latch preventing the block’s fall. |
| `new_label_010_q03` | unknown | Daria / 2026-09-10T09:12:00Z | n01 prevents n02 | t03, t05 | Daria retracts her own prevention denial and explicitly withholds both positive and negative positions. |
| `new_label_010_q04` | unknown | Leon / 2026-09-10T09:15:00Z | n02 prevents n01 | t02, t04, t06 | Leon’s assertion and withdrawal concern latch-to-fall prevention; his stairs note leaves fall-to-latch absent. |
| `new_label_011_q01` | supported | Hubert / 2026-09-11T09:03:00Z | n02 implies n01 | t02 | Hubert independently asserts wave-sign-to-raised-sail implication. |
| `new_label_011_q02` | refuted | Ewa / 2026-09-11T09:06:00Z | n01 implies n02 | t03 | Ewa replaces sail-to-wave-sign affirmation with an explicit denial. |
| `new_label_011_q03` | unknown | Hubert / 2026-09-11T09:09:00Z | n01 implies n02 | t02, t04 | Hubert reports Ewa’s change without adoption; his own implication is the reverse of this query. |
| `new_label_011_q04` | supported | Ewa / 2026-09-11T09:12:00Z | n01 implies n02 | t03, t05 | Ewa’s renewed sail-to-wave-sign affirmation replaces her denial. |
| `new_label_012_q01` | supported | Filip / 2026-09-12T09:00:00Z | n01 requires n03 | t01 | Filip positively asserts glaze-cycle requiring a not-green indicator; target negation is part of the required proposition. |
| `new_label_012_q02` | refuted | Filip / 2026-09-12T09:06:00Z | n01 requires n03 | t03 | Filip retracts his positive requirement of the not-green indicator. |
| `new_label_012_q03` | supported | Filip / 2026-09-12T09:12:00Z | n01 requires n02 | t04 | Filip separately affirms glaze-cycle requiring green light; the withdrawn requirement used the other target. |
| `new_label_012_q04` | unknown | Sara / 2026-09-12T09:12:00Z | n03 requires n01 | t02, t05 | Sara’s retained denial is glaze-cycle-to-not-green; no visible declaration addresses not-green-to-glaze-cycle. |
| `new_label_013_q01` | supported | Hazel / 2026-09-13T09:03:00Z | n01 causes n02 | t01, t02 | Hazel’s personal tone-to-circle causal assertion survives Otto’s disagreement. |
| `new_label_013_q02` | refuted | Hazel / 2026-09-13T09:06:00Z | n01 causes n02 | t03 | Hazel retracts her own positive tone-to-circle causation. |
| `new_label_013_q03` | unknown | Otto / 2026-09-13T09:12:00Z | n01 causes n02 | t02, t04, t05 | Otto removes his denial and withholds a new position; Hazel’s later housing note supplies no Otto affirmation or renewed denial. |
| `new_label_013_q04` | unknown | Hazel / 2026-09-13T09:12:00Z | n02 causes n01 | t01, t03, t05 | Hazel’s assertions and withdrawal run tone-to-circle only; no visible circle-to-tone commitment is attributable to her. |
| `new_label_014_q01` | unknown | Rhea / 2026-09-14T09:03:00Z | n01 requires n02 | t02 | Rhea asks about crescent-to-entry without asserting it. No personal positive or negative requirement exists at this prefix. |
| `new_label_014_q02` | refuted | Quinn / 2026-09-14T09:06:00Z | n01 requires n02 | t03 | Quinn personally denies crescent-to-entry requirement in t03. The entry endpoint now exactly denotes the actual-entry proposition used by the source. |
| `new_label_014_q03` | supported | Rhea / 2026-09-14T09:09:00Z | n02 requires n01 | t04 | Rhea explicitly adopts entry-to-crescent requirement as her own in t04. Her requested source endpoint denotes actual entry. |
| `new_label_014_q04` | unknown | Quinn / 2026-09-14T09:12:00Z | n01 requires n02 | t01, t03, t05 | Quinn withdraws his denial of crescent-to-entry and explicitly leaves that reverse question open; his retained requirement runs entry-to-crescent instead. |
| `new_label_015_q01` | unknown | Jules / 2026-09-15T09:03:00Z | n01 supports n02 | t02 | Jules explicitly declines to endorse the quoted Sable denial. |
| `new_label_015_q02` | refuted | Sable / 2026-09-15T09:03:00Z | n01 supports n02 | t02 | The quoted filter-to-pattern support denial is expressly assigned to Sable. |
| `new_label_015_q03` | supported | Jules / 2026-09-15T09:12:00Z | n01 supports n02 | t03, t05 | Jules personally affirms filter-to-pattern support and retains it; Imani’s different personal denial cannot retract Jules. |
| `new_label_015_q04` | unknown | Imani / 2026-09-15T09:12:00Z | n02 supports n01 | t01, t04 | Imani’s positive and later denial both concern filter-to-pattern; no visible statement supplies pattern-to-filter support for Imani. |
| `new_label_016_q01` | supported | Maren / 2026-09-16T09:00:00Z | n01 prevents n02 | t01 | Maren explicitly affirms latch-to-parcel-arrival prevention. |
| `new_label_016_q02` | refuted | Maren / 2026-09-16T09:06:00Z | n01 prevents n02 | t03 | Maren retracts her own positive latch-to-parcel-arrival prevention. |
| `new_label_016_q03` | supported | Dex / 2026-09-16T09:09:00Z | n02 prevents n01 | t02, t04 | Dex independently asserts parcel-arrival-to-latch prevention and retains that reverse commitment. |
| `new_label_016_q04` | unknown | Maren / 2026-09-16T09:12:00Z | n02 prevents n01 | t01, t03, t04, t05 | Maren’s re-affirmation is forward only; Dex’s reverse relation is not attributable to her. |
| `new_label_017_q01` | supported | Tess / 2026-09-17T09:00:00Z | n03 implies n02 | t01 | Tess positively asserts not-flag-to-preview implication; operand negation does not negate the relation. |
| `new_label_017_q02` | refuted | Tess / 2026-09-17T09:06:00Z | n01 implies n02 | t03 | Tess separately denies flag-to-preview implication using the other source proposition. |
| `new_label_017_q03` | unknown | Tess / 2026-09-17T09:09:00Z | n01 implies n02 | t01, t03, t04 | Tess retracts the flag-to-preview denial and retains only the distinct not-flag positive. |
| `new_label_017_q04` | unknown | Noam / 2026-09-17T09:12:00Z | n02 implies n03 | t05 | Noam adopts not-flag-to-preview; no visible own declaration supplies preview-to-not-flag implication. |
| `new_label_018_q01` | supported | Fen / 2026-09-18T09:03:00Z | n01 supports n02 | t02 | Fen is expressly the speaker of the quoted lighthouse-to-bookmark positive support line. |
| `new_label_018_q02` | unknown | Sol / 2026-09-18T09:03:00Z | n01 supports n02 | t02 | Sol says he is only reporting Fen at this prefix. |
| `new_label_018_q03` | refuted | Wren / 2026-09-18T09:09:00Z | n01 supports n02 | t01, t03, t04 | Wren retracts lighthouse-to-bookmark support; Sol’s independent later adoption does not restore Wren’s commitment. |
| `new_label_018_q04` | supported | Wren / 2026-09-18T09:12:00Z | n02 supports n01 | t03, t05 | Wren independently asserts bookmark-to-lighthouse support, distinct from her withdrawn forward relation. |
| `new_label_019_q01` | refuted | Gail / 2026-09-19T09:00:00Z | n01 causes n02 | t01 | Gail personally denies panel-to-wheel causation. |
| `new_label_019_q02` | unknown | Gail / 2026-09-19T09:09:00Z | n01 causes n02 | t01, t03, t04 | Gail retracts the denial without affirmation; her later typeface note does not reinstate it. |
| `new_label_019_q03` | supported | Bram / 2026-09-19T09:06:00Z | n01 causes n02 | t02, t03 | Bram’s panel-to-wheel affirmation survives Gail’s separate denial withdrawal. |
| `new_label_019_q04` | supported | Bram / 2026-09-19T09:12:00Z | n02 causes n01 | t05 | Bram independently asserts wheel-to-panel causation and says he retains the first direction. |
| `new_label_020_q01` | supported | Aster / 2026-09-20T09:03:00Z | n02 requires n01 | t01, t02 | Aster’s case-entry-to-border requirement survives Beck’s disagreement. |
| `new_label_020_q02` | refuted | Beck / 2026-09-20T09:03:00Z | n02 requires n01 | t02 | Beck personally denies case-entry-to-border requirement. |
| `new_label_020_q03` | supported | Beck / 2026-09-20T09:09:00Z | n02 requires n01 | t04 | Beck personally affirms case-entry-to-border after withdrawing the negative. |
| `new_label_020_q04` | refuted | Aster / 2026-09-20T09:12:00Z | n01 requires n02 | t03, t05 | Aster retracts her positive border-to-case-entry requirement; her retained original runs the other way. |
| `new_label_021_q01` | supported | Cleo / 2026-09-21T09:00:00Z | n01 prevents n02 | t01 | Cleo positively asserts violet-background-to-showcase prevention. |
| `new_label_021_q02` | refuted | Cleo / 2026-09-21T09:06:00Z | n01 prevents n02 | t03 | Cleo retracts her own positive prevention rule. |
| `new_label_021_q03` | refuted | Orin / 2026-09-21T09:09:00Z | n03 supports n02 | t02, t04 | Orin explicitly denies not-violet-background-to-showcase support; this denies the relation with a negated source, rather than merely mentioning a negation. |
| `new_label_021_q04` | unknown | Cleo / 2026-09-21T09:12:00Z | n02 prevents n01 | t01, t03, t05 | Cleo’s positive and withdrawal concern background-to-showcase prevention; no visible own showcase-to-background rule appears. |
| `new_label_022_q01` | unknown | Pax / 2026-09-22T09:03:00Z | n01 implies n02 | t02 | Pax expressly refuses endorsement of Vale’s reported gate-to-star denial. |
| `new_label_022_q02` | refuted | Pax / 2026-09-22T09:06:00Z | n01 implies n02 | t03 | Pax later makes the gate-to-star denial personally. |
| `new_label_022_q03` | unknown | Pax / 2026-09-22T09:15:00Z | n01 implies n02 | t03, t05, t06 | Pax withdraws his denial without affirmation; Esme’s frame note does not restore Pax’s negative. |
| `new_label_022_q04` | unknown | Esme / 2026-09-22T09:15:00Z | n02 implies n01 | t01, t04, t06 | Esme’s gate-to-star assertion and withdrawal do not supply star-to-gate implication. |
| `new_label_023_q01` | supported | Nico / 2026-09-23T09:03:00Z | n01 causes n02 | t01, t02 | Nico’s forward cause remains active; Uma’s reverse denial has a different speaker and direction. |
| `new_label_023_q02` | refuted | Nico / 2026-09-23T09:06:00Z | n01 causes n02 | t03 | Nico replaces his forward cause with an explicit whole-relation denial. |
| `new_label_023_q03` | unknown | Uma / 2026-09-23T09:09:00Z | n02 causes n01 | t02, t04 | Uma removes her denial of the reverse cause and takes no positive position. |
| `new_label_023_q04` | supported | Nico / 2026-09-23T09:12:00Z | n01 causes n02 | t03, t05 | Nico’s latest forward affirmation replaces his earlier denial. |
| `new_label_024_q01` | supported | Lyra / 2026-09-24T09:03:00Z | n01 requires n03 | t01, t02 | Lyra positively requires a silent-bell target; Seth’s report of Arden has different attribution and cannot retract Lyra. |
| `new_label_024_q02` | refuted | Lyra / 2026-09-24T09:06:00Z | n01 requires n03 | t03 | Lyra retracts her positive moon-to-silent-bell requirement. |
| `new_label_024_q03` | supported | Lyra / 2026-09-24T09:15:00Z | n01 requires n02 | t04, t06 | Lyra independently requires a sounding bell and explicitly retains that current target in her pedestal note. |
| `new_label_024_q04` | unknown | Seth / 2026-09-24T09:12:00Z | n03 requires n01 | t02, t05 | Seth’s report and later personal adoption deny moon-to-silent-bell only; no visible own silent-bell-to-moon requirement appears. |

## Exposure and limits

Read mandatory owner/instruction documents and only the named source-view/code contracts, fresh corpus files, prepared source inputs and corpus metadata. Public budget/build verification metadata was visible in normative documents; no historical model-quality/selection accuracy file, earlier corpus/gold, model output, private key, temporary ledger, old control/fixture or sealed holdout was opened or loaded. The authorised correction review read only the exact previously reviewed draft and correction receipt for provenance. Contract functions were inspected statically; no corpus producer or production/native runner was executed by this peer. No source/gold edits, git mutation, commits, network or paid/model calls were made. Only this report pair was written.

The adversarial peer confirmed a separate scope of 12 fresh FRONTIER/REPLY packets; this review did not duplicate that graph-source work.

Four queries share each family conversation; they are views rather than 96 independent conversations. The fictional families vary their operands and scenarios while sharing lifecycle motifs. No statistical independence, representativeness or external-conversation adequacy follows. Later performance claims must identify this newly authored synthetic population and report family and query denominators.
