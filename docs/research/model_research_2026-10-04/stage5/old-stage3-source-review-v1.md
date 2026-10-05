# Stage 3 — primary source review of eight saved scientific repetitions

Primary review complete; independent semantic peer review pending. JSON: `old-stage3-source-review-v1.json` (`bd8b73f24b158029f7a67cab5eb475b7748822a1a1abda3d82e762c7c15e5f44`).

All eight complete first contents were read against unchanged preregistration `9787f92838174075e34e1fa579153abbce6a28f02e57b3d653895d224163c9d8`. No model was called, no answer was repaired, and no new corpus was inspected by this reviewer. Two known authored DEV cases were repeated; this is not an unseen evaluation.

| Recipe / task | Repetition | Source families | Evaluable | Target | Adequacy | Grounding | Mechanical | Actual USD |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| gpt61_sol / pattern_discovery | 1 | 2 reused | 2/2 | 2/2 | 2/2 | 2/2 | 0/2 | 0.0362645 |
| gpt61_sol / pattern_discovery | 2 | 2 reused | 2/2 | 2/2 | 2/2 | 2/2 | 0/2 | 0.0192577 |
| gemini31_pro / graph_completion | 1 | 2 reused | 2/2 | 2/2 | 2/2 | 2/2 | 0/2 | 0.048968 |
| gemini31_pro / graph_completion | 2 | 2 reused | 2/2 | 2/2 | 2/2 | 2/2 | 0/2 | 0.04442 |

**Total: 8/8 semantically evaluable, 8/8 task-correct, 8/8 adequate, 8/8 source-grounded; 0/8 mechanically valid. Actual verified cost in the bound public records: $0.1489102.** These manual labels concern source-relative meaning, not native graph acceptance or real-world truth.

For GPT polarity controls, the two singleton structures preserve opposite signs and the complete response explicitly declares no verified repeated pattern. Nonempty singleton descriptions are not counted as a repeated qualified template. Gemini adopts the affirmative `right1 → produces → right2` extraction with the exact `right-second` support, `dev/en`, and content truth unverified. Three replies omit a literal `extra.polarity` member; their adopted affirmative meaning remains clear, while their emitted JSON stays unchanged and mechanically invalid. The final reply also preserves its `origin=model` / `extractor=model-inferred` metadata caveat. No missing key is reconstructed.

The recipes solve different tasks. The eight repetitions support known-case repeatability only: each recipe has four opportunities over the same two source families. No cross-task winner, unseen-quality estimate, confidence quota, or production-ready preset follows from these counts.

## Exact evidence by opportunity

### 0: gpt61_sol / repeat / repetition 1

Operation: `strong-repeat.69ae9745e98d352264f4dfad791fb2102e337f6c938f5187855b9244c6862807`. Output: `NORMALIZED.json/responses/0/projection/choices/0/message/content`; content SHA `a5781ee43f93cae7e648391f3c533f93b04c83ca145f56e05370510b92dbb6e3`. Input: `source-prepared.json/requests/0`. All three semantic labels: **pass**; mechanical: **fail** (`comparison_pattern_label_abstraction_undeclared`). Cost: `$0.0170035`.

The exact positive/dev/en condition-permits-action template has two separate injective left/right claim-backed witnesses. Label abstraction is explicitly declared. The optional produces omission and extended-chain proposal remain unverified and are not counted as a second full-chain witness. Caller-declared groups do not become empirical independence.

- `/patterns/0/expected_property`: “Two exact structural witnesses of a directed condition-permits-action edge, with kinds and all qualifiers preserved. Confidence concerns structural matching, not content truth or universal validity.” (raw codepoints 854 + 198; UTF-8 bytes 854 + 198).
- `/transformation_report/loss/0/detail`: “Entity labels and canonical keys are not represented in template nodes; original identities remain available through separate occurrence node maps.” (raw codepoints 1255 + 147; UTF-8 bytes 1255 + 147).
- `/transformation_report/augmentation/1/status`: “not_verified_as_recurring” (raw codepoints 3166 + 25; UTF-8 bytes 3166 + 25).

