# Independent structure diagnostics v1

This suite was authored from the owner's R2/R6/R13/R18 requirements and the
explicit thought-structure objective, without reading `synthetic_dev` examples
or ground truth and without accessing `eval/real-holdout-key`. It is synthetic
diagnostic material, not an owner-export or historical-prediction benchmark.

`cases.json` freezes source prose, structured annotations, conclusions, splits,
and multi-axis labels under `frozen_labels_sha256`. Labels were authored before
the first run of the independent structure implementation. Each group stays in
one split; domain vocabulary is disjoint between development and validation.
Validation becomes a disclosed diagnostic set after its first measurement; it
must not be described as an untouched holdout in later tuning.

## Label protocol

Project membership, owner-philosophy evidence, conceptual usefulness, and
proof support are distinct. Selection is evaluated against an explicitly named
goal. An analogy is not project membership. A generic philosophy question is
not inherently irrelevant. Quoted third-party language is retained as such.
Ambiguous labels and missing predictions are counted separately; they are not
silently converted to negatives. The current production catalog policy is
broader than a `self_project` task, so that result alone is not catalog quality.

Formal annotations are grounded in exact UTF-8 byte spans of authored source
prose. They are **gold inputs**, not parser output. A verified deduction is
conditional on the interpretation and premises. A source utterance being
observed does not establish its external-world truth. Proposed conclusions
remain inferred, with premises and rule provenance. Unsupported, defeated and
underdetermined candidate conclusions must not be asserted as observed facts.

## Scope

- Actual alias inflections; unnamed continuation; generic philosophy questions;
  quoted third-party names; ambiguous short identifiers; cross-project drift.
- Universal instantiation, modus ponens, modus tollens, affirming the consequent,
  quantifier scope, polarity, implication chains, and defeasible exceptions.
- Generalization followed by specialization; guarded branches and unknown
  values; unknown operations whose semantics must remain unrepresented.
- Late project mentions, topic returns, ambiguous references, mid-turn resets,
  and a late context that must not authorize an earlier unrelated alias.

Structural pairs vary domain vocabulary and relation structure separately.
Every structural group has one equivalent pair and one contrasting pair.
Method comparison uses their ordering, without fitting a validation threshold.
Universality means observed recurrence across represented domains; specificity
means constraint content. Neither is labeled as the inverse of the other.
No claim is made that this small operation inventory captures all thought.

## Reproduce

From `loom/`:

```sh
python tools/eval/independent_cases.py validate
python tools/eval/independent_cases.py materialize --split development --output /tmp/loom-independent-dev
python tools/eval/independent_cases.py materialize --split validation --output /tmp/loom-independent-validation
```

Only generated `conversations.json` files are source inputs for catalog runs.
Never scan `cases.json` or this directory as an archive: that would expose gold
labels and formal annotations to the retrieval system. Current conclusions,
graph projections and source text are separate fields intentionally.

`independent_cases.py evaluate` accepts recorded per-case decisions/features
and per-pair method scores, and emits separate split/category reports. Report
the exact implementation hash, fixture hash, pack hash and ablation settings
with any measurement. Natural-language extraction coverage is a separate
measurement and must remain unavailable when no parser was run.
