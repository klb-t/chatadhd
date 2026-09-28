# Independent source-text measurement

The annotated graph result does **not** survive the current text adapter.
The bounded extractor recognizes some simple sentences accurately, but its
current projections tie the cross-domain and same-vocabulary contrasts in
every tested group. Topic scoping avoids the tested false focus assignments,
while missing several explicit topic returns and references.

No method, threshold or fixture label was changed after observing these
results. These measurements use independently frozen synthetic source text.
They do not establish quality on the owner's actual exports.

## Text extraction and comparison

The results below are identical on the 50-case development split and the
50-case validation split. Source text alone was supplied to the adapter;
formal gold, candidate conclusions and labels were withheld.

| Measure | Result per split |
| --- | ---: |
| Fully parsed records | 9/50 |
| Partially parsed records | 12/50 |
| Fully abstained records | 29/50 |
| Recognized sentence envelopes | 36/80 |
| Exact gold formula matches | 36/51 |
| Formula proposals matching gold | 36/36 |
| Verified returned source spans | 80/80 |
| Strict contrast order, operation projection | 0/11 |
| Strict contrast order, logical-candidate projection | 0/11 |

The formula comparison normalizes case, bound-variable names and the adapter's
documented `property:`/`class:` prefixes. It does not translate, stem or rename
predicate meanings. Because the gold does not encode that prefix distinction,
class-versus-property typing is not measured. These exact matches concern
individual annotated formulas, not an accepted proof or an external fact.

For both projections, role/relation cosine and WL tie all eleven groups.
Alignment ties seven groups and is unavailable in four with insufficient
extracted structure. Unavailable scores are preserved as missing; they are
not zero similarity, successful abstentions or ties. The adapter deliberately
isolates formula candidates, so it does not preserve the repeated predicate
and entity relationships across premises needed for these argument contrasts.
Partial recognition also omits quantifier/negative/default structure.

| Family | Matched formulas / gold, per split |
| --- | ---: |
| Modus ponens | 6/6 |
| Affirming consequent | 6/6 |
| Implication chains | 9/9 |
| Universal instantiation | 3/6 |
| Modus tollens | 3/6 |
| Polarity | 3/6 |
| Quantifier scope | 0/3 |
| Defeasible exceptions | 6/9 |

The broader generalization/specialization, guarded-branch and unknown-operation
families each have zero recognized envelopes in this suite. The same applies
to the retrieval and philosophy conversations. Their source text and unknown
records survive. Zero grammar coverage is not evidence that those thoughts
have no structure. No extracted formula was promoted to an eligible proof
premise; end-to-end deduction validity was not measured.

## Conversation-local topic focus

Eight conversations contain 21 independently labeled local spans. Gold local
focus was written and hashed before topic-method results; it is separate from
whole-conversation membership. A conversation may be on-topic while most of
its spans concern a different subject.

| Measure | Development | Validation |
| --- | ---: | ---: |
| Correct local focus sets | 10/10 | 8/11 |
| Extra-focus spans | 0/10 | 0/11 |
| Missing-focus spans | 0/10 | 3/11 |
| Both gold boundaries present | 7/10 | 7/11 |

All 23 returned observation spans verify against their original turn bytes.
The three focus misses involve an explicit return to an earlier SDK task,
a shortened reference to a previously introduced counter, and an explicitly
selected client antecedent. The tested late unrelated technical context does
not retroactively authorize the earlier textile alias. These are small
contrast diagnostics, not general coreference or segmentation accuracy.

## Provenance and reproduction

- Source fixture v1.0.1 SHA256:
  `113421e9ff11aecdbfffa4af9f4d6e991ace3a814603d532f9428c0a5269a39a`.
- Topic gold SHA256:
  `d40d737026a826b4f71e9ac883801f244604b5abb0e950e4a555e261f528a56e`.
- Frozen extractor v1 SHA256:
  `b07d8f01656b1ff52247e0bf6eabe86a671b75d103e8f49aa9657741cf3914f0`.
- Frozen topic method SHA256:
  `1175c60d5cdf80758603231e8e8746c517fcad9d69c26cb77d301786f994de28`.
- Machine reports: `initial_extraction_report.json` and
  `initial_topics_report.json`. These retain per-category coverage, all
  abstentions, candidate source spans, comparisons and local-focus failures.
- The extractor subsequently changed metadata-only family names/version;
  these first results explicitly refer to the frozen v1 implementation.

From `loom/`, with the desired frozen implementation paths:

```sh
python tools/eval/independent_cases.py run-extraction --implementation tools/structure/extract.py --output /tmp/source-extraction-report.json
python tools/eval/independent_cases.py run-topics --implementation tools/structure/topics.py --expectations tests/fixtures/eval/independent_structure_v1/thread_expectations.json --output /tmp/source-topic-report.json
```

Inspect implementation hashes before comparing regenerated reports. The
v1.0.0 invalid-role correction and both fixture hashes are documented in
`INITIAL_RESULTS.md`; it happened before the first completed measurement.
