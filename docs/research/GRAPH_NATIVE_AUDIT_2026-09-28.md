# Graph-native representation and semantic-model audit

Audit date: 2026-09-28. Production code inspected at `7ae91c6`; concurrent
research additions are outside that production baseline. This is a read-only
audit and an implementation proposal, not a claim that the proposal is already
implemented. No independent validation examples or holdout key were read.

Subsequent implementation: see `SEMANTIC_MODEL_FLOW_2026-09-28.md` and
`GRAPH_NATIVE_RESULTS_2026-09-28.md`. They close the model-setting destruction,
failure-latch recovery and native/UI model-call gaps identified below, through
reviewable source-grounded candidates. This baseline audit intentionally keeps
its original findings; it is not a description of those later fixes.

The owner's clarification is consistent with the binding conceptual model:
arguments, thoughts and ideas belong in the same assessed knowledge graph.
`Claim + Assessment` is already substantially richer than the graph drawn by
either UI. The immediate task is to expose and populate that structure, then
add explicit data contracts for genuinely missing semantics. An experimental
logical AST must remain a derived view or test notation, not another authority.

There is also a concrete integration gap. The separately configured semantic
model exists and is connected in both Python and C++. Its current prompt asks
for entities, topics and simple subject–predicate–object relations. Its live
and background results enter the legacy `nodes`/`links` graph. They do not enter
the richer `KnowledgeStore` as `Claim + Assessment`. The knowledge pipeline
accepts `llm: "auto"` but the inspected extraction/generalization stages do not
use that setting to call a model. Therefore an `llm: "off"` measurement is a
useful deterministic baseline, not a measurement of the intended complete app.

## 1. Binding contract and actual stores

Read together: `AGENTS.md`, `CLAUDE.md`,
`docs/architecture/LOOM_CONCEPTUAL_MODEL.md` §§1–3 and
`docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md` R3, R7, R10–R15.
The model requires immutable sources, located observations, assessed claims,
explicit derivations, competing models and owner judgement. Its fourteen
universal project roles are not an inventory of logical argument positions.
Open domain data and the closed executable operation vocabulary remain
different things.

| Layer in actual code | Stored representation | Current consequence |
|---|---|---|
| Python-compatible core graph | `Node{id,kind,label,content,tags,metadata,created}` and `Link{id,src,dst,link_type,weight,metadata,created}` in `loom/include/loom/db.h`; Python equivalents in `engine/db.py` | Keeps compatibility and powers the older graph UI; it is not the assessed claim schema. |
| Assessed knowledge | Per-run entity, observation, claim, principle, operator, model, instance, slot and other tables in `loom/include/loom/knowledge_store.h` | Full model objects survive in JSON bodies, with selected SQL indexes. This is the appropriate authority for knowledge semantics. |
| Source/provenance | Source hashes, BlobStore/catalogue locators, observations and their support references | The original source and the interpreted claim are separate. A graph view must preserve the route back to the source. |
| Research formula and graph objects | `loom/tools/structure/structure_methods.py` | In-memory/file experiment inputs and projections; no production store integration or independent authority. |

The implementation currently has both compatibility graph tables and assessed
knowledge tables. Calling them one already-unified physical graph would be
inaccurate. A single *semantic authority* does not require deleting the legacy
tables during this research round: make compatibility views/adapters explicit
and have new knowledge pass through the assessed representation.

## 2. What Claim and Assessment already represent

Authoritative definitions are `loom/include/loom/model.h` (`Entity`, `Support`,
`Derivation`, `Premises`, `Assessment`, `Qualifiers`, `Claim`, `Model`). Actual
JSON serialization is in `loom/src/model/model_core.cpp`.

