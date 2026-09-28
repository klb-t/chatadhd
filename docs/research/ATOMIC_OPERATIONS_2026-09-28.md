# Atomic operations for thought structures — research proposal, 2026-09-28

Status: **open candidate repertoire and executable coverage plan**, not a claim
that human thought has been exhaustively decomposed. This document proposes
research annotations over the binding
[Loom conceptual model](../architecture/LOOM_CONCEPTUAL_MODEL.md); it does not
replace Claim, Assessment, Principle, Operator, Morphism, Area, or universal
role. The initial implementation is isolated in `loom/tools/structure/` and
does not change the core schema or the C++/Python contract.

The owner's requirement is broader than syllogisms: represent the small
operations that compose arguments, explanations, questions, plans and changes
of perspective; discover reusable structures across topics; and compare those
structures mathematically. Generalization, specialization and guarded branches
must be explicit. The repertoire must also describe how a conversation changes
topic, introduces a project late, connects new material to existing knowledge,
and grows or simplifies that knowledge without losing its history.

## 1. What is atomic here?

An operation is *atomic relative to a declared representation and task*: its
contract is useful without opening its implementation. It is not necessarily
psychologically indivisible or uniquely primitive. Conjunction could be
encoded using other connectives in some logics; retaining it still helps
extraction, explanation and comparison. A different method may choose another
basis and publish a translation with its information losses.

Keep four kinds of objects distinct:

| Kind | What it does | What it does not establish |
|---|---|---|
| Structural constructor or transformation | Builds or changes a typed description: negation, a guard, part structure, abstraction, role alignment | That the described proposition is true, that a rule applies, or that a similar target has the source's properties |
| Inference rule application | Produces a conclusion from eligible premises under an explicit logic, rule version and scope; records a replayable proof or other warrant | That the premises are true merely because the rule is valid |
| Epistemic assessment | Records evidence class, origin, confidence, dependencies, counterevidence, consequences and open questions | A new structural relation or an inference rule; a score is not a proof |
| Analysis/data transformation | Segments, focuses, links, revises or projects stored representations, with a change record | Deletion of source history, an identity merge, or acceptance of a hypothesis without its own grounds |

The existing **Operator** remains a reusable situation-to-solution
transformation with justification, including inference operators. A research
operation identifier is an annotation vocabulary entry; it must not silently
become a new executable Operator opcode. Under MEGA MASTER §2.B–E, new values
within a supported contract may be data, but a new execution semantics needs a
validator and an implementation. Core promotion therefore follows a conceptual
model amendment and a reviewed adapter, rather than treating arbitrary JSON as
executable semantics.

## 2. A compositional carrier shared by several perspectives

Use an annotated, typed expression graph as an interchange candidate. The
logical expression tree is one projection; a directed incidence graph is
another. An n-ary operation is a node with named input/output ports, so argument
order, repeated references and nesting survive. Bindings use binder references,
not accidental equality of printed variable names. A proposition can be an
argument of another expression: `believes(agent, P)` is different from `P`.

An annotation should carry:

- Operation id and vocabulary version; typed inputs, output and named ports.
- The entity/claim ids it describes, exact observation locators where present,
  source and parser hashes, and annotation origin/method.
- Scope: project, branch, version, time, speaker/perspective, hypothetical
  world, and variable environment where applicable.
- Preserved constraints, omitted information, unsupported components, and a
  map back to the original representation.
- Alternative parses or mappings, rather than a forced single reading.
- Separate status for representation, semantic validation, proof execution,
  evidence grounding, and eligibility for persistence as an assessed claim.

This is a contract proposal, not the current JSON schema. Current prototype
fields and exact accepted syntax are documented by
`loom/tools/structure/README.md`, `operations.json` and the validator.

An observation may establish that a speaker *said* a conditional. It does not
establish the conditional's real-world truth. Likewise, a deterministic graph
projection can be `derived` from an annotation while the annotation itself is
an uncertain interpretation. Do not launder the interpretation into observed
knowledge by putting a deterministic transformation after it.

### Grounding and loss notation for the repertoire

Every row below has the common precondition of well-typed ports and explicit
scope. **O** means a located observation can ground a description of what was
said. **D** means the result needs the identified inputs and a reproducible
transformation. **H** means a proposed interpretation/model/extension needs
premises, alternatives and counterexamples; it is not a fact. Any new inferred
Loom claim also requires its ordinary Assessment and implemented Expected
Property. These letters are research shorthand, not new evidence classes.

