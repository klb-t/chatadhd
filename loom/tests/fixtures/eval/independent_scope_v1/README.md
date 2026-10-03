# Fresh scoped-source validation v1

Retaining repeated literal symbols within an explicitly supplied conversation
segment improves the independently tested source-to-structure comparisons.
This is a conditional projection result: it does not verify real-world
identity, infer topic boundaries, establish truth or authorize proof premises.

This validation-only extension was written after the scoped API froze and
before its first measurement. The method authors received no source examples,
labels or results. It contains 18 source cases in six composition groups,
three deliberately unrepresented thoughts, and five scope-contract probes.
Each group pairs an anchor with an equivalent in another domain and a
contrast that changes subject/predicate incidence while retaining overlapping
words. All source records in a valid scope explicitly identify conversation,
segment and turn. No similarity cutoff is fitted.

| Logical structural method | Separate formulas | Literal sharing within scope |
| --- | ---: | ---: |
| Lexical cosine | 0/6 correct | 0/6 correct |
| Role/relation bag | 2/6, 4 ties | 0/6, 6 ties |
| Directed labeled WL | 2/6, 4 ties | 5/6, 1 tie |
| Bounded structural alignment | 2/6, 4 ties | 6/6 correct |

Alignment completes all comparisons: literal sharing gives six isomorphic
positive pairs and six different contrasts, without budget exhaustion. WL's
remaining tie is retained in the report. The method can preserve a useful
relation that a local feature bag misses; this small result is not a general
proof that WL or alignment captures thought structure.

The actual text extractor produces all 60/60 annotated formulas across the
18 composition cases. This controlled subset uses simple copular conditional
syntax; it does not supersede the much lower broad-coverage result in
`independent_structure_v1/SOURCE_RESULTS.md`. The three source-attribution,
temporal-versus-causal and incomplete-branch cases remain logically
unrepresented, with zero formula proposals. All 21 projections in each mode stay
inference-ineligible, non-persistable and non-mutating.

All five scope probes behave as specified: a new turn in the same declared
segment is accepted; different segment, different conversation, missing
segment and missing conversation are rejected. This tests enforcement of
supplied boundaries, not whether the supplied boundaries are correct.

## Frozen scope

- Fixture hash:
  `3cf16c0d6acd793d49e7ebf4e161ac43f8a8cd6d61b097f310d72992908fe57e`.
- Scoped projection method:
  `c9e31b89f681977b3889c147d285cafef60b412743a8f123d24a07e8a485d915`.
- Extractor v2:
  `b88152833363f48808a8e4ab72050f6953343c0a8a3cdd91eb0883c43982fd59`.
- Structure method v2:
  `89596297f279ad50b1599d2cb125fc7e140c065c91827d21f2c11ec4021e0fb3`.
- `initial_scoped_report.json` records both modes, each perspective, all
  rankings, scope rejections, unknowns and implementation hashes.

The original v1 fixtures and labels remain unchanged. These fresh validation
results are now disclosed; future tuning against this extension is development
and requires another untouched validation set. Nothing in this experiment
reads the original synthetic examples or the real owner holdout.

From `loom/`:

```sh
python tools/eval/independent_cases.py --fixture tests/fixtures/eval/independent_scope_v1/cases.json validate
python tools/eval/independent_cases.py --fixture tests/fixtures/eval/independent_scope_v1/cases.json run-scoped --implementation tools/structure/scoped_projection.py --output /tmp/independent-scoped-report.json
```