| Required information | Existing representation | Important limit |
|---|---|---|
| Subject, relation, entity/object identity | `Claim.subject`, open `predicate`, `object` entity ID; alternatively a JSON `value` | An `object` is an entity ID by contract, not a free slot for a claim ID. Arbitrary JSON capacity does not supply logical semantics. |
| Source support | `assessment.basis.support[]` with `observation`, `locator`, `quote`, `extractor`, `quality` | A matching source span grounds an interpretation; it does not prove that the source assertion is true. |
| Method and dependency | `basis.derivation` with `operator`, `operator_version`, `morphism`, `depth`; `premises.claims`, `principles`, `assumptions` | Premise lists encode dependency, not antecedent/consequent ports, discharge of assumptions or quantified binding. |
| Counterevidence and alternatives | `counter.claims`, `counter.observations`, `alternatives`, `status`; competing `Model` claim/principle sets | `contested` and `rejected` are epistemic states, not object-language negation. Competing interpretations must remain distinct. |
| Consequences and questions | `consequences.claims/predictions/checks`, `open.slots/questions/fill_query` | These references should become visible projection edges; their existence alone is not proof replay. |
| Missing candidate | Evidence class `absent`, neither object nor value, with `open` | Means nothing known for that slot. It is not a negative claim and does not mean an expression parser understood the source. |
| Validity/context | `qualifiers.valid_from`, `valid_to`, `version`, `branch`, `scope`, `lang`, `extra` | The string `scope` is an area/instance/context identifier, not a checked lexical binder scope. |
| Source order and dialogue | Observation `ordinal`, `speaker`, `date`, `attrs`; located unit/source | Order, source date and claim validity time are separate dimensions. |

The canonical JSON envelope, using descriptive placeholders rather than a
persistable example, is:

```json
{
  "id": "<claim-id>",
  "subject": "<entity-id>",
  "predicate": "<relation-id>",
  "object": "<entity-id-or-empty>",
  "value": null,
  "qualifiers": {
    "valid_from": "", "valid_to": "", "version": "", "branch": "",
    "scope": "", "lang": "", "extra": {}
  },
  "assessment": {
    "basis": {"support": [], "derivation": null},
    "evidence_class": "<class>", "origin": "<origin>", "confidence": 0.0,
    "premises": {"claims": [], "principles": [], "assumptions": []},
    "counter": {"claims": [], "observations": []},
    "status": "active",
    "consequences": {"claims": [], "predictions": [], "checks": []},
    "open": {"slots": [], "questions": [], "fill_query": null},
    "expected_property": null, "check_state": "n/a", "alternatives": []
  }
}
```

The exact serializers, rather than this illustrative envelope, govern empty
field values. Observed claims require support; inferred claims require a
supported Expected Property and non-`n/a` check state. Do not create the above
placeholder as a record.

`Claim::make_id` hashes subject, predicate, object/value and all serialized
qualifiers. Assessment is not in the content identity. Consequently adding
semantic content to `qualifiers.extra` changes claim identity; storing an
ephemeral projection node ID there is not harmless cache metadata. Unknown
top-level Claim fields are not preserved by `Claim::from_json`; declared JSON
carriers such as `value`, `qualifiers.extra` and entity/observation `attrs` are
the actual extension points.

### Storage acceptance is weaker than complete semantic validation

`Claim::validate` checks required subject/predicate, object XOR value (with the
`absent` exception), and assessment invariants. `KnowledgeStore::put_claims`
(`loom/src/kb/store.cpp`) checks known premise evidence and transfer chaining.
It explicitly skips a premise ID not found in the batch/current run. These
functions do not prove reference closure, validate quote bytes against source,
check nested binders, or verify an arbitrary interpretation payload. A bridge
must report unresolved references and grounding failures rather than treating
accepted JSON as complete logical knowledge.

## 3. Precisely missing semantics versus missing projection