**R** means the operation can be reversed from its full structured result;
**M** means reversal additionally needs a stored mapping/delta; **L** means the
result alone discards information. All operations preserve the raw source and
prior state. This is a recoverability description, not permission to run an
inference backwards. Unless a rule is named separately, a row licenses
construction and comparison only.

## 3. Candidate repertoire and contracts

These families are overlapping lenses, not a closed classification of thought.
The candidate identifiers below are more fine-grained than the initial
executable inventory. An unknown operation remains representable as unknown.

### 3.1 Reference, scope and logical composition

| Candidate operation | Inputs → output | Preconditions or required distinction | Recovery; grounding | Allowed result / prohibited shortcut |
|---|---|---|---|---|
| `refer` | source mention + candidate identities → reference with alternatives | Identity evidence; distinguish a named individual, a class, and a variable | M; O/H | Link a mention; similar labels do not prove identity |
| `predicate` | relation/property + ordered arguments → proposition | Arity, argument types and units where relevant | R; O/D/H | Represent a relation; no automatic assertion of its truth |
| `bind_substitute` | expression + binder + term map → substituted expression | Capture avoidance; distinguish free/bound occurrences; type compatibility | M; D | Rename or instantiate syntax; truth needs an eligible inference rule |
| `classify` | item + category + membership criterion → membership candidate | Separate instance-of, subclass-of and part-of | M; O/H | Propose/test membership; category labels do not transfer every property |
| `quantify` | binder + domain + body + quantifier/cardinality → quantified expression | Preserve domain, restriction, witness and quantifier scope | R; O/D | Express all/some/exactly-k; observed examples do not prove all |
| `negate` | proposition → scoped negative proposition | Explicit versus inferred negation; selected logic | R; O/D | Negate the stated scope; missing evidence is not a negative proposition |
| `conjoin` | propositions → conjunction | All operands retained under compatible scope | R; O/D | Represent all-of; discard none during storage |
| `alternative` | propositions + inclusive/exclusive mode → alternatives | Inclusive disjunction, exclusive choice and unresolved interpretations differ | R; O/D/H | Represent possibilities; do not choose a branch from uncertainty |
| `conditional` | antecedent + consequent + conditional kind → guarded relation | Strict implication, causal, default, normative and counterfactual kinds are distinct | R; O/H | Preserve direction; no converse, contraposition or material-implication rewrite unless the selected logic permits it |
| `case_split` | context + guarded branches + optional remainder → branch structure | Guard overlap/exhaustiveness explicit; branch-local assumptions kept local | R; O/D/H | Analyze alternatives; merge a common conclusion only with an applicable case rule and established coverage |
| `modalize` | mode + perspective/world + proposition → modal expression | Possibility, necessity, belief, obligation and permission have separate semantics | R; O/H | Record the mode; belief(P), should(P) and possible(P) do not imply P |
| `scope` | expression + time/project/world/speaker qualifiers → qualified expression | Scope inheritance and overrides explicit | R; O/D/H | Restrict interpretation; do not drop qualifiers when comparing or inferring |

### 3.2 Abstraction, structure and measurement

| Candidate operation | Inputs → output | Preconditions or required distinction | Recovery; grounding | Allowed result / prohibited shortcut |
|---|---|---|---|---|
| `generalize` | examples/expressions + chosen generality relation → schema, abstraction map and residuals | State whether this is variable abstraction, category ascent, constraint weakening or empirical induction | M/L; D/H | Find common structure; no universal truth from recurrence alone |
| `specialize` | schema + bindings + added conditions → constrained expression/instance | Consistent substitutions; inherited constraints preserved | M; D/H | Build/test an instance; truth transfers only under a separately validated entailment/instantiation rule |
| `compare` | alternatives + dimensions/scales/context → comparison vector or partial order | Comparable units and direction of each criterion; missing values stay missing | L unless inputs retained; D/H | Report agreement/difference per dimension; do not invent one total ranking |
| `align` | source/target structures + candidate role/relation maps → partial correspondence and unmatched parts | Preserve declared ports, bindings, relation direction and selected constraints | M; D/H | Produce analogy/morphism candidates; correspondence is not identity or target truth |
| `decompose` | whole + decomposition criterion → parts and interfaces | Distinguish exhaustive partition, overlapping cover and selected aspects | M; O/D/H | Expose structure; parts alone need not explain whole behavior |
| `compose` | components + interfaces/arrangement/conditions → whole model | Composition rule, compatibility, interactions and ordering declared | M; D/H | Form a candidate whole; do not assume each part's property lifts to it |
| `organize` | members + grouping/order criterion → set, hierarchy or arrangement | Membership, hierarchy edges and sequence are different relations | M/L; D/H | Provide a view; a hierarchy is not automatically a causal graph |
| `measure` | target + operational definition + procedure/unit → value or interval | Instrument/procedure provenance, unit, uncertainty and missingness | L; O/D | Report a measurement; ordinal ranks are not automatically distances |
| `aggregate` | members + weights + function + inclusion rule → aggregate and lineage | Units, sampling frame, dependency/deduplication and null handling | L; D | Compute a specified summary; preserve dissent and avoid treating repeated copies as independent support |
| `order` | events/intervals + temporal constraints → partial temporal order | Event time versus recording time; uncertain intervals; branch chronology | M; O/D/H | Infer order only under declared interval rules; before does not imply causes |

