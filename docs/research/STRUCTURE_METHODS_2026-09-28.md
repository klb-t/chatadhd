# Structure and meaning: a measured palette, 2026-09-28

Status: **experimental proposal**, grounded in the binding conceptual model,
owner requirements R3–R13/R21 and handoff §5.1. Syllogisms are one elementary
family in a broader compositional representation; they are not a complete
theory of thought. This note records candidate methods and executable controls,
not a claim that semantic understanding is solved.

## Central decision

Keep one epistemic model, with several mathematical projections. Preserve the
source statement and its Assessment, then project its claim-bound interpretation
into a graph of operations, bindings, arguments, scopes and dependencies.
Compare that graph at several abstraction levels. A repeated pattern can be
structurally real while invalid as an inference, or valid as a rule while
inapplicable to the present evidence. Scores for these questions must remain
separate.

A useful atomic palette includes reference/predication, binding/quantification,
negation, conjunction/disjunction, condition/branch, comparison, abstraction and
specialization, part/whole composition, ordering, causation, means/end constraints,
exceptions, analogy and evidence/inference links. The experiment's data inventory
groups these into operation families with declared inputs, outputs, preconditions
and coverage. Dedicated AST execution is much narrower than graph representation.
Unrepresented text/operations are preserved and counted.

## What primary research supports