| Feature | Already present | What is missing for the requested use |
|---|---|---|
| Argument/thought/idea identity | Open entity kinds and ordinary assessed relation claims can describe such objects | A versioned data vocabulary and extraction convention for occurrences, constituents and assertion context; no new global truth schema is needed. |
| Inference premise dependency | `assessment.premises.claims/principles/assumptions` | Typed premise roles, ordered ports, rule instantiation and assumption discharge are not first-class checked contracts. Their display/traversal is also missing. |
| Conditional content | A conditional can be quoted as an observation/value or described by entities/claims | No native arbitrary-user-statement `implies` constructor with checked antecedent/consequent semantics was found in the inspected model/extractor. A rule's executable `when` is a different layer. |
| Quantifiers | JSON can physically encode a quantifier; principles have textual conditions and exceptions | Checked quantifier, binder, variable occurrence, restriction/domain and body contracts; native extraction of those contracts. `all`/`any` in rule conditions are not automatically object-language quantifiers. |
| Entity binding/coreference | Entity IDs, aliases, resolution, repeated subject/object identity | Bound-variable identity is different from ordinary entity identity. Existential witnesses and nested scopes need explicit binding topology. |
| Polarity | Negative text and arbitrary predicate/value data can survive; lexical negation cues exist | Compositional, scoped `not` semantics. Evidence `absent`, status `rejected`, counterevidence and low confidence must not stand in for logical negation. |
| Nested scope | Qualifier context ID and observation topology | Parent/operand/binder relations and assertion/hypothetical/quotation contexts that distinguish nested expressions. A single root polarity flag is insufficient. |
| Source sequence, version, time | Observation order/date, dialogue attrs, qualifiers and status history | Some propagation and projection are missing; these existing dimensions must not be collapsed into a single order or timestamp. |
| Support, counter, consequences, alternatives | Already serialized in Assessment | Mostly query/projection/display work; these do not require inventing another knowledge object. |
| Proof checking | Data-pack rules and a closed Expected Property language; research replay for four rules | No implemented production `proof_replay` Expected Property. Generic graph similarity does not fill that gap. |

One misleading existing label is `has_premise`. It occurs in
`loom/data/schema/types.json`, but the actual `film` project-kind slot
(`loom/data/project_kinds/film.json`) means logline/narrative premise under
`intent`. It is not evidence that formal argument-premise semantics are already
registered. Do not reuse it without an unambiguous data contract.

For classical propositional interpretation, the content structures
`not(implies(P,Q))` and `implies(P,not(Q))` differ in the parent of the negation
and the path to its operand. For `P=false, Q=false` they evaluate to false and
true respectively. An unlabeled pair of P/Q entities or a bag of relation names
cannot retain that distinction. Similarly, `forall x exists y R(x,y)` and
`exists y forall x R(x,y)` require binder order and variable-occurrence links.
This is a requirement on a selected logical interpretation, not a claim that
all natural-language conditionals have material-implication semantics.

### Native extraction and resolver details that constrain a bridge

- `loom/src/extract/extract.cpp` preserves conversation message metadata
  (`node`, `parent`, `branch`, `current`, `seq`) in observation attrs.
  `extractors.cpp::Run::claim` copies `seq` into claim qualifiers except for
  `mentioned_in`, producing distinct occurrences when repeated later. It does
  not automatically copy all observation branch metadata into every claim.
- `extractors.cpp::nearest_version` chooses a nearby prior, otherwise future,
  mention. `branch_of` uses explicit branch mentions. These are extraction
  heuristics, not proof of temporal scope. Status extraction does populate
  `version`, `branch` and `valid_from` where available.
- `extractors.cpp::do_normative` and `do_areas` extract candidate principles,
  umbrella/list areas and invariants. They do not implement an arbitrary
  compositional logic parser. `lexicon.cpp::negated_at` is a small preceding
  token window, not a nested-negation semantics.
- `loom/src/resolve/resolver.cpp::apply_remap` remaps Claim subject/object,
  rekeys claims and combines support. It does not rewrite arbitrary embedded
  references in `value`/`extra`, and suppresses object==subject relations as
  superseded. Scoped expression occurrences and reflexive content need an
  explicit resolver policy before they can safely use ordinary alias merging.
- `loom/src/resolve/assess.cpp::detect_conflicts` handles selected functional
  predicates and qualified slots; it is not a general P-versus-not-P checker.
- `loom/src/generalize/common.cpp::usable` excludes absent/extrapolated and
  rejected claims but does not exclude every other status. A new projection
  must state its status policy explicitly rather than assume that all native
  consumers already have the desired temporal/epistemic filter.

## 4. What the UIs omit

`loom/src/db/db.cpp::get_graph_data` returns a compatibility display graph:
message/reply edges plus a limited node list, then core links reduced to
`{src,dst,type,weight}`. It drops link identity, metadata and creation time.
`loom/src/capi/capi_graph.cpp` serves this legacy graph, used by
`loom/web/src/components/GraphView.tsx`. This endpoint is not an export of the
assessed knowledge graph.