### 3.3 Explanation, exceptions, intent and evidence

| Candidate operation | Inputs → output | Preconditions or required distinction | Recovery; grounding | Allowed result / prohibited shortcut |
|---|---|---|---|---|
| `cause` | proposed cause/effect + mechanism/model + conditions → causal claim structure | Distinguish association, mechanism, sufficiency and intervention assumptions | M; O/H | Describe/test causal hypotheses; no causality from temporal order or graph reachability alone |
| `intervene` | causal model + intervention target/value → modified model and predicted outcomes | Intervention semantics and identification assumptions supplied | M/L; D/H | Evaluate the explicit model; conditioning on X is not doing X |
| `counterfactual` | factual evidence + model + alternative action/world → conditional alternative outcome | Shared background assumptions; world identity; model adequacy | M/L; D/H | Produce a model-relative claim; no factual observation of the unrealized outcome |
| `default` | applicability + expected conclusion + defeat/priority policy → defeasible rule | Open/closed-world policy explicit; do not silently use strict implication | R; O/H | Represent a defeasible expectation; silence does not establish absence of exceptions |
| `exception` | base rule + exceptional condition + effect/priority → restricted or defeated application | Exception concerns applicability, outcome, priority, or scope; distinguish these | M; O/D/H | Block/refine an application with grounds; one exception does not erase unrelated cases |
| `goal` | agent/project + desired state + criterion → goal structure | Desired versus observed state; time and stakeholder explicit | R; O/H | Represent intent; goals are not forecasts |
| `means` | goal + action/plan + transition assumptions → candidate means-end link | Necessary, sufficient, enabling and merely helpful means differ | M; O/H | Compare conditional plans; intended means do not prove effectiveness |
| `constrain` | object/plan + condition + hardness/scope → constrained candidate | Hard constraint, preference, norm and physical limitation distinguished | M; O/D/H | Filter under the stated rule; satisfying a constraint does not make a candidate preferred |
| `prefer` | alternatives + affected values + ordering policy → contextual preference/trade-off | Keep value dimensions and unresolved incomparability | L unless rationale retained; O/H | Support a decision under a policy; no universal scalar utility is implied |
| `evidence_link` | observation/argument + target claim + support/attack kind → evidential relation | Quote verification, relevance, direction and dependence tracked | R; O/D/H | Record support, rebuttal or an undercutting challenge to a warrant; support edges alone do not prove a claim |
| `question` | topic + requested variable/property + constraints → open issue and answer type | Distinguish a presupposition from an established premise | R; O/H | Define what would answer the question; asking why P does not establish P |

### 3.4 Conversation attention and graph maintenance

These are analysis/data operations over the same model, not additional logical
connectives. Each returns a proposed change or view with provenance. Their
outputs can themselves be assessed claims where they assert something about
the source or project.

