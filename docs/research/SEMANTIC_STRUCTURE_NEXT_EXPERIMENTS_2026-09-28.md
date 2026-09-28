# Next semantic-structure experiments

Status: proposed directions and evaluation contracts, not an adopted ontology,
implemented feature, or measured model-quality result. This review used the
production source and frozen independent experiments. It made no provider calls
and changed no runtime implementation.

The next useful module is a **source-grounded candidate graph compiler with
explicit occurrence, scope and reference contracts**. Two extraction strategies
can compete above that common contract; a third, complementary track can test
topic and existing-graph context. Making the current prompt longer cannot by
itself supply structure that its validator explicitly excludes.

## 1. What the current production contract can represent

The reviewed implementation is `loom/src/extract/semantic.cpp`, especially
`kPrompt`, `make_input`, `validate_proposal` and `propose_semantics`. Its SHA256
at review was `755171e974b0fe749f9dec33eccac927dc92deafee52a92785ec632dd0c22313`.
The helper version is `1` in `loom/include/loom/knowledge_semantic.h`; the draft
envelope has `schema_version: 1`. The native model reviewed was
`loom/include/loom/model.h`, SHA256
`675247c1b1529d399c02e50a1d69ab5b67386e935ccb52007f5a56b3d20e1b98`.
These identifiers describe the reviewed source, not a frozen new experiment.

The implementation already gives the next experiments useful infrastructure:
exact observation-local UTF-8 support, original locator preservation, bounded
requests, explicit omission/rejection reporting, request identity, reusable
validated responses, and reversible candidates. The configured semantic model
is separate from the default chat model. The helper adds candidates to the
existing queue; it does not promote them into canonical knowledge.

| Source contract | Capability consequence | Next measurement needed |
| --- | --- | --- |
| One existing-entity subject, one predicate, and an existing object or scalar literal | Describes a flat relation or a textual interpretation. Cannot introduce a proposition/operation occurrence when it has no existing entity | Coverage before and after allowing grounded candidate occurrence entities |
| Nested literal objects/arrays and new entity IDs are forbidden; prompt explicitly excludes nested logical scope and quantifier binding | Cannot encode a quantifier's body, ordered conditional operands, repeated variable binding or nested quoted propositions as checked composition | Exact composition and binding preservation, including negative contrasts |
| `extra` permits only polarity, assertion context and one topic string | Positive/negative and quoted/hypothetical are useful coarse distinctions; their inner scope and multiple simultaneous perspectives are unavailable | Negation-of-belief versus belief-of-negation; nested quotation and conditional scope |
| `premises.claims` contains existing local Claim IDs | Records dependence. It is not an operand list, proof tree, binder map, or way to reference sibling proposals | Keep epistemic dependencies separate from expression syntax |
| Proposal scope is the chunk ID | Isolates input work. A resource chunk is not necessarily a semantic topic, quantifier environment or hypothetical world | Boundary invariance and explicit semantic scope membership |
| Prior Claims are supplied only if all support lies in the chunk; only locally grounded entities enter | A late topic may be sampled, but an earlier thread outside the chunk cannot supply a referent or structured continuation | Return-to-topic and graph-context recall across bounded input boundaries |
| Existing Claim input includes content/qualifiers but omits Assessment | Adequate to name local content; insufficient for judging whether a retrieved relation is observed, inferred, contested or eligible as a premise | Context packets retaining relevant Assessment and support identity |
| Empty grounded-entity context skips the chunk | Unnamed ideas and newly introduced abstract propositions can fail before the model sees them | Source-denominator coverage of unnamed/new concepts |
| `kind: generalization` is a category; predicates are free bounded strings | Neither establishes a generalization rule, its examples, residuals or exceptions | Relation normalization, operation completeness and unsupported strengthening |
| Unknowns occur inside a proposal; empty proposals is valid | A valid empty response alone cannot explain which source atoms were unsupported | Located, reason-coded coverage output independent of accepted proposals |