The newer `KnowledgeWorkbench.tsx::KnowledgeGraph` does read KB entities and
claims, but renders an edge only when both `subject` and `object` are visible
entities. Literal-valued claims, support, premise dependencies, counterevidence,
consequences and nested contexts are absent from that drawing. Its neighborhood
walk follows only subject/object adjacency, and its default node cap is 60.
The inspector has the complete selected record; the drawing is the lossy part.
Concluding that the model lacks Assessment because these edges are not drawn
would confuse projection with storage.

The existing read API is `POST /api/knowledge/query`, implemented through
`loom/src/capi/capi_knowledge.cpp::run_query`:

```json
{"run":"<explicit-run-id>","what":"claims","limit":10000}
```

It returns `{"run":"...","items":[<Claim JSON>, ...]}`. Claim filters are
`subject`, `predicate`, `object`, `observation`, `branch`, `evidence`, `origin`,
`status`; entity filters are `kind`, `canonical_key`, `alias_key`, `parent`.
Other supported collections include instances, slots, principles, operators,
morphisms, decisions, forks, areas, predictions, models, products and status
history. This generic endpoint has no observations collection, semantic
dependency closure, version/scope/premise filter or pagination cursor. C++
`KnowledgeStore` does provide observation lookup. A bridge should select an
explicit run, retain the query limit/truncation status and use a consistent
read transaction when reading SQLite directly. An omitted result is unknown,
not evidence that a relation does not exist.

## 5. Semantic-model path: configured, live, but aimed at a different graph

### Python path

`gui/dialogs.py::SettingsPopup` exposes a semantic model picker and LLM-analysis
toggle; `_save` persists `semantic_model` and `semantic_analysis` independently
of the conversation model. `main.py` constructs `SemanticLLM`, passes it to
`GraphEngine`, constructs `SemanticWorker` and starts the worker.

`engine/semantic_llm.py::enabled` requires the toggle, a semantic model ID and
`api_key`, and no five-failure disable state. `analyse` first runs regex, then
uses the model for text of at least 20 characters. `_call_llm` sends up to
3,000 characters to the configured `base_url + /chat/completions`, with the
semantic model, temperature 0.1 and max_tokens 800. This is an implemented call
path, not an unused setting. It falls back to regex on failure.

`engine/graph_engine.py::_on_message` receives `MSG_CREATED`; both it and
`ingest_analysis` turn entities/topics into legacy nodes and SVO relations into
legacy links, with a fixed 0.7 relation weight. The worker processes pending
messages through that same ingestion function. These functions do not create
the later Loom `Claim + Assessment` representation.

### C++ path

`loom/web/src/components/SettingsPanel.tsx` preserves both settings.
`loom/src/runtime.cpp` constructs `SemanticLLM`, injects it into `GraphEngine`
and `SemanticWorker`, always starts the graph subscription, and starts the
worker when `RuntimeOptions.start_workers` permits it. The C++ implementation
in `loom/src/semantic/semantic_llm.cpp` follows the Python gate, prompt,
truncation, HTTP shape and fallback. `GraphEngine::on_message` and worker
`drain_once` both reach `graph_engine.cpp::ingest_common`, which writes legacy
`Database` nodes/links. The worker's Anthropic batch result ingestion also
calls this graph ingestion function. No assessed-store call occurs there.

The actual model-output contract in both prompts is:

```json
{
  "entities": [{"name":"...","kind":"...","relevance":0.8}],
  "topics": [{"label":"...","confidence":0.8}],
  "relations": [{"subject":"...","predicate":"...","object":"..."}],
  "summary":"...", "sentiment":"neutral"
}
```

The implementation adds `source: "llm"` or `"regex"`; batch worker uses
`"llm_batch"`. No fields request evidence spans, argument occurrences, operand
positions, quantifier binding, scoped polarity, alternatives or derivation
premises. Generic JSON parsing plus array-shape normalization is not semantic
validation. Additional raw fields could be returned, but the current graph
ingester would ignore them. A model capable of reasoning cannot recover this
missing persistence path just by being selected in Settings.

### Other model paths must not be conflated with this one