| Candidate operation | Inputs → output | Preconditions or required distinction | Recovery; grounding | Allowed result / prohibited shortcut |
|---|---|---|---|---|
| `segment` | ordered turns/spans + boundary hypothesis → overlapping or nested topic spans | Preserve original order, quotations and attachments; allow mixed-topic turns | M; O/D/H | Propose boundaries; a topic shift is not necessarily a new project |
| `focus` | current goal/turn + candidate spans/graph regions → local attention weights and selected context | Record goal, budget and selection reasons; keep multiple active threads | L view, full state retained; D/H | Allocate attention locally; omission from context is not rejection or deletion |
| `contextualize` | new span/claim + existing graph + relation candidates → qualified attachment alternatives | Distinguish same project, analogous structure, dependency, correction and mere mention | M; H | Connect with evidence and explicit unknowns; no attachment to the largest/first project by default |
| `append` | current state + new source-backed object/event → new version plus added facts about the record | Immutable ids, deduplication policy and provenance | M; O/D | Grow the record; do not infer that a later utterance automatically supersedes an earlier one |
| `revise` | old claim + correction/evidence + scope → supersession/contestation event and successor candidate | Explicit correction or justified change; branch/time scope retained | M; O/D/H | Update active interpretation with history; do not overwrite the old claim or erase alternatives |
| `merge` | identity candidates + identity evidence + conflict checks → proposed alias/identity mapping | Preserve lineage as lineage, not identity; allow reversal and owner judgements | M; H | Consolidate established identity; analogy, co-reference guesses and shared roles are insufficient |
| `refine` | coarse node/claim + distinctions/new data → more specific linked representation | Refinement map, conservation of supported content, and explicit unresolved parts | M; D/H | Split/add distinctions without silently strengthening unsupported claims |
| `simplify` | graph/expression + equivalence or abstraction policy → compact view, mapping and residuals | Declare whether equivalence is proven, assumed, or intentionally lossy | M/L; D/H | Reduce redundancy for a task; do not delete exceptions, dissent, scope or provenance |
| `unknown` | source span/partial structure + missing contract → opaque node with known ports and open questions | Retain raw bytes and reason: ambiguous, unsupported, incomplete, or budget-exhausted | R to raw source; O/D | Report partial coverage and abstain; unknown is neither false nor a wildcard matching everything |

The repertoire is deliberately expandable. A new operation first enters with
its typed contract, examples, anti-examples and `representation_only` status.
It is promoted separately for parsing, validation, projection, proof execution
and persistence. These capabilities need not arrive together.

## 4. Generalization and specialization are not one slider

At least four processes must be reported separately:

1. **Term abstraction:** replace selected individuals with variables while
   retaining repeated-reference constraints. For example, `depends(a,b)` and
   `depends(c,d)` can share `depends(X,Y)`. A template with variables is not an
   assertion that every pair depends on each other.
2. **Conceptual ascent:** classify an instance under a category, then a wider
   category, with membership/subclass evidence. This differs from deleting
   the instance's identity or assuming all category members are identical.
3. **Constraint abstraction:** weaken a specification under a declared
   semantics. If `[[S]]` denotes its satisfying models, `S` is more specific
   than `G` when `[[S]] ⊆ [[G]]`; strict inclusion loses distinctions. Replacing
   `P ∧ Q` by `P` is one such weakening in ordinary classical semantics.
4. **Empirical induction:** propose a reusable relation or principle from
   examples. Its support, exceptions and out-of-sample performance are separate
   from the structural fact that examples match a schema.

The direction of an English phrase such as “more general rule” can differ
between these processes. `∀x(A(x) → B(x))` and its application to a larger
antecedent class change the scope of a universal claim; that is not the same
operation as weakening an arbitrary formula. Always declare the order being
used. Plotkin's anti-unification work provides a precise precedent for a
particular symbolic generalization relation, not a general license for
inductive truth [1].

Specialization substitutes parameters, narrows categories or adds constraints.
It is not generally an inverse: from a coarse representation there are often
many valid refinements. Store the residual and map if round-trip reconstruction
matters. Abstract interpretation supplies an established example of declaring
abstraction/concretization relationships and the property being preserved; a
Loom abstraction would need its own soundness contract [2].

### Universality and specificity remain independent

For a pattern `p`, report a vector rather than `specificity = 1 − universality`:

- **Observed spread:** number of independently labeled domains, projects,
  authors and time intervals with a qualified match; denominators and sample
  selection disclosed. A domain count is not a claim about all possible domains.
- **Transfer performance:** accepted matches, false transfers and abstentions
  on held-out domains, with uncertainty and sample counts.
- **Structural content:** retained relations, bindings, guards, quantifiers,
  constraints, exceptions and composition depth, alongside the raw graph size.
  A size count is only a proxy; duplicated tautologies must not increase it.
- **Semantic specificity:** where a formal semantics exists, compare admitted
  model sets by inclusion or another declared generality order. Otherwise mark
  this coordinate unmeasured instead of replacing it with node count.