These are declared capability limits, not implementation defects. In particular,
schema-valid output and an exact quote do not establish that the interpretation
follows from that quote. Sampling first/last chunks improves opportunity for late
material; it is not topic understanding or a graph-context update mechanism.

## 2. Common carrier: the existing knowledge graph

Use the existing Entity and Claim/Assessment model for all three tracks. A
candidate batch may describe new **occurrences**, such as a conditional in a
particular source span, without identifying those occurrences with true world
propositions. Native Entity kinds and Claim predicates are open data vocabularies;
new argument roles must not be added to the closed universal-role enum.

Proposed common candidate envelope, versioned separately from v1:

- `entity_drafts`: local handles for source-located term, proposition, operation
  and context occurrences; proposed kinds; source spans; unresolved identity
  alternatives. A handle is never an invented canonical Entity ID.
- `claim_drafts`: native-shaped subject/predicate/object-or-value/qualifiers
  drafts with source support and existing Claim dependencies. Endpoints may
  name supplied canonical entities or handles in the same batch. Native IDs are
  resolved deterministically by the compiler after validation.
- `coverage`: every supplied span's represented, partial, unsupported, ambiguous
  or omitted status, with reasons and links to any drafts. Unknown does not
  function as a wildcard operand.
- `alternatives`: mutually exclusive candidate interpretations with separate
  scope/binding assignments. Do not force one referent to make a graph connected.

This envelope is an interchange and review protocol in the existing candidate
queue. It must not become another authoritative AST or truth store. Each
accepted graph assertion still requires an ordinary native Assessment before
canonical persistence. Native Claim status has no `candidate` member; pending
bundles remain in candidate storage rather than inventing one. Model-produced
entity drafts must also avoid accidentally
inheriting native default `observed`/active settings before review. The current
candidate-only boundary remains until evidence-class and Expected Property
requirements are satisfied; the experiment does not bypass them.

A minimal possible graph vocabulary, to validate before choosing it:

| Concept | Candidate native representation | Required distinction |
| --- | --- | --- |
| Operation occurrence | Entity plus an `operation_type` Claim with a literal vocabulary ID | Describing a conditional is not an executable inference Operator |
| Operand | Claim from operation Entity to operand Entity with named port and, where needed, ordinal qualifiers | Antecedent/consequent and ordered argument positions cannot swap |
| Nested expression | An operation operand targets another occurrence Entity | Negation and quotation attach to the intended body, not a whole chunk |
| Bound reference | Binder/context Entity and explicit occurrence-to-binder Claims | Repeated binding survives; equal printed names do not establish identity |
| Context | Source-linked context Entity and explicit membership/parent relations, plus native qualifiers | Topic, time, speaker, branch and hypothetical world remain distinct axes |
| Prior knowledge dependency | Native `Assessment.premises.claims` and source Claim references | Epistemic dependency is separate from expression operands |

`Claim.object` names an Entity, not another Claim. Any proposition reification
must therefore use an explicit occurrence Entity; it cannot put a Claim ID in
that field. Existing Claim IDs stay in the native dependency/provenance slots,
or in an explicitly validated typed-reference relation if one is later agreed.
Do not use an untyped scalar ID as a shortcut around reference validation.

Every emitted edge must retain source support or an identified deterministic
construction dependency. A deterministic compiler preserves an interpretation;
it does not turn an uncertain parse into observed truth. `premise` and
`conclusion` are argument-port data, not new universal roles. Generalization,
specialization, guards, defaults and exceptions describe source operations first;
none automatically authorizes an inferred conclusion.

## 3. Three parallel contract experiments

### Minimal executable research API

The first increment can be pure Python research code without a new native ABI:

```python
validate_bundle(bundle, source_packet, *, limits) -> validation_report
compile_bundle(bundle, source_packet, *, limits) -> candidate_graph_report
compare_bundles(left, right, *, goal, budget) -> comparison_report
```