Source anchors: `left-first, left-second, right-first, right-second`; all exact current request pointers, original locators, quote hashes and spans are in JSON.

### 1: gemini31_pro / repeat / repetition 1

Operation: `strong-repeat.9f115d9b7fa65c744813128af143b73506883bee8f338881947566fa66c0e8c9`. Output: `NORMALIZED.json/responses/1/projection/choices/0/message/content`; content SHA `7d83882191e1986e7e118516c8f0f70a039f91770492d1531f76d68700f3d466`. Input: `source-prepared.json/requests/1`. All three semantic labels: **pass**; mechanical: **fail** (`comparison_transformation_report_required`). Cost: `$0.030104`.

The output adds precisely right1 produces right2 as an affirmative positive/dev/en source-backed extraction, with the exact right-second quote and locator. Existing records are neither removed nor modified; content truth is explicitly unverified. Metadata record shape is mechanically invalid but the meaning is complete and unambiguous.

- `/diff/added/claims/0/assessment/basis/support/0/quote`: “right: action produces result; scope=dev.” (raw codepoints 865 + 41; UTF-8 bytes 865 + 41).
- `/diff/added/claims/0/open/questions/0`: “Content truth is unverified.” (raw codepoints 1415 + 28; UTF-8 bytes 1415 + 28).
- `/transformation_report/augmentations/0/description`: “Added missing claim 'right-second-claim' to capture the relation observed in the 'right-second' source text 'right: action produces result', mapping 'right1' (action) to 'right2' (result).” (raw codepoints 2521 + 188; UTF-8 bytes 2521 + 188).

Source anchors: `left-first, left-second, right-first, right-second`; all exact current request pointers, original locators, quote hashes and spans are in JSON.

### 2: gpt61_sol / polarity / repetition 1

Operation: `strong-repeat.46a926d973632276e81b96fe4b43f46ec278e198aa57c6f9e9296345777a3a5d`. Output: `NORMALIZED.json/responses/2/projection/choices/0/message/content`; content SHA `834b359171b2ff787f7df9a40a2085a570bc4cd6f799b1c3f0ada2652a813349`. Input: `source-prepared.json/requests/2`. All three semantic labels: **pass**; mechanical: **fail** (`comparison_transformation_report_required`). Cost: `$0.019261`.

The two emitted singleton structures preserve opposite permission polarities, direction, kinds and concrete witnesses. The whole response expressly denies verified recurrence. It does not present the singleton structures as two witnesses of a shared qualified template. The optional source-backed omission stays unverified; it is not filled by recurrence.

- `/transformation_report/recurrence/description`: “The permission edges share endpoint kinds and predicate but differ in polarity. The apparent two-chain recurrence is not an exact structural recurrence.” (raw codepoints 2978 + 152; UTF-8 bytes 2978 + 152).
- `/patterns/0/alternatives/0`: “Single witnessed structure, not established recurrence.” (raw codepoints 1237 + 55; UTF-8 bytes 1237 + 55).
- `/transformation_report/augmentation/0/status`: “source-backed candidate; not a verified claim occurrence” (raw codepoints 3488 + 56; UTF-8 bytes 3488 + 56).

Source anchors: `left-first, left-second, right-first, right-second`; all exact current request pointers, original locators, quote hashes and spans are in JSON.

### 3: gemini31_pro / polarity / repetition 1

Operation: `strong-repeat.0cb265ed5e75e8b8f8718ff9efeb7b294d9569e0a9f8517c4c5c438b5b3f9db7`. Output: `NORMALIZED.json/responses/3/projection/choices/0/message/content`; content SHA `9f4fd624b12986ab6d264dca21d662235df5fe55e9c71525554cccab83408356`. Input: `source-prepared.json/requests/3`. All three semantic labels: **pass**; mechanical: **fail** (`comparison_transformation_report_required`). Cost: `$0.018864`.