- **Compression at fidelity:** bytes/tokens or description length for the
  pattern plus bindings, exceptions and residuals, compared with the originals.
  Compression without faithful reconstruction is not evidence of understanding.

A highly detailed protocol can recur in several domains; a vague relation may
be observed in only one. A test set should include all four combinations of
high/low measured spread and high/low structural detail. Avoid relabeling each
example as a new domain to inflate spread, and avoid claiming calibrated
probabilities from uncalibrated similarity scores.

## 5. Branches, inference and epistemic boundaries

Branching has at least three readings: a proposition conditional on a guard, a
plan selecting actions, and an analyst retaining several interpretations. Keep
the reading in the representation. Preserve the order of priority when guards
overlap and retain an explicit residual branch when coverage is incomplete.

`P → Q` does not yield `Q → P`. With strict material implication, `P → Q` and
`¬Q` support modus tollens; the same syntax-looking move is not licensed for a
default such as “normally, P leads to Q.” `unknown(P)` supplies neither `P` nor
`¬P`. Branch-local conclusions stay conditional until their guards are
established. Classical proof by cases needs exhaustive premises and a common
conclusion, not just a UI showing two options.

Syllogisms are compositions within this larger space. A simple universal
syllogism can be represented by quantified predicates, substitution, and
strict implication chaining. That family does not cover analogy, default
reasoning, planning, perspective, or causal intervention.

The initial proof module executes only the following bounded rules; recording
other structures does not execute their semantics:

| Rule | Inputs → output | Required warrant | Not licensed |
|---|---|---|---|
| Universal instantiation | `∀x P(x)`, eligible existing term `a` → `P(a)` | Compatible scope and capture-avoiding substitution | Inventing a witness from an existential; generalizing observations to all x |
| Modus ponens | `P → Q`, `P` → `Q` | Strict implication; exactly matching scoped antecedent | Affirming the consequent; treating causal/default/modal relations as strict implication |
| Modus tollens | `P → Q`, `¬Q` → `¬P` | Strict implication and compatible logic/scope | Denying the antecedent; contraposing a defeasible expectation |
| Conjunction elimination | `P ∧ Q` → `P` and separately `Q` | Explicit conjuncts and eligible premises | Projecting from a disjunction; erasing source conjuncts |

Executing a valid proof over supplied annotations is a test of that proof
procedure, not end-to-end extraction quality. The current prototype reports
candidate conclusions with `persistable_claim=false`: its proposed
`proof_replay` Expected Property is not implemented in the core's closed
predicate set. Production admission must provide a real checker, preserve the
proof and premises, apply assessment policy, and honor the existing transfer
depth and extrapolated-premise restrictions.

## 6. Compositions across non-identical domains

The examples below are fictional specifications, not factual claims about a
real project, an experiment or the owner. Their point is to expose both common
structure and differences that prevent invalid transfer.

| Domain and local statement | Composition | Shared structure / retained distinction |
|---|---|---|
| Software archive: “For each retained source, if its hash verifies, permit the replay check; if it fails, preserve the source and open an issue.” | Quantification → guarded branches → goal/means/constraint → evidence-linked check | Guarded verification resembles many workflows. A hash check verifies bytes under its contract, not semantic correctness of the source. |
| Film workshop: “Normally preserve the established chronology; for an unreliable narrator, permit a deliberate contradiction and record the reveal that explains it.” | Default → exception → temporal order → goal → constraint | Shares controlled exceptions with software compatibility policy, but artistic permission is not a strict universal implication or a factual causal claim. |
| Fictional sensor study: “The two settings correlate with the reading. Compare a model with a common cause and a model with a direct effect; propose an intervention that distinguishes them.” | Measurement → comparison → competing causal models → counterfactual prediction → evidence plan | Shares alternatives and discriminating checks with debugging. Correlation and the mere availability of a graph path do not select a causal model. |
| Community exhibition: “Select an accessible venue; compare two venues on capacity, quiet space and cost. If both violate a required access condition, search again.” | Goal → decompose criteria → compare vector → constrain → guarded plan → open question | Shares constraints and branches with project planning. Preferences stay multidimensional; an apparently cheaper option need not dominate. |

An experiment should also compose operations *within* each example: generalize
the guarded check, specialize it into another project, compare the resulting
guards, and retain unmatched mechanisms and values. Mapping `check` to `check`
is insufficient evidence of a valid full transfer. Gentner's structure-mapping
account motivates attention to connected relations and distinguishes analogy
from surface attribute overlap; its use here is a design inspiration, not a
validation of this implementation [3].