- **Argument Interchange Format** distinguishes domain content from applications
  of reasoning schemes. This motivates retaining explicit inference-application
  structure, including jointly required premises, instead of reducing an argument
  to a bag of topics. We map that distinction to Loom's Claims plus Operators and
  derivations; adopting a second AIF ontology would duplicate existing semantics.
  [Chesñevar et al., 2006](https://arg-tech.org/people/chris/publications/2006/argmas2006.pdf).
- **AMR** is a useful example of sentence-level semantic graphs and predicate
  arguments. It is not a sufficient truth-conditional foundation for this task:
  a meaning projection still needs explicit scope, modality, provenance and
  inference contracts. The research question is what abstractions preserve the
  distinctions relevant to Loom, not whether one published representation is
  universally best. [Banarescu et al., 2013](https://aclanthology.org/W13-2322/).
- **Structure mapping** emphasizes consistent relational correspondence and
  interconnected systems of relations. This motivates entity/predicate bindings
  that survive abstraction, and a distinction between literal similarity,
  recurring form and analogical transfer. An analogy proposes correspondence;
  it does not prove a new target-domain assertion.
  [Gentner, 1983](https://groups.psych.northwestern.edu/gentner/papers/Gentner83.2b.pdf).
- **MAC/FAC** supplies a useful architecture precedent: inexpensive candidate
  retrieval followed by more selective structural comparison. It motivates a
  palette/cascade, not an unsupported claim that lexical scores recover every
  structural analogy. [Forbus, Gentner and Law, 1991 conference version](https://www.qrg.northwestern.edu/papers/Files/macfac91(searchable).pdf).
- **1-WL graph kernels** repeatedly refine labels using neighborhood multisets,
  giving efficient count features for local graph structure. The paper's linear
  edge/iteration scaling relies on its implementation assumptions; the current
  simple prototype sorts neighborhoods and therefore has a sorting factor. The
  method is a retrieval feature, not a complete isomorphism decision procedure.
  [Shervashidze et al., 2011](https://www.jmlr.org/papers/v12/shervashidze11a.html).
- **Natural deduction** makes propositions, judgments and proof dependencies
  explicit; quantifier elimination requires substitution discipline. This
  supports a tiny separately verified deduction family rather than treating graph
  similarity as evidence of entailment. [Pfenning, 2017](https://www.cs.cmu.edu/~fp/courses/15317-f17/lectures/02-natded.pdf)
  and [Pfenning, 2008, quantification](https://www.cs.cmu.edu/~fp/courses/15317-f08/lectures/06-quant.pdf).

These sources motivate experiments. None establishes that the selected methods
extract the owner's actual reasoning reliably.

## Minimal representation proposal

| Existing Loom concept | Role in the experiment | Preservation requirement |
|---|---|---|
| Observation + Locator | Exact source words and span | Never replace raw content with normalized formula |
| Claim + Assessment | Source assertion, interpretation or relation | Observed assertion is not proof of its truth |
| `Qualifiers.extra` | Candidate formula annotation and extraction record | Interpretation uncertainty must remain assessed; uncertain annotation may need its own interpretation Claim |
| Operator + Derivation | Named rule application with bound inputs/output | Rule/version, premise IDs, scope and assumptions retained |
| Principle/Paradigm | Candidate repeated abstraction/template | Evidence, counterexamples, scope and validation status |
| Morphism | Conditional role-preserving correspondence | Transfer is not identity; depth one, never chain transfer |
| Expected Property | Checkable property vouched for by an inference | Exact proof replay needs a model-approved predicate + core implementation |

No schema-v4 change, new evidence class, fifteenth universal role or shadow
truth store is proposed. Formula vertices are derived graph views, not knowledge
objects with competing identities. Argument positions and inference roles are
different from the model's fourteen project-system roles.

Logical annotations currently use atoms, typed ordered arguments, explicit
negation, implication, quantifiers, conjunction/disjunction, modality and
causation. Predicate and constant symbols are shared vertices, so renaming is
consistent across a whole argument. Bound variables have explicit binder edges.
This catches the distinction between `P→Q, P` and `P→Q, Q` even when the surface
names come from unrelated topics. `causes` and `implies` are different operations.
Scopes and qualifiers are not erased to increase a score.

Abstraction is an explicit sequence, not one scalar: source wording → grounded
claim interpretation → semantic relation/argument graph → names-abstracted
operation graph → candidate recurring substructure/template. Each step records
which information it forgets. Category membership, example-of, relation
instantiation and structural analogy are separate relation types in pack data,
not aliases or merges.

## Executable comparison controls

Let `n` be vertices, `m` directed edges, `h` refinement rounds, `Δ` maximum degree,
`B` alignment-search budget, and `L` text tokens. Costs below describe this
prototype rather than unqualified theoretical best implementations.

| Method | What it preserves / ignores | Cost | Main failure case |
|---|---|---|---|
| Token-count cosine | Preserves word counts, ignores order/roles; casefold invariant | `O(L)` features, sparse dot product | Same vocabulary with reversed reasoning; cross-topic rename misses |
| Role/relation counts | Preserves labeled directed local relations/qualifiers, ignores global variable consistency | `O(n+m)` features plus serialization | Counts can agree while arguments connect differently |
| Directed edge-labeled 1-WL cosine | Preserves iterative incoming/outgoing role neighborhoods, invariant to vertex IDs/order | `O(h(n+m log Δ))` plus serialization/hash cost | Regular graphs can be indistinguishable; no proof of logical equivalence |
| Bounded labeled alignment | Preserves complete bijective incidence structure, repeated symbol bindings and roles | Factorial worst case; hard `B` expansion limit, current per-expansion edge scans | Expensive symmetries, partial analogy missed; exhaustion is unknown |

The last method is an experimental exact-match control, **not a replacement for
the production subject-anchored partial homomorphism/constraint propagation
matcher**. Runtime promotion would use indexing and constrained partial matching
only after measurements. Return a vector of method scores; do not conceal
differences behind an uncalibrated fused probability.

All methods have an explicit no-representation state. Semantic projection keeps
predicate/constant names, structural projection permits consistent renaming.
Single `R(a,b)` and `R(b,a)` share an abstract form under renaming; they remain
different semantically. Shared constraints can distinguish a role reversal even
structurally. This is a deliberate invariance choice, not automatically an error.

## Consequence candidates and safety boundary

The executable proof subset comprises universal instantiation, modus ponens,
modus tollens and conjunction elimination. Chaining is bounded and every proof
retains its original eligible Claim IDs. No inverse implication, some-to-all
generalization, causal-to-logical coercion, modal collapse, unknown-as-false,
extrapolated premise or transferred-premise chain is allowed. Direct
contradictions are reported; there is no explosion rule. Scope must match exactly.
No proof found means unknown/unsupported by the subset, not false.

Post-freeze generic correctness review found that version 1 marked newly derived
contradictions too late, allowing a downstream proposal to use them. Version 2
quarantines them before reuse and retracts dependent proposals if conflict arrives
later. Contradicted/withdrawn results remain in a separate report channel. This
fix used author-owned examples only; original independent metrics remain tied to
the frozen version 1 hash documented in the harness README, with re-evaluation
required for version 2. Free variables are unsupported by the compact grammar;
unbound term strings mean constants, so extractors must preserve or abstain on
unrepresented free-variable interpretations rather than silently grounding them.

The report's candidate conclusions are conditional on supplied logical
annotations and premises. They are not persisted Claims: a `proof_replay`
Expected-Property verifier is absent from the core. Every result explicitly has
`persistable_claim: false` and no invented confidence. Before production, add the
predicate to the binding model, implement source-aware replay, validate the
annotation/Assessment chain and calibrate semantic correctness on independent
data. A valid proof under the wrong extracted premises is still a system error.

Analogy-based transfer uses a different justification from deduction. A matching
shape can suggest a candidate Morphism and an open question. An unstated target
claim requires the existing transfer conditions, constraints, evidence classes,
Expected Property and depth limit. Generalization from examples is a candidate
Principle, not a deductive universal. New future design ideas are extrapolations,
not observations or deductive consequences.

## Universality and specificity

The initial profile deliberately reports two independent vectors. Observed
coverage counts distinct occurrences, domains and contexts, deduplicated by
occurrence ID. Retained detail counts nodes, relations and qualified fields.
Neither is a calibrated universal-validity estimate or intrinsic information
content. A precise conditional pattern can recur in many domains; a vague
statement can appear only once. More domain coverage must not automatically
erase structural detail or raise confidence.

Future estimates should account for sampling exposure and domain imbalance, and
compare description length within a declared codebook, including exceptions.
Information-theoretic compression is useful only when reconstruction fidelity
and provenance costs are measured. A geometric entailment order or concept
lattice is directional; symmetric cosine cannot represent specialization by
itself. Topological signatures discard labels/semantics and need a justified
filtration; they belong in a later ablation after grounded graph baselines.

## Evaluation and promotion plan

1. Author-owned invariance/mechanism checks: rename and statement-order
   invariance; binder alpha-equivalence; argument identity, polarity, quantifier,
   scope and modality preservation; invalid inverse steps; explicit WL collision;
   bounded-search unknown state; no false persistence/confidence.
2. Freeze implementation and independently authored validation data before
   scoring. Keep structured-gold pattern/inference metrics separate from
   natural-language extraction coverage and end-to-end quality. Never inspect or
   tune on `eval/real-holdout-key` or existing independent validation fixtures.
3. Report per-relation retrieval/ranking, rejected near-misses, proof candidate
   precision/coverage, provenance completeness, false-certainty rate, unsupported
   operation rate, runtime and memory. A proof's conditional validity is not
   calibration. A graph match is not the same project, philosophy, analogy,
   entailment or applicable policy; measure those axes independently.
4. On genuine owner data, use judged source spans and timestamp-faithful packs;
   recurrence can search backward, prediction must freeze at the historical cut.
   Current contaminated temporal benchmark remains unavailable.
5. Only promote measured, source-grounded gains. A C++ adapter should consume the
   same claim projection contract, use fixed local label interning/hash behavior,
   preserve deterministic provenance and limits, and pass cross-language golden
   projection/proof parity. No public ABI or core schema change belongs to this
   experimental checkpoint.

Implementation: `loom/tools/structure/structure_methods.py`, operation inventory
`operations.json`, and author-owned mechanism checks. Independent measurements
belong in the evaluator's separate report; no independent quality score is
claimed here.