| Path | What really calls a model | Where its result goes |
|---|---|---|
| Archive graph stage | `archive/pipeline.cpp::StageRunner::graph`, only `llm:auto` and enabled SemanticLLM | Same legacy graph for not-yet-done messages. Its `docs` output still uses the separately computed regex entities. |
| Archive item refinement | `refine_items_llm`, only archive `llm:auto`, key/base/model available; prefers semantic model, falls back to default model | Reclassifies at most 100 low-confidence, non-implementation item sentences into idea/decision/question/etc.; changes archive Item type/confidence/cues. Does not build assessed argument structure. |
| Goal/context classification | `context/context_engine.cpp`, cue confidence below 0.55, configured semantic model/key | Selects one known goal type and confidence. It is not a thought-structure extractor. This direct call does not reuse the full SemanticLLM enable gate. |
| Generic SemanticBatchAPI | `chat/batch_api.cpp`, concurrent and batch result paths | Calls `Database::mark_analysed` but does not call GraphEngine ingestion. Unlike the worker batch path, this path can mark a message done without materializing its returned relations. |
| Assessed knowledge run | `knowledge/engine.cpp` parses/serializes `KnowledgeConfig.llm`, passes config into StageContext | At the inspected baseline, no matching model call or `llm` use in `extract`, `resolve` or `generalize`. `extract/stage.cpp` writes deterministic extraction directly to KnowledgeStore. |

The comment in `loom/include/loom/knowledge.h` that areas “may refine with a
model” is not evidence of an implemented stage. This audit searched the stage
implementations and call sites; the active archive refiner is a separate path.

### Concrete defects and observability gaps for the integration owner

1. **Overbroad settings migration.** Both `engine/config.py::_auto_upgrade` and
   `loom/src/core/config.cpp::Config::auto_upgrade` clear every configured
   semantic model string containing `claude-haiku-4`. The C++ check is not gated
   on an old config version; existing tests explicitly cover the broad reset.
   This audit does not assert which provider model IDs are currently valid.
   The defect is an unconditional substring policy that discards an explicit
   owner selection on load. Review exact historical invalid IDs and preserve
   arbitrary configured IDs; change compatibility tests deliberately.
2. **Recovery after model failure.** After five failed analyses the model is
   disabled for that instance. C++ `reset_failures` exists, but the inspected
   production config/secret setters do not call it; only tests do. A corrected
   model/key in Settings therefore does not clear the failure state in the
   running instance. Python likewise has no reset in its settings save path.
3. **Fallback completion prevents later enrichment.** `mark_analysed` writes
   `semantic_status='done'` even for regex/error paths. The worker query selects
   only pending, active messages of at least 20 characters. Merely enabling a
   model later does not revisit already-done fallback results. A deliberate,
   model/prompt-versioned enrichment task is needed, with provenance and no
   deletion of previous results.
4. **Output/provenance loss.** `Database::mark_analysed` keeps source, counts,
   summary and sentiment, not the full model response or support spans. Legacy
   relation writes give each relation weight 0.7, independent of model evidence.
   A future bridge must preserve the response and analysis configuration and
   validate grounding before assessed persistence.
5. **Different gates in different consumers.** Archive item refinement can use
   the default conversation model if the semantic model is empty; context
   classification checks model/key directly and does not use the toggle/failure
   gate. A coherent policy should be explicit rather than inferred from the
   existence of `semantic_model` in each function.

These findings establish potential failures from code, not an assertion about
the owner's current secrets, selected model, past request success or provider
availability. No live provider request was made for this audit.

## 6. Single authority, derived graph projections

The minimal useful bridge should project existing records before adding any
new semantic content. A claim-incidence node is an ephemeral node identified
by its canonical claim ID. Directed, typed ports link it to its subject,
object or typed literal, qualifier context, premise claims, support observations,
counterevidence and consequences. These are projections of existing fields,
not newly asserted facts. Keep IDs and source records for every projected node
and edge. An arbitrary text assumption remains text, not an invented entity.

Use separate projection policies for different questions:

- **Literal identity:** preserve entity identity, typed values, predicates,
  qualification and assessment distinctions needed by the query.
- **Structural analogy:** permit consistent renaming of selected domain labels
  while preserving direction, semantic ports, repeated-entity binding, scope,
  assertion context and explicitly selected qualifiers.
- **Topology control:** deliberately remove labels for a comparison baseline;
  report what was lost and never promote its matches to semantic equivalence.

Every result should report selected run, method/projection version, source
claim IDs, omitted fields, unresolved references, boundary truncation and the
eligible evidence/status policy. Matching may cross domains without making
domain entities identical. Domain/source support for a pattern and the
specificity of the pattern are separate measurements.