### A conversation that introduces a project late

Suppose turns 1–8 discuss the archive app. Turn 9 says, “Also, the exhibition
opens in November; we need an accessible venue.” Turn 10 returns to the app's
importer, and turn 11 says the exhibition needs an audio guide.

1. `segment` proposes a new local thread at turn 9 while preserving the ordered
   conversation and alternatives for mixed-topic spans.
2. `focus` attends to the exhibition request, retrieving local goals and venue
   constraints; it does not require a fully resolved project identity first.
3. `contextualize` searches existing projects for an exhibition candidate and
   checks references. If none is grounded, it keeps a provisional subject and
   an identity question. It does not attach the venue to the app by inertia.
4. `append` records the turn-9 observations and qualified claims. The focus
   returns to the app at turn 10 without moving or deleting exhibition data.
5. Turn 11 may `refine` the exhibition's outputs and `align` its audio-guide
   requirements with audio capabilities elsewhere. An analogy does not merge
   the exhibition with the app or make the app's capabilities already present
   in the exhibition.
6. If later text establishes that two exhibition names denote one project,
   `merge` proposes an identity mapping with its evidence. `simplify` can show a
   compact project view while preserving both names, locators, old groupings,
   decisions and any dissent. A correction creates a `revise` event.

The attention window is dynamic; identity, truth and provenance have separate
lifecycles. This avoids interpreting an entire conversation under its opening
topic, while still allowing genuine multi-project claims and shared foundations.

## 7. Mathematical comparisons need preservation contracts

Keep several projections available and expose what each forgets:

| Perspective | Comparable object and method | Preserves or measures | Cannot certify |
|---|---|---|---|
| Lexical/geometric control | Token vectors now; optional grounded embedding adapter later | Surface/content proximity under the selected representation | Argument validity, identity, direction, scope, or causal truth |
| Logical | Typed scoped AST; alpha-renaming; bounded proof replay | Bindings, connective scope and consequences within the supported rule fragment | Complete natural-language meaning, arbitrary entailment, or premise truth |
| Graph | Directed typed incidence graph; role/relation features; WL features; bounded exact alignment | Declared node/edge/port constraints at each method's resolution | Logical equivalence from an approximate score; nonmatch after search-budget exhaustion |
| Algebraic/abstraction | Declared generality order, substitution/refinement maps, composable typed transforms | Direction and composability of a specified abstraction | A universal lattice or reversible transformation without proof |
| Temporal/process | Partial orders and guarded state changes | Sequence, local assumptions, branch lineage and recurrence | Causality from order or an unobserved complete process |
| Information/compression | Pattern + substitutions + residuals + exceptions + provenance | Storage/context economy at a specified reconstruction fidelity | Truth or universality from compressibility alone |
| Topological, candidate only | Explicit graph-to-complex construction and filtration | Chosen shape properties across a declared scale | Semantic equivalence; a directed labeled argument cannot be reduced without reporting discarded information |

The prototype's graph match concerns its *projection*, not isomorphism of
unrestricted real-world meaning. A bijective exact graph alignment and a
many-to-one homomorphism are different contracts. Symbol-renaming structural
comparison intentionally abstracts domain content; the semantic projection
retains symbols. Neither authorizes an identity merge. Logic-independent
translations have been studied formally through institutions, including
conditions on satisfaction preservation; the relevance here is to require a
translation contract, not to claim that Loom implements institution theory [4].

For partial representations, report `(score, coverage, unmatched, uncertainty)`.
A matched supported fragment beside an unknown fragment is not a complete
match. Budget exhaustion, unsupported syntax and missing evidence require
separate statuses. Do not fill an unknown subtree with whichever symbol makes
the similarity highest.

## 8. Coverage experiment: executable now, broader admission later

### 8.1 Capability ledger before scores

The initial implementation snapshot has nine formula constructors (`atom`,
`not`, `implies`, `causes`, `and`, `or`, `forall`, `exists`, `modal`) and the four
proof rules in §5. Its inventory currently has 21 entries across 11 families:
four executable rules, nine structural operations, and eight conversation/data
operations. The latter two groups have representation/comparison support only.
Inventory extensions should be read from `operations.json`; the machine-readable
implementation field, not a prose count, is authoritative.

