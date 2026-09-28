# Research continuation: structures of thought and contextual selection

The owner resumed work on 2026-09-28 and explicitly authorized sustained
experiments, iteration and alternative approaches. This is the central system
capability, not merely an optimization of keyword retrieval. All meaningful
increments and measured results must be checkpointed on GitHub.

## Owner clarifications (Polish, verbatim excerpts)

> Skoro temat otwarty, to pracujemy dalej. Eksperymenty robimy. Znaczy, ja na ciebie liczę, że to opracujesz. Bo to jest najważniejszy element systemu.

> potrzebujemy jakiegoś uogólnienia rozmów, które by oddawało strukturę argumentu, myśli czy idei, żeby później to łatwo matematycznie porównać.

> Chodzi mi o struktury na tyle ogólne jak na przykład syllogizmy jako podstawowe elementy tych struktur. Kumasz, nie? Że na przykład jak jest rozmowa na jakiś temat, ale struktura jest bardzo popularna w uogólnieniu w innych tematach, no to to uogólnienie trzeba wyłapać.

> To jest przykład właśnie taki atomowy element. Innego rodzaju rzeczy to będą uogólnienia albo sprecyzowania, warunki rozgałęzienia i wiesz, takich atomowych rzeczy każdą prawieże strukturę myśli można opisać. Jeśli takich właśnie tego typu atomowych elementów się więcej wyliczy, zmapuje prawie wszystkie możliwe

> Dobrze by było też rozpoznać ogólnie zmianę tematu, żeby wyciągnąć z rozmów wielowątkowych, gdzie na przykład dopiero pod koniec się jakiś temat projektu omawia, to analizator powinien być świadomy, że o tutaj się zaczyna ten temat i dalej może być na ten temat. I szczególną uwagę przyłożyć. I także odwoływać się do umieszczonych w grafie danych. W ich kontekście umieszczać nowe, a w razie potrzeby updateować - rozbudowywać graf, czasem upraszczać, zawsze udoskonalać.

The longer prompt forwarded from Claude additionally reiterates:

- Multiple complementary perspectives on argument, thought and meaning;
  geometry/topology and mathematical comparison across abstraction levels.
- Universality and specificity are independent, not inverse quantities.
- Category, example-of and relation structure; cross-topic recurrence;
  recognize warranted but unstated conclusions with explicit provenance.
- Research directions and competing hypotheses, not premature fixed decisions.
- Complete interpretation and retention of OpenAI/Anthropic exports and
  attachments, including unknown formats with marked inferred interpretation.
- Owner-controlled copy/link options and representation of all source metadata.
- Provider-inspired interface profiles and multiple coordinated simultaneous
  views; no exclusive-view restriction. Extend the general capability rather
  than hardcoding the examples the owner happened to mention.

The full source requirement log remains
[`OWNER_REQUIREMENTS_2026-09-26.md`](../architecture/OWNER_REQUIREMENTS_2026-09-26.md)
and the shared semantic contract remains
[`LOOM_CONCEPTUAL_MODEL.md`](../architecture/LOOM_CONCEPTUAL_MODEL.md).

## Working decomposition

1. Source preservation and topic segmentation: identify exact turn/span
   boundaries, topic introductions, continuations, returns and uncertain links.
   Late evidence must not retroactively authorize unrelated early text.
2. Grounded representation: observations support assessed claims; experimental
   structure projections retain their source IDs, spans and epistemic status.
   Parsing coverage is measured separately from matching on annotated input.
3. Composable operations: a broad, extensible vocabulary with inputs, outputs,
   preconditions, information loss and unsupported cases. Syllogisms form one
   family alongside abstraction/specialization, conditions/branches/exceptions,
   comparison/analogy, parts/wholes, goals/means/constraints and other families.
4. Multiple comparisons: lexical control, role/relation statistics, directed
   graph neighborhoods and bounded binding-preserving alignment. Separate
   semantic identity, structural analogy and membership in the same project.
5. Conditional inference: replayable rules with premises and counterconditions;
   frequency of a pattern is neither deductive validity nor evidence that it
   applies to a new case. Ambiguity and unsupported operations remain explicit.
6. Existing graph context: propose extensions, refinements, competing readings,
   conflicts and reversible consolidations; retain source evidence and history.
   Similarity alone must not merge entities or promote a hypothesis to fact.

The experimental registry is not an unreviewed expansion of the production
closed Operator.op or universal-role sets. Any eventual core change must amend
the shared model and validate the real graph/transport paths.

## Experiment discipline

- Preserve the old native baseline: 13/45 conversation recall, 13/13 precision,
  0/20 selected noise; the existing 0.55 recall gate stays unchanged.
- Use paired controls: same argument across different domains, same vocabulary
  with a different dependency, changed negation/quantifier/role, misleading
  identity cues in another topic segment, and unsupported reverse inference.
- Freeze independently authored development/validation groups before scoring.
  Method authors do not read validation examples to tune rules or weights.
- Report project membership, philosophy relevance, structural analogy and
  inference validity on separate axes, conditional on a declared task goal.
- Report abstention, representation/parser coverage, failure categories,
  latency, bounded-search exhaustion and unknown cases, not only aggregate
  similarity. No claim that the current vocabulary covers all human thought.
- Do not read or tune against `eval/real-holdout-key`. The earlier historical
  pack contamination and benchmark limitations still apply.

## Active artifacts and status

- `loom/tools/structure/`: offline research prototypes; no production graph
  writes, no external model calls, no new application behavior implied.
- `STRUCTURE_METHODS_2026-09-28.md`: primary-source rationale and contrasting
  methods, with invariances and known failure cases.
- Independent multi-axis fixtures/evaluator and topic/source adapters are in
  progress. Author tests and independent results must be distinguished.
- Native C++ checkout is rebuilt from the verified remote snapshot `33fb083`.
  Its local git history is synthetic; remote Git-data commits retain the real
  parent/tree. Original historical files omitted locally remain on GitHub.

This is an active research checkpoint. It does not claim that a new semantic
representation, complete natural-language parser or graph updater is ready.