The exact added right1-produces-right2 proposition is affirmatively extracted from the supplied right-second statement, keeping dev/en, exact support and unverified content truth. It is not inferred from the permission recurrence or converted to a negative relation. The lack of a literal extra.polarity member does not make this affirmative source extraction ambiguous. Empty modified/removed cohorts preserve the original claims.

- `/diff/added/claims/0/assessment/basis/support/0/quote`: “right: action produces result; scope=dev.” (raw codepoints 783 + 41; UTF-8 bytes 783 + 41).
- `/diff/added/claims/0/open/questions/0`: “Content truth is unverified.” (raw codepoints 1615 + 28; UTF-8 bytes 1615 + 28).
- `/transformation_report/augmentations/0/description`: “Added claim for the missing relation in 'right-second' source observation.” (raw codepoints 2502 + 74; UTF-8 bytes 2502 + 74).

Source anchors: `left-first, left-second, right-first, right-second`; all exact current request pointers, original locators, quote hashes and spans are in JSON.

### 4: gpt61_sol / repeat / repetition 2

Operation: `strong-repeat.5bb959182bf847bff996d39972b5e9f1ff234ad1154e28eae73fea65bcf2117f`. Output: `NORMALIZED.json/responses/4/projection/choices/0/message/content`; content SHA `04f6a2f496d4722840a97d67c29874dcae01c1f9585156feb4dc35e313a19fff`. Input: `source-prepared.json/requests/4`. All three semantic labels: **pass**; mechanical: **fail** (`comparison_transformation_report_required`). Cost: `$0.0084187`.

A positive/dev/en permission template is witnessed separately by the existing left-first-claim and right-first-claim. The declared label abstraction retains concrete occurrence identities. Only the left full chain is verified; the right produces gap stays an unverified proposed extraction, and source group names are explicitly not treated as empirical independence.

- `/patterns/0/expected_property`: “Candidate recurring directed condition-to-action permission edge, with exact supplied qualifiers; confidence concerns structural matching, not content truth or universal validity.” (raw codepoints 854 + 179; UTF-8 bytes 854 + 179).
- `/transformation_report/losses/0/detail`: “Entity labels, including left/right naming, are omitted from the template. Concrete identity is retained in each occurrence's node_map.” (raw codepoints 1324 + 135; UTF-8 bytes 1324 + 135).
- `/transformation_report/augmentation/0/status`: “unverified_proposed_extraction” (raw codepoints 2400 + 30; UTF-8 bytes 2400 + 30).

Source anchors: `left-first, left-second, right-first, right-second`; all exact current request pointers, original locators, quote hashes and spans are in JSON.

### 5: gemini31_pro / repeat / repetition 2

Operation: `strong-repeat.167b25f94d1f4a8e549388729b4419cf1964c85bbe665ddc8139bfe85ab1c990`. Output: `NORMALIZED.json/responses/5/projection/choices/0/message/content`; content SHA `b24777b69712ef4c713b3d80bbf2b80a220a93ccab5d6de7701f83ca40977d5e`. Input: `source-prepared.json/requests/5`. All three semantic labels: **pass**; mechanical: **fail** (`comparison_transformation_report_required`). Cost: `$0.02162`.

The exact added right1-produces-right2 statement is explicitly adopted from right-second, with exact source locator, dev/en and unverified content truth. No existing claim is changed or removed. The claim has affirmative positive meaning even though extra={} does not encode a polarity key; there is no unresolved analogy or alternative negation in the answer.

- `/diff/added/claims/0/assessment/basis/support/0/quote`: “right: action produces result; scope=dev.” (raw codepoints 1417 + 41; UTF-8 bytes 1417 + 41).
- `/diff/added/claims/0/open/questions/0`: “Content truth is unverified.” (raw codepoints 1863 + 28; UTF-8 bytes 1863 + 28).
- `/transformation_report/augmentations/0`: “Added missing 'produces' claim between right1 and right2 based on the 'right-second' source text.” (raw codepoints 2336 + 97; UTF-8 bytes 2336 + 97).