The broad candidate names map to that inventory explicitly: `generalize` →
`generalization`, `specialize` → `specialization`, `compare` → `comparison`,
`aggregate` → `aggregation`, `align` → the `analogy` comparison capability,
`conditional`/`case_split` → the `conditional_branch` family, and goal/means/
constraint compositions → `means_end`. This is a family correspondence, not a
claim that a family entry validates every primitive's complete semantics.
The conversation/data identifiers retain the same names. Other candidate names
remain proposed vocabulary or supported formula syntax, as documented by the
validator; they are not silently aliases of executable rules.

Dedicated AST support, a generic graph label, and executable semantics are
different levels of coverage. For example, a labeled `exception` edge can be
compared structurally without an exception calculus. Unsupported natural
language is not parsed by this prototype. The broad repertoire above is a
contract backlog with concrete cases, not a report that all its operations run.

From the repository root, these commands inspect the live capability inventory
and run author-owned mechanism checks without reading the independent corpus:

```sh
python loom/tools/structure/structure_methods.py registry
python -m unittest discover -s loom/tools/structure -p 'test_structure_methods.py' -v
```

The CLI's `compare` and `infer` commands accept the supplied-annotation records
specified in its README. The independent evaluation supplies separately owned
cases and report commands. Parser/extraction evaluations must retain their own
operation labels: a source form such as “all X are Y” is universal inclusion,
not evidence that the speaker performed empirical induction; “X is a kind of Y”
is a subtype assertion, not evidence that a specialization process occurred.

### 8.2 Separate experiments and denominators

1. **Representation unit experiment:** supply explicit annotations; verify
   binding, scope, direction and raw grounding. Test benign renamings/reordering
   against meaning-changing mutations. This measures the representation and
   comparison machinery, not extraction.
2. **Structural retrieval experiment:** compare lexical, role/relation, WL and
   bounded alignment methods on cross-domain positives plus same-vocabulary
   negatives. Record ranking quality, mapping validity, coverage, abstention,
   budget and runtime. Keep method vectors before any optional policy-weighted
   combination.
3. **Inference experiment:** evaluate the supported strict fragment using
   eligible premises and independently labeled requested conclusions. Track
   soundness failures, valid conclusions missed, blocked/unknown cases and
   false evidence promotion separately. Do not count every blocked unsupported
   case as a correct theorem decision.
4. **Repertoire coverage experiment:** annotate open text from multiple
   fictional authors and domains, including plans, explanations, questions,
   exceptions, metaphor and mixed-topic conversations. For every source span,
   mark representable, ambiguous, partial or unsupported, and which operation
   composition it needs. Add a new primitive only when a useful distinction
   cannot be expressed faithfully by an existing composition.
5. **Conversation update experiment:** introduce a new project after a long
   established topic, return to the first, then add shared requirements and a
   correction. Measure topic-boundary quality, local focus, project attachment
   precision/recall, recovery of prior threads, preservation of both sides of a
   correction, and provenance-preserving merge/split replay.
6. **Integrated experiment, later gate:** run extraction from raw exports
   through annotations, retrieval/inference and context selection. Report every
   stage separately as well as end-to-end results; supplied-annotation success
   cannot substitute for this gate.

The structure and independent-evaluation modules supply the executable command
and versioned input/output contract. This document records no fabricated score;
actual runs belong in a report with source, pack, annotation and tool hashes,
exact command, counts and errors. The existing real temporal-holdout answer key
remains unread for development. Retrospective synthetic structure tests do not
establish prospective prediction of the owner.

### 8.3 Required challenge matrix

| Dimension | Positive/invariance cases | Required hard contrast |
|---|---|---|
| Bindings and identity | Consistent renaming of bound variables, predicates and domain names | Inconsistent reference reuse; swapping argument positions; variable capture |
| Quantification | Same quantifier and domain under renaming | All versus some; exactly-one versus at-least-one; changed domain |
| Negation and scope | Equivalent printed names with same nesting | `not(all P)` versus `all(not P)`; negative claim versus absent claim |
| Conditions | Same guarded plan in distinct topics | Converse; negated guard; missing else; overlapping guards; default versus strict rule |
| Structure and composition | Reordered conjunction; same typed parts/interfaces | Whole-to-part fallacy; lost shared dependency; a multistep relation replaced by a bag of labels |
| Generality | Recoverable schema with consistent substitutions | Induction asserted as universal truth; dropped exception; specialization violating inherited constraint |
| Causality and time | Same model-relative intervention structure | Before versus causes; association versus intervention; counterfactual versus observed outcome |
| Goals and values | Same goal/constraint arrangement in different domains | Necessary versus sufficient means; objective versus preference; unknown criterion treated as satisfied |
| Evidence and perspective | Same support/attack role structure with different content | Speaker believes P versus P; supports P versus proves P; repeated copies counted as independent |
| Partiality and context | Known fragment matches with unknown remainder shown | Unknown matches everything; empty graph gets a perfect score; new project attached to old topic automatically |