`source_packet` contains exact located Observations, allowlisted existing
Entities and Claims (including Assessments), and a graph snapshot identity.
`bundle` contains a contract version, local Entity drafts, native-shaped Claim
drafts, located coverage/unknown records, and alternative-group identity.
Support offsets remain observation-local UTF-8 bytes. Validation does not look
up arbitrary IDs outside this packet.

`validation_report` separates syntax, source grounding, reference closure,
port/scope/binder well-formedness, and unsupported contracts. It has no semantic
correctness score. `candidate_graph_report` returns the resolved draft entities
and claims, comparison structure, local/canonical reference map, source spans,
assumptions and losses, with `inference_eligible: false` and
`persistable_claim: false`. It must not fabricate a complete Assessment merely
to fit an existing core projector. The candidate projection is derived from the
same native-shaped drafts and remains distinguishable from accepted core Claims.

For an initial bounded grammar, validate predicate application, conditional,
negation, conjunction and quantification with explicit operand ports and binder
references. An opaque unsupported operation preserves its source and known
ports but cannot match as if it had validated semantics. Broader operations
enter through declared port contracts. Graph closure and binder reachability
can be checked mechanically; whether the author meant that binding cannot.

Contract A emits the common native-shaped drafts directly. Contract B emits
compact located operation frames and reference alternatives; a deterministic
adapter expands them into exactly the same Entity/Claim drafts before any
comparison. Frames are transient input syntax, not another persistence model.
Gold tests should require the two encodings of a specified structure to compile
to equivalent views, and require changed port, scope or binding to remain
distinguishable. No live model is needed for this first representability test.

### A. Direct typed occurrence graph

Ask the inexpensive configured semantic model to emit the common graph draft
directly for a bounded source window. Start with a small supported data
vocabulary: predicate application, conditional, conjunction, scoped negation,
quantifier, generalization/specialization, guard/branch and default/exception.
Unsupported operations retain a located unknown and any independently valid
partial operands. The repertoire stays open; accepting a vocabulary identifier
does not establish new execution semantics.

The validator checks native field shapes, local-handle closure, exact source
spans, typed port cardinalities, operator-specific operand requirements, explicit
scope, and binder reachability/capture constraints. It can reject a malformed
graph; it cannot prove the source interpretation is correct. Keep cyclic
argument dependence distinct from the syntactic nesting constraints of a
particular constructor.

Hypothesis: direct graphs reduce calls and preserve composition better than flat
triples, but increase malformed structures and unsupported interpretation. Test
both claims; do not assume that a larger output schema helps a small model.

### B. Ground spans and references, then compose

Use two smaller contracts with the same final carrier. First, a model identifies
operation cues, located proposition/term spans, possible referents and scope
boundaries; it emits grounded occurrence drafts and alternatives. Second, a
bounded composer receives those handles, their exact source text and declared
reference alternatives, and emits only operation/operand/binding Claims. It
cannot silently introduce new unsupported occurrences or change source bytes.

Deterministic rules may close a fully specified simple frame; ambiguous binding
or a missing operator body remains unknown. Preserve alternative candidate
graphs instead of equating repeated text by default. The first stage's output is
not truth or an independent second evidence source for the second stage.

Hypothesis: separating grounding from composition improves reference precision
and makes errors diagnosable at extra call/token cost. Compare A and B on the
same source groups, with matched total budgets as well as unconstrained-quality
diagnostics. Attribute failures separately to missed spans, wrong reference,
wrong composition, validator rejection and budget omission.

### C. Located thread and existing-graph delta

Give a bounded conversation window both its ordered exact spans and a retrieved
context packet from a fixed earlier graph snapshot. Context entries retain
Claim IDs, qualifiers, Assessment class/status, support source IDs, relevant
alternatives and the selection reason. Keep current-source support separate
from old-context dependencies. Do not allow future graph state into an online
continuation experiment.