Source anchors: `left-first, left-second, right-first, right-second`; all exact current request pointers, original locators, quote hashes and spans are in JSON.

### 6: gpt61_sol / polarity / repetition 2

Operation: `strong-repeat.f3a8743493d043427f915133e7cf876256fe6422bbf37ae725d53b100da595f6`. Output: `NORMALIZED.json/responses/6/projection/choices/0/message/content`; content SHA `da832d8e796f7e8d1d095d00ea2f6cb774f3d939d42acd1945cc0e4d8a9d9fac`. Input: `source-prepared.json/requests/6`. All three semantic labels: **pass**; mechanical: **fail** (`comparison_transformation_report_required`). Cost: `$0.010839`.

Every described occurrence is a singleton with matching existing claims, and permission polarity remains positive on the left and negative on the right. The complete response explicitly declares no verified repeated pattern and one complete claim-backed left-chain occurrence. Label loss and unverified groups are disclosed; the optional right produces candidate is not counted as a verified occurrence.

- `/transformation_report/recurrence/description`: “The permission edges differ in polarity, so they cannot witness the same exact-qualified template. The left chain has only one complete claim-backed occurrence.” (raw codepoints 2721 + 160; UTF-8 bytes 2721 + 160).
- `/transformation_report/augmentation/0/status`: “source_backed_candidate_not_verified_claim” (raw codepoints 3267 + 42; UTF-8 bytes 3267 + 42).
- `/transformation_report/limitations/3`: “No missing claim has been inserted and no unwitnessed occurrence has been counted.” (raw codepoints 4510 + 82; UTF-8 bytes 4510 + 82).

Source anchors: `left-first, left-second, right-first, right-second`; all exact current request pointers, original locators, quote hashes and spans are in JSON.

### 7: gemini31_pro / polarity / repetition 2

Operation: `strong-repeat.1e6b86d0895b0f94ba0ceed01844f44a0ae5fc0b75e2764ff620c033075dc5ff`. Output: `NORMALIZED.json/responses/7/projection/choices/0/message/content`; content SHA `41cb32678678e7f287708b4b08a7f2b5facc030908c6b7eeac36245879dc025c`. Input: `source-prepared.json/requests/7`. All three semantic labels: **pass**; mechanical: **fail** (`comparison_transformation_report_required`). Cost: `$0.0228`.

The answer explicitly extracts the missing right1-produces-right2 claim from the exact right-second quote and locator, keeping dev/en and content truth unverified. Empty modified/removed collections preserve historical input and negative permission. The extractor label model-inferred is a recorded metadata caveat; the actual support and transformation text clearly distinguish a source-backed extraction from an unsupported model-knowledge inference.

- `/diff/added/claims/0/assessment/basis/support/0/quote`: “right: action produces result; scope=dev.” (raw codepoints 861 + 41; UTF-8 bytes 861 + 41).
- `/diff/added/claims/0/open/questions/0`: “Content truth is unverified.” (raw codepoints 1411 + 28; UTF-8 bytes 1411 + 28).
- `/transformation_report/augmentations/0`: “Extracted missing claim 'right-second-claim' (right1 produces right2) based on observed evidence in the 'right-second' source text.” (raw codepoints 2503 + 131; UTF-8 bytes 2503 + 131).

Source anchors: `left-first, left-second, right-first, right-second`; all exact current request pointers, original locators, quote hashes and spans are in JSON.

## Remaining work

Independent semantic peer review and export of this eight-response supplement as dated method-evaluation claims remain pending. Start from the hash-bound JSON and unchanged preregistration; retain any reviewer disagreement rather than rewriting original answers. No new paid call is needed to review this batch. Do not start Stage 4 under the owner’s closing instruction.