Hold out domains, composition combinations, authors and paraphrase styles,
rather than randomly splitting near-duplicate templates. Keep an independent
author's challenge contents separate from implementation development. Freeze
the vocabulary/version before that run; newly discovered gaps go into the next
version, with the old score retained.

Coverage is at least four numbers: source spans addressed, required operations
represented, compositions preserved, and supported inferences executed.
Report numerator/denominator for each, macro-averages by family/domain, and the
distribution of unsupported reasons. An average dominated by simple
conjunctions can hide complete failure on counterfactuals or topic shifts.

Proposed admission gates are qualitative until sample sizes justify numerical
targets: no known false promotion of inferred content to observed; no known
invalid derivation in the supported fragment; all unsupported/budget-exhausted
cases explicitly marked; source round-trip and update history preserved; and
measured cross-domain benefit over content-only controls without hiding
coverage loss. A finite passing test set establishes only its measured scope.

## 9. Grounded research references and limits

The repertoire and evaluation design are proposals made here. The following
primary sources motivate specific distinctions; none proves its completeness.
Links checked 2026-09-28.

1. Gordon D. Plotkin, *A Further Note on Inductive Generalization* (1971),
   Machine Intelligence 6, 101–124.
   [Author-hosted paper](https://homepages.inf.ed.ac.uk/gdp/publications/MI6_further_note.pdf).
   Provides a formal precedent for computing common symbolic generalizations
   under a specified relation. It does not equate a shared pattern with a true
   universal empirical law.
2. Patrick Cousot and Radhia Cousot, *Abstract Interpretation: A Unified
   Lattice Model for Static Analysis of Programs by Construction or
   Approximation of Fixpoints* (1977), POPL, 238–252.
   [Author publication page and paper](https://www.di.ens.fr/~cousot/COUSOTpapers/POPL77.shtml).
   Motivates stating what an abstraction approximates and preserves. Applying
   this discipline to thought annotations is our proposed adaptation.
3. Dedre Gentner, *Structure-Mapping: A Theoretical Framework for Analogy*
   (1983), Cognitive Science 7, 155–170.
   [Author-hosted paper](https://groups.psych.northwestern.edu/gentner/papers/Gentner83.2b.pdf).
   Distinguishes relational mapping from superficial attribute overlap and
   emphasizes connected systems of relations.
4. Joseph A. Goguen and Rod M. Burstall, *Institutions: Abstract Model Theory
   for Specification and Programming* (1992), Journal of the ACM 39, 95–146;
   [author-institution technical report](https://publish.lfcs.inf.ed.ac.uk/reports/90/ECS-LFCS-90-106/)
   (1990). Motivates explicit semantics-preservation conditions between
   representations; the prototype does not implement the theory.
5. Judea Pearl, *Causal Inference* (2010), Proceedings of Machine Learning
   Research 6, 39–58.
   [Proceedings paper](https://proceedings.mlr.press/v6/pearl10a.html).
   Gives a structural causal model account of causal and counterfactual
   reasoning. It supports keeping model assumptions, intervention and
   observation distinct; a causal label in our graph is not such a model.
6. Phan Minh Dung, *On the Acceptability of Arguments and Its Fundamental Role
   in Nonmonotonic Reasoning, Logic Programming and n-Person Games* (1995),
   Artificial Intelligence 77, 321–357.
   [Paper](https://cse-robotics.engr.tamu.edu/dshell/cs631/papers/dung95acceptability.pdf).
   Supplies a formal treatment of argument acceptability under attack relations.
   Our support/attack annotations do not yet implement its semantics, and
   acceptability must not be confused with factual correctness.

Next useful expansion is driven by failed cases: social perspective and speech
acts, norm conflicts, probabilistic dependence, spatial relations, continuous
processes, recursion and self-reference, metaphor, and genuinely multimodal
structure. Some may be compositions or parameterized relations; others may
require new semantics. Preserve them as unknown or partial until that is
demonstrated, rather than claiming that the initial list already covers them.