For later structural extraction, a candidate contract can use ordinary graph
entities for source-scoped expression occurrences, variables and binders, plus
assessed relation claims for operator kind, typed operand positions, binder
references and occurrence context. This is a proposed *data vocabulary with
validation*, subject to the shared conceptual-model process, not a second
canonical AST store. A logical AST may be reconstructed from those relations
when a particular checker needs it.

Do not assert a conditional's antecedent as a fact merely because it appears
as a child expression. Claims about the source expression's composition are
different from assertions of the expression's content. The contract must
distinguish asserted, hypothetical and quoted occurrence contexts and preserve
interpretation alternatives. Missing or ambiguous structure is explicitly
unknown; a partially extracted graph is useful without being logically closed.

For the semantic-model integration, retain raw source and full model response,
then validate proposed spans, referenced IDs, port cardinalities and binding
scope before producing claims. An observed claim can describe what the source
says only when support is grounded; model-supplied world knowledge is a
separate candidate with the appropriate origin and assessment. Structural
similarity supplies a retrieval result, not a valid inference or Expected
Property implementation.

### Smallest native integration selected after this audit

The owner authorized a bounded optional proposal helper in the actual native
extract stage. Its implementation and validation are recorded separately in
`SEMANTIC_MODEL_FLOW_2026-09-28.md`; the findings above describe the inspected
baseline. The existing `loom_kb_candidates` and `loom_kb_llm_cache` tables were
already defined in `loom/src/kb/schema.cpp`, but had no producer/store methods
at that baseline. Reusing them requires an implemented adapter, not just a
claim that reserved tables already provide model integration.

The first increment is an explicit `llm:auto` call over bounded, located
observation groups. It proposes partial Claim-shaped drafts in the existing
candidate queue. It does not promote them or create a second claim graph.
Run/unit/source/branch, exact observation-byte quotes, model/prompt identity
and unknown semantics remain attached. The response cache serves replay and
avoids repeated charges; it is not another semantic authority. No general
logical constructor, first-class quantifier or proof rule is added by this
increment.

One additional projection convention is grounded in actual producers:
`extractors.cpp::Run::entity` writes `Entity.attrs.units` and
`attrs.observations`; the final extraction fills observation IDs from
`ent_obs_`. `extract/stage.cpp::merge_entity` unions these named reference
arrays, with a 256-observation cap. A native projection may expose these
specific producer-defined fields as reference edges, with unresolved/capped
coverage visible. It must not discover typed references by guessing from
arbitrary strings in every opaque attrs payload.

## 7. Existing generalization and executable next steps

`loom/src/generalize/evidence.cpp::Evidence::load` already loads assessed
entities/claims and referenced observations. `match.cpp` matches data-pack
paradigms against slots and role bindings. Its `ParadigmMatcher::analogies`
compares sets of shared roles and slot/value signatures, with Jaccard-based
scores; it does not compare arbitrary binder/operand topology. Its resulting
`analogous_to` claim is inferred with premises and a pending
`not_contradicted_by` property, not a proof of transfer validity.

`infer.cpp` evaluates a bounded data-pack operation vocabulary. The `all`,
`any`, `not`, `has_claim` and related operators in `loom/include/loom/kb.h`
are executable model-state expressions. They should not be confused with
parsing arbitrary quantified claims in the owner's text. The research
`structure_methods.py` supports nine formula constructors and four replay
rules; its proposed `proof_replay` property is explicitly not persistable in
the current core. Neither subsystem establishes complete thought coverage.

Actionable ownership map (proposal; no production edits in this audit):