The output contract proposes source-linked thread/context entities, span
membership, return/continuation relations and reference alternatives. It may
also propose links to old entities/Claims classified as same-project,
structural-analogy, dependency, correction, contradiction or mere mention. These
are different relation goals. An uncertain `it`/`that` can retain several
referents or none; neither adjacency nor a later project name is sufficient to
rewrite earlier membership.

Graph changes are a reviewable delta over the existing graph: base snapshot,
additions, proposed status/supersession links and retained alternatives. Initial
application is a reversible view, preserving all Claims, source spans and
candidate lineage. A proposed refinement or simplified view must retain a map
to what it omitted. Identity merges and deletion are outside the first contract.

Compare three context treatments on identical cases: no older graph, lexical
retrieval of older context, and graph-structured retrieval with the same token
budget. Measure retrieval recall separately from the model's choice among the
retrieved candidates. A perfect referent selector cannot recover an omitted
antecedent. This track can feed A or B; it is not a claim that one global topic
should govern the conversation.

## 4. Comparisons with sufficient shared binding

Derive comparison views from candidate or accepted Claim graphs with explicit
status filters and source IDs; never silently combine their evidence classes.
Define sufficiency for the query, not for all possible thought:

1. Preserve directed typed ports, multiplicity, occurrence identity, scope and
   binding equality/distinctness. Keep negation, quantifier, modality and
   quotation constraints when the query needs them.
2. Literal identity keeps lexical identity. Structural analogy may rename
   declared lexical channels consistently, while retaining both equalities and
   inequalities. Template containment allows unrelated host context.
3. Every removed attribute has a loss record. Abstraction maps and matching
   witnesses point back to original Entity/Claim IDs and source spans.
4. A smaller view is acceptable only when the declared task's positive and
   negative contrasts remain separable. Topology alone is an explicit control.

This keeps the graph native without requiring all text or all Assessment
metadata in every ranking fingerprint. A matching witness verifies the selected
represented relation; it neither validates source interpretation nor transfers
the matched conclusion into the target domain.

## 5. Independent measurement protocol

Freeze the contracts, compiler, prompt, model configuration and split before
quality scoring. Reuse old frozen suites only as regression evidence. Their
outcomes are already known; new tuning requires fresh independently authored
validation groups. Method authors should receive public schemas and development
examples, not validation texts or labels.

Proposed first bounded corpus: 48 development and 96 fresh validation source
groups, balanced across 12 predeclared families. Treat these counts as a budget
proposal, not data already created. Group by source/motif before the split and
vary both domain and wording. Include English and Polish, multiple speakers,
single/mixed-topic turns, unknown operations and genuinely ambiguous references.
An independent annotator specifies allowable alternatives and exact support;
small graph results use a separate exhaustive witness oracle where feasible.

The immediate executable pilot is smaller: 32 manually grounded cases, 16
development and 16 fresh validation, covering the first five operation contracts
and explicit unknown/invalid controls in English and Polish. Both direct and
frame encodings are supplied as gold inputs. This pilot measures contract
representability and compiler behavior before model quality; the larger corpus
above remains a possible later expansion rather than a current commitment.

Required contrast families include shared versus independent bindings; all/some
and quantifier order; relation versus epistemic negation; conditional direction;
guarded/nonexhaustive branches; default plus exception versus strict rule;
generalization with residuals versus unsupported universal strengthening;
quotation/attribution; same words with different relations; equivalent structures
across domains; late topic/revisit/ambiguous pronouns; and repeated versus
independently acquired support. Unstated conclusions need a separate target
label and eligible proof/warrant; extraction success does not satisfy that label.

