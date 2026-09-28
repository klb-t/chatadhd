# First independent measurement

The frozen annotated-input methods distinguish the supplied cross-domain
patterns better than the lexical control on this small synthetic suite.
This result does not establish natural-language extraction or owner-history
performance. No production selector, weights, thresholds or gates changed.

| Method | Development: strict contrast ordering | Validation: strict contrast ordering |
| --- | ---: | ---: |
| Lexical cosine | 4/11 | 4/11 |
| Role/relation bag | 7/11, 4 ties | 7/11, 4 ties |
| Directed labeled WL | 11/11 | 11/11 |
| Bounded structural alignment | 11/11 | 11/11 |
| Semantic identity alignment (logical pairs only) | 0/8, 8 ties | 0/8, 8 ties |

Every group compares a cross-domain equivalent to a same-vocabulary relation
contrast. There is no fitted similarity cutoff. Semantic identity alignment
retains literal predicate identity, so its tied result on cross-domain
analogies is a useful distinction between identity and form, not evidence that
literal meanings should be discarded.

For each split, 12/13 entailed target conclusions were proposed. None of the
11 unsupported or defeated targets was proposed. The one miss is a universal
statement whose target constant occurs only in the question: the bounded
candidate generator instantiates constants occurring in source premises.
This is a generation-coverage limit. Other generated conclusions are listed
as unscored extras, not counted as verified or silently labeled false.

The input contains manually grounded formal annotations. Source extraction
was not executed in this measurement (0 cases); graph representability and
conditional symbolic proof are separate from interpreting conversation text.
The 18 operation cases measure relation preservation and comparison, not
execution of generalization, branching or an unknown private operation.

The development and validation groups use different domain vocabulary but
share logical templates. The validation results are now disclosed; further
tuning against them is diagnostic development, not untouched holdout scoring.
No existing synthetic development examples or real holdout key were read.

## Exact scope

- Fixture v1.0.1: 100 cases, 54 pairs, 50 cases per split.
- Full source/annotation hash:
  `113421e9ff11aecdbfffa4af9f4d6e991ace3a814603d532f9428c0a5269a39a`.
- Rejected v1.0.0 pre-correction hash:
  `a1f8655c494d082f9729a1c361d9b34ed048f4f63d49f9722ef92b4b869df496`.
- Method SHA256:
  `166322e6ddc6ffc102eee4009b3733b1e30c9bd7189534285704b094583ace60`.
- Machine results: `initial_structure_report.json`.
- Protocol checks: 9/9 Python tests pass.
- Before the first completed measurement, an invalid `source_artifact` role
  caused graph validation to abort. It was corrected to the binding model's
  `artifact` role, versioned and refrozen; no result or label was used to tune
  the correction.

An independently motivated late-contradiction soundness fix produced method v2,
SHA256 `89596297f279ad50b1599d2cb125fc7e140c065c91827d21f2c11ec4021e0fb3`.
Re-evaluation on unchanged fixtures has identical reported metrics; see
`v2_structure_report.json`. The fixture does not itself cover every late-derived
conflict arrangement; the method's own dedicated tests cover that correction.

Reproduce from `loom/`:

```sh
python tools/eval/independent_cases.py run-structure --implementation tools/structure/structure_methods.py --output /tmp/independent-structure-report.json
python -m unittest discover -s tools/eval -p test_independent_cases.py -v
```