| Work item | Primary files | Concrete completion criterion |
|---|---|---|
| Preserve settings and recover analysis | `engine/config.py`, `loom/src/core/config.cpp`, `loom/src/capi/capi_core.cpp`, settings tests | Arbitrary explicit model survives reload; corrected config resets failed analysis; no live-provider dependency in tests. |
| Make semantic execution visible | `semantic_llm.cpp`, `semantic_worker.cpp`, `SettingsPanel.tsx` | Expose configured/enabled/fallback/disabled reason, method version and actual attempted/accepted count without exposing secrets. |
| One grounded model-output adapter | `extract` stage/helper, semantic prompt, `KnowledgeStore` write path; shared live/archive caller | Scripted model response with located source spans becomes valid assessed records; unresolved/unscoped output is retained as partial, not fabricated semantics. |
| Shared live/archive enrichment | `GraphEngine`, `SemanticWorker`, `knowledge/engine.cpp`, `extract/stage.cpp` | Both callers use the same adapter; enabling enrichment does not erase observations, prior claims or competing readings. |
| Graph-native bridge and search | Research projection/search modules, then an API consumer if justified | Actual native records yield a source-linked incidence graph; missing references and query caps are visible; all matches identify canonical source claims. |
| Expression vocabulary and validator | Data schema/packs plus a narrowly scoped validator and resolver policy | Operator ports, binders, nested negation and assertion contexts survive round-trip, alpha-renaming and identity resolution; unknown constructs abstain. |
| UI focus and topic continuity | KB projection/UI and context selection | A late-introduced project is an added local thread; focus changes without overwriting older graph content. |

Run experiments in layers so attribution remains possible: (1) actual native
record projection, (2) independent supplied-graph matching, (3) scripted
semantic-output ingestion with source checks, (4) independently scored model
extraction when deliberately enabled, then (5) integrated context/retrieval.
Use the same source cases for extraction coverage and end-to-end retrieval,
and report skipped, ambiguous and truncated cases. A deterministic baseline,
a mocked transport integration check and real model quality are three different
measurements.

Graph search can use cheap necessary-condition filters and neighborhood
signatures to produce candidates, then a bounded labeled subgraph check that
preserves direction, ports, parallel relations and injective bindings. Report
search-budget exhaustion as unknown. A match in an explicitly lossy projection
remains a match only under that projection. The research programme and its
independent evaluator own performance claims; this audit supplies no new scores.

Topic segmentation, focus, contextualization, append, revision, merge and
refinement from `ATOMIC_OPERATIONS_2026-09-28.md` fit here as transformations
over selected graph neighborhoods. Segmentation hypotheses and merges retain
source membership and alternatives. Simplification produces a view or a
justified replacement with derivation links; it does not overwrite the source
or silently collapse conflicting interpretations.

## 8. Source map for implementation review

| Question | Source and symbol |
|---|---|
| Exact assessed schema | `loom/include/loom/model.h`: Claim, Assessment, Qualifiers, Premises, Support, Model |
| JSON identity and validation | `loom/src/model/model_core.cpp`: Claim::make_id/validate/to_json/from_json; Assessment::validate |
| Persistence/query behavior | `loom/include/loom/knowledge_store.h`; `loom/src/kb/store.cpp`: write_claim, put_claims, query_claims |
| Public read shape | `loom/src/capi/capi_knowledge.cpp::run_query`; `loom/web/src/api/loom-http.ts` |
| Compatibility graph | `loom/src/db/db.cpp::get_graph_data`; `loom/src/graph/graph_engine.cpp::ingest_common` |
| KB visual omission | `loom/web/src/components/KnowledgeWorkbench.tsx::KnowledgeGraph` |
| Native source interpretation | `loom/src/extract/extract.cpp`; `extractors.cpp::Run::claim/do_areas/do_normative`; `lexicon.cpp::negated_at` |
| Resolution risk | `loom/src/resolve/resolver.cpp::apply_remap`; `resolve/assess.cpp::detect_conflicts` |
| Native analogy/inference | `loom/src/generalize/match.cpp::ParadigmMatcher::analogies`; `infer.cpp`; `common.cpp::usable` |
| Python configured-model path | `gui/dialogs.py`, `main.py`, `engine/semantic_llm.py`, `semantic_worker.py`, `graph_engine.py` |
| C++ configured-model path | `loom/src/runtime.cpp`; `semantic/semantic_llm.cpp`; `worker/semantic_worker.cpp`; `graph/graph_engine.cpp` |
| Model consumers outside live graph | `loom/src/archive/pipeline.cpp::refine_items_llm/StageRunner::graph`; `context/context_engine.cpp`; `chat/batch_api.cpp` |
| Assessed pipeline integration gap | `loom/src/knowledge/engine.cpp`; `loom/src/extract/stage.cpp`; `loom/include/loom/knowledge.h` |

These are code-derived findings. No claim about present model pricing, provider
availability or external service behavior is needed to establish them.