| Layer | Primary measures | Denominator / failure distinction |
| --- | --- | --- |
| Source selection | Located-span recall, late/middle/topic-return recall, omitted bytes | All eligible source spans, not only selected chunks |
| Grounding/reference | Exact span and entity/binder precision/recall; false identity merges; abstention coverage | Gold source occurrences including unnamed concepts |
| Composition | Required edge/port recall, graph exact match up to allowed renaming, scope/binding contrast accuracy | All supported gold structures; invalid and empty outputs are not successes |
| Uncertainty | Unsupported confident assertions; alternative-set coverage; abstention by family | Separate ambiguous, unsupported and budget-exhausted cases |
| Retrieval/comparison | Candidate-filter recall, ranking/top-K recall, independently valid witnesses, timeout rate | Exact identity, structural analogy and containment scored separately |
| Context delta | Thread membership, boundary tolerance, antecedent alternatives, attachment relation accuracy | Same-project versus analogy/mention remain different labels |
| Reversibility | Source integrity, complete provenance, replay of view changes, unchanged canonical state | Deterministic contract gates, not model self-evaluation |
| Cost | Requests, actual input/output usage, latency, retries, cache hits, cost per correctly represented structure | Count omitted/invalid responses and all stages; report uncached and replay separately |

Report per-family and macro results, plus uncertainty intervals resampled by
independent source group. Source interpretation needs independent judgments;
schema validators and matcher witnesses cannot grade their own meaning.
Native exports without semantic gold remain coverage diagnostics, not quality
benchmarks for the owner's history.

Use the configured cheap model for simple bounded stages first. A stronger
model is an optional separately budgeted escalation experiment, selected by
predeclared signals such as validator failure, unresolved binding or contract
disagreement. It is neither the gold judge nor automatically more correct.
Include a random sample of accepted and abstained cases in evaluation so a
failure-only escalation sample does not conceal recall loss. Dollar costs may
be computed only from recorded usage and a dated actual tariff; missing usage
remains unknown. No prices or paid quality measurements are supplied here.

## 6. What prior measurements do and do not justify

- The frozen broad source experiment had 0/11 strict contrast orderings per
  split after source extraction, despite 11/11 on supplied graphs. Representation
  and retrieval correctness therefore cannot substitute for text coverage.
- The fresh scoped experiment improved exact contrast ordering from 2/6 to 6/6
  when shared literal bindings were preserved. Its scopes were supplied, its
  syntax restricted and its sample small. It motivates explicit binding tests,
  not an assumption that repeated words resolve real conversational identity.
- The independent graph-native matcher obtained 228/228 correct bounded
  judgments and 72 valid positive witnesses. Analogy ranking mAP was 0.940 in
  each split, with a hard negative ranked above a relevant result in one query
  per split. Supplied graph abstraction and tiny graphs bound this result.
- Semantic/structural native projections retained selected restrictions and
  exact support identities in 16/16 curated exports; all 36 relation-mode
  checks passed. Those qualifiers were already explicitly supplied. This does
  not show that a model can infer quotation, scope or modality from prose.
- Native helper scripted tests concern transport, validation, budgets and
  persistence. They are not live-model interpretation scores. Source-only
  deterministic architecture-document diagnostics also cannot estimate model
  quality or graph-context recall.

Sources: the frozen reports under
`loom/tests/fixtures/eval/independent_structure_v1/`,
`independent_scope_v1/` and `independent_graph_native_v1/`;
`SEMANTIC_MODEL_FLOW_2026-09-28.md`; `ATOMIC_OPERATIONS_2026-09-28.md`;
`GRAPH_NATIVE_PROGRAMME_2026-09-28.md`; and the binding conceptual model.

## 7. Proposed next increment

Build the offline common compiler and its independently checked contract
fixtures first, retaining the existing v1 flat draft as a baseline. Prototype
A and B behind separate candidate schema/prompt versions and prototype C with
fixed graph snapshots. Before any provider call, measure which gold structures
each contract can encode at all; a prompt cannot recover a forbidden structure.
Freeze a new validation split before any model outcomes.
Only then run an explicitly bounded model comparison and choose the combined
flow from measured coverage, errors and cost. Record partial improvements as
partial: completing the model transport does not complete universal thought
representation, and a richer carrier does not complete semantic extraction.
