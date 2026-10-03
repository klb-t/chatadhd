# Initial independent candidate-graph results

Both frozen contracts preserve the manually supplied structure in this bounded
pilot. Each split has 13 accepted represented cases, two valid empty abstentions
and one rejected dangling reference. No provider was called. The result shows
that these structures can pass through the contracts; it does not show that a
model will discover the structures or choose the intended interpretation.

## Frozen implementation

| Component | SHA256 |
| --- | --- |
| Direct candidate compiler | `ade0944646c3cfc5356787612beb14657335db2067b4a705d5221b0f3810125d` |
| Candidate vocabulary | `0d02838638e6f82c4e5f1cc90e3842f152f4db0ac44d593721cddc8e6af8650d` |
| Grounded frames adapter | `bea98192009085e26fe7c161942ed4dbd682e3b3c3bb204c8e7a6b70f9b010c4` |

All were copied before evaluation. Data hashes and the one pre-run development
schema clarification are in `README.md`. No implementation, threshold, source
text, semantic label or validation case changed after outcomes were seen.

## Separate measurements

| Measurement | Development | Fresh validation |
| --- | ---: | ---: |
| A expected validity decision | 16/16 | 16/16 |
| B expected validity decision | 16/16 | 16/16 |
| Nonempty represented graphs preserve independent gold, A and B | 13/13 | 13/13 |
| Valid abstentions preserve empty graph and located unknown | 2/2 | 2/2 |
| Deliberate dangling references rejected | 1/1 | 1/1 |
| Accepted draft fields and added source provenance retained, A and B | 15/15 | 15/15 |
| A/B normalized Claim content agrees | 15/15 | 15/15 |
| All original inputs unchanged; inference/persistence gates closed | 16/16 | 16/16 |

All 32 raw source records and every supplied support span passed independent
byte/hash checks. Source packets remain available even for rejection. The
declared coverage summary is unknown on rejected bundles; it is not included
in the 15 accepted coverage-retention successes per split. Rejection does not
convert the submitted represented label into accepted representation.

The evaluator reconstructs the actual comparison graph, not just a copied
bundle: local kinds/attrs, declared roots, Claim node predicates and qualifiers,
ordered ports, typed literal values, and directed subject/object/scope incidence.
It separately verifies native-shaped compiled drafts against immutable source
gold, closed rewritten references, unique IDs, copied locators and observation
hashes. Both sides of all 13 manually labeled contrasts retain those fields.
That is a preservation result, not an additional matcher accuracy or general
semantic-equivalence score.

There are no observed preservation failures within the supplied supported
cases. The deliberate gaps remain visible: two ambiguous sources and two
unsupported sources yield valid empty outputs, and two malformed references
are rejected. A schema-valid empty graph remains abstention; it must not be
treated as a useful empty containment query. Predicate application, conditional,
negation, conjunction and quantification are exercised; free-variable semantics,
causality, counterfactual execution, broader operation families and prior-Claim
eligibility are not thereby established.

Fifteen independent evaluator guard tests pass. They cover the frozen A/B gold,
source/anchor hashes, byte versus character offsets, copied-source-only and
copied-draft-only false positives, unreplaced local references, invented
Assessment fields, lost operand order, wrong added provenance, lost roots,
missing unknown records and missing eligibility flags. The shared compiler is
never used as the gold oracle. Evaluator guards were strengthened during review;
the same frozen methods/data were rerun, without changing their outputs.

## Mechanical payload sizes

For the 26 accepted represented cases, using compact sorted-key UTF-8 JSON:

| Supplied gold payload | Minimum bytes | Median bytes | Maximum bytes |
| --- | ---: | ---: | ---: |
| A direct bundle | 4,582 | 10,254 | 24,865 |
| B anchor response | 1,550 | 2,587 | 5,205 |
| B composition response | 1,292 | 2,799 | 6,572 |
| B anchor + composition total | 2,842 | 5,228 | 11,777 |

These are the supplied structured payloads, not actual model response sizes,
tokenizer counts, provider usage or prices. They exclude source-packet input,
system prompts, second-stage input repetition, retries, alternatives not
supplied in this pilot and validation repairs. The compact encoding reduces
mechanical output size here; a two-stage model flow could still cost more or
lose recall. No model-budget feasibility conclusion follows from bytes alone.

One-call-per-case local compiler medians in the saved run were approximately
1.86 ms / 2.25 ms for A and 2.94 ms / 3.47 ms for B, development/validation.
These tiny-case timings are descriptive and exclude model calls, retrieval and
network overhead; they are not performance guarantees.

## Scope of the next experiment

The inputs already contain correct occurrence inventory, explicit scopes,
bindings and exact whole-sentence support. The validation subset includes
allowlisted native Entity references, but prior Claim arrays are empty. Context
retrieval, topic changes, live model decomposition and ambiguity resolution need
separate source-to-output evaluation. The direct and frame contracts share a
carrier/compiler, so their agreement cannot alone grade extraction quality.

This closes a controlled serialization/validation question. It supports trying
these contracts as alternative source-grounded candidate interfaces, while
keeping native promotion, world truth and proof validity separate.

## Reproduction

From the repository root, at the recorded method versions:

```sh
python -m unittest discover -s loom/tools/eval -p test_candidate_graph_eval.py -v
python loom/tools/eval/candidate_graph_eval.py --compiler loom/tools/structure/candidate_graph.py --frames loom/tools/structure/grounded_frames.py --output /tmp/candidate-graph-report.json
```

`initial_report.json` contains all per-case results, copied method output,
source/graph audits, contrast-retention records, payload sizes and timings.
Future runs with changed methods must be labeled separately.
