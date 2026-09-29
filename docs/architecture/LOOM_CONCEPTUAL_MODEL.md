# Loom Conceptual Model v1

Status: **binding contract** for every Loom component, data file, API, UI and
agent. Inputs: `MEGA_MASTER_2026-09-16.md`, `OWNER_REQUIREMENTS_2026-09-26.md`
(R1–R13), `NOTATKA_GPT_2026-09-26.md`, the three paradigm-engine design
proposals and the analysis of the recovered versions
(`docs/history/ANALIZA_v0.8.3_v0.9.0.md`).

Rule for everyone: **a term in this document has exactly one meaning.** Code,
table names, JSON keys, C ABI names and UI labels use these words in this
sense. If a component needs a meaning that is not here, it stops and asks for a
model change — it does not invent a local meaning.

---

## 0. The process

Every project the owner runs — software, research, film, music, a legal case —
is an instance of one process:

```
observations  →  epistemic state  →  generalizations  →  decisions / actions / products
(raw, immutable)  (claims + assessment)  (principles, operators,     (materialized under
                                          paradigms, morphisms)       preferences, checked)
        ↑                                                                   │
        └──────────── outcomes, corrections, new sources ───────────────────┘
```

Loom's job is to automate the passage from data and experience to
generalizations, and from generalizations to coherent actions and products —
learning the **generator** of the owner's decisions, not only their history.

---

## 1. Layer A — Sources and observations (immutable)

| Term | Meaning |
|---|---|
| **Source** | An immutable raw input: an export zip, a repository, a file, a recording, an e-mail box, a URL snapshot. Content-addressed (sha256, `loom_sources` + BlobStore or a hashed file reference). Never modified. |
| **Unit** | An addressable part of a source: a conversation, a message, a commit, a file, an e-mail, a recording segment, a document page. Identified by a **Locator** (source + member + byte range / JSON pointer / time range). Units are **catalogued before anything is imported** (R1). |
| **Observation** | An atomic, located piece of content taken from a unit: a sentence, a list item, a heading, a code block, a timestamped utterance, a table row. Every claim that says "observed" points to observations. A **quote** is the text span of an observation. |

Invariant I1: raw sources are immutable; every observation is located and its
bytes are verifiable against the source hash.

---

## 2. Layer B — Epistemic state

### 2.1 Entities and claims

| Term | Meaning |
|---|---|
| **Entity** | A thing the model talks about. It has a **kind** (open set, data: `project`, `component`, `version`, `principle`, `character`, `scene`, `track`, `institution`, `legal_norm`, `hypothesis`, …) and identity maintained by **entity resolution** (aliases, `same_as` merges, splits). Lineage (`evolved_into`, `merged_into`, `forked_from`) is a relation between entities, **never** an alias merge. |
| **Claim** | The unit of knowledge: `(subject entity, predicate, object entity or value, qualifiers)`, where qualifiers include time (`valid_from`/`valid_to`), version, scope and language. Everything Loom "knows" is a claim. There are no bare facts: every claim carries an **Assessment**. |
| **Assessment** | The epistemic core, identical in every domain. It answers seven questions about a claim (§2.2). |

We say **claim**, not "fact": a fact is a claim whose assessment makes it
observed and uncontested. Tables and APIs use `claim`.

### 2.2 The seven questions (the Assessment)

| # | Question | Field | Content |
|---|---|---|---|
| 1 | What is it? | `claim` | subject, predicate, object/value, qualifiers |
| 2 | How is it known? | `basis` | observation support (quotes with locators) and/or a derivation (operator id + version + premises); this is the provenance chain |
| 3 | How certain? | `evidence_class`, `origin`, `confidence` | see §2.3 |
| 4 | What does it depend on? | `premises` | claim ids, principle ids, assumptions |
| 5 | What contradicts it? | `counter`, `status` | counter-observations / claims; `contested` when unresolved |
| 6 | What follows from it? | `consequences` | derived claims, predictions, checks it enables |
| 7 | What is missing? | `open` | unfilled slots or questions it raises |

### 2.3 Evidence class, origin, confidence

**Evidence class** — *how* the value was obtained (closed set, code):

| Class | Meaning | May be a premise of inference? |
|---|---|---|
| `observed` | Stated in an observation (extractor + locator). | yes |
| `derived` | Deterministic function of observed values (max version, date span, count). | yes |
| `inferred` | Produced by an operator over claims/principles; **must** carry an Expected Property (§2.4). | yes, depth-limited |
| `extrapolated` | Philosophy-driven proposal for something not yet existing or decided. Capped confidence (policy). | **never** |
| `absent` | The slot/question has no candidate; the UI shows what would fill it. | no |
| `user` | Asserted or confirmed by the owner. Always wins (I4). | yes |

**Origin** — *from whom/what*, orthogonal to evidence class (closed set, code):
`archive` (the owner's own sources) · `repo` (code and git history) · `user`
(direct statement in the current interaction) · `external_authority` (a
statute, standard, official document) · `model_knowledge` (general knowledge of
a language model, R7) · `system` (Loom's own computation).
Example: "Android apps usually store data in SQLite" proposed by an LLM is
`inferred` + `model_knowledge`; a quoted article of a code of procedure is
`observed` + `external_authority`.

Authority order used when claims compete: `user` > `archive`/`repo` (by recency and
explicitness) > `external_authority` for normative content > `system` >
`model_knowledge`. `model_knowledge` never outranks an observation from the
owner's sources.

**Confidence** is calibrated: a number in [0,1] that, on held-out data, matches
the empirical frequency with which claims of that confidence turn out correct
(ECE is measured). This is what makes "opacity = confidence" honest (R5).

**Conflict** is not an evidence class. It is a state of a claim set: two or
more supported, incompatible values. All values stay, each with its own
assessment; the resolution (later decision, explicit reversal, user) is itself
recorded.

### 2.4 Inference and Expected Property

An **inference** is a claim produced by applying an **operator** (§3.3) to
premises. Every inference carries (R13 §13): method (operator id + version),
premises, confidence, supporting principles, competing alternatives (other
candidate values with scores) and an **Expected Property**.

**Expected Property** — what the system actually vouches for when it infers a
value: a *checkable predicate* from a closed set implemented in code
(`in_class`, `subset_of`, `version_between`, `date_between`,
`consistent_with(principle)`, `available_on`, `not_contradicted_by`,
`exists_symbol`, …), plus a rationale and `confirm_if` / `refute_if`
conditions. The value is only the best candidate; the property is the claim.
When new observations arrive the property is re-checked: `pending` → `holds`
(the value may be promoted to `derived`) or `violated` (demoted, marked
contested). An inference is therefore a falsifiable prediction, not a guess.

### 2.5 Hypotheses and competing models

A **Model** is a named, consistent set of claims and principles that explains a
body of observations. Several models may coexist for the same data (two
readings of the owner's philosophy, two interpretations of a piece of
evidence). Models are scored by explanatory and predictive power (§7), never
silently merged. Principles themselves are hypothesis-like (§3.1).

---

## 3. Layer C — Generalizations (the generator)

### 3.1 Principle

A general statement that constrains or generates decisions.

- **level** (closed set): `value` (what is worth protecting or maximising —
  truth, autonomy, transparency, honesty, preservation of information, minimal
  arbitrariness, optionality) · `epistemic` (how to build a model of reality —
  provenance, fact ≠ inference, explicit uncertainty, keep alternatives, seek
  counterexamples, avoid over-generalisation, update on new data) ·
  `strategy` (how the above become actions — modularisation, capability
  interfaces, KOD ≠ DANE, immutable originals, graceful degradation, defer
  decisions until forced, …).
- **form** (closed set): `invariant` (almost always holds) · `heuristic`
  (usually works) · `default` (preferred when nothing else decides) · `meta`
  (how to create principles) · `conflict_resolution` (how conflicts between
  principles or values are settled).
- **Fields**: `id`, `statement` {pl, en}, `phrasings` (verbatim), `level`,
  `form`, `scope` (domains, project kinds, conditions), `protects` (value ids),
  `derived_from` (more general principles), `evidence_for` (observations and
  decisions it explains), `counterexamples`, `exceptions`, `confidence`,
  `predicts` (situation types → expected solution classes), `conflicts_with`,
  `supersedes`, `validation_status` (`candidate` · `supported` · `confirmed` ·
  `rejected` · `retired`), `owner` (`user` for the owner's principles).

A **Preference** is a principle whose owner is the user and whose scope is the
user's products (language, style, stack, format). There is no separate
preference type; preferences are enforced as checks on products (R11).

The top-level principle of the owner's philosophy, as currently understood
(candidate, to be validated by the engine, not assumed):
*"Find a representation of the problem in which the variety of the world is
data and code describes only universal operations; if a new case seems to need
a new code branch, first check whether it should be a new data value."*

### 3.2 Values and trade-offs

Principles of level `value` are not a scalar utility. A **decision** records,
per alternative, the values it affects (+ / − / 0, with rationale). The
explanation of any decision has the form: *"follows from principle X, which
protects value Y, under constraint Z."* Values may act locally, conflict, and
change priority by situation; `conflict_resolution` principles say how.

### 3.3 Operator

A reusable transformation pattern: **situation type → solution class**, with
justification. This is what lets the system produce solutions nobody wrote down
and predict the owner's next decisions.

- **Fields**: `id`, `situation` (a predicate over the model state, e.g. "a new
  data source appears that is not behind a provider"), `solution` (a change
  template, e.g. "extend the provider/capability registry; do not add a
  separate subsystem"), `principles` (justifying principles), `examples`
  (historical decisions that applied it, with locators), `success`/`failure`
  counts, `confidence`, `validation_status`.
- Seed operators (candidates, from the note): new data source → extend the
  provider/capability abstraction · new artifact type → find the common
  structure, keep the specifics as data · repeated manual action → make it
  observable with provenance, then automate · uncertainty → do not choose
  arbitrarily, keep the alternatives with evidence · implementation-only
  difference → do not propagate it upwards.
- Operators are also the mechanism of inference: rules of the data pack are
  operators with `produces ∈ {derived, inferred, extrapolated}`.

### 3.4 Universal roles (the system meta-model, R7)

Every project describes a system being built. Its parts play one of fourteen
**universal roles** (closed set, code; changing it is a model change):

| Role | Meaning | Software app | Film | Music | Legal case | Research |
|---|---|---|---|---|---|---|
| `intent` | why it exists, for whom | purpose, users | logline, premise, audience | mood, genre intent | desired outcome | research question |
| `constraint` | what binds it | requirements, invariants, licences | genre rules, rating, budget | key, tempo, form | legal norms, procedural rules | ethics, assumptions |
| `part` | constituent units | modules, components | acts, scenes, shots | sections, tracks | proceedings, claims, counts | experiments, studies |
| `actor` | who acts | users, services, agents | characters | performers, instruments | parties, institutions, judges | researchers, subjects |
| `resource` | what is used or consumed | data sources, stores, models, tools | footage, portfolio refs, assets | samples, patches | evidence items | datasets, instruments |
| `interface` | boundaries and channels between parts | APIs, protocols, formats, C ABI | cuts, transitions | modulations, transitions | filings, submissions, channels | protocols of measurement |
| `flow` | what moves between parts | data/control flow, inputs/outputs | plot causality, motif recurrence | harmonic progression | procedural sequence | causal/analytic pipeline |
| `event` | dated happenings | releases, sessions, incidents | timeline events | cues, drops | hearings, deliveries, deadlines | runs, observations |
| `artifact` | concrete source representations | code, docs, configs | screenplay, storyboard | score, MIDI, arrangement | pleadings, letters, transcripts | papers, notebooks |
| `transformation` | how artifacts become outputs | build, deploy, migration | render, compose, edit | synthesis, mix, master | drafting, filing | analysis, simulation |
| `check` | verification | tests, lint, compat tests | continuity, character consistency | mix checks, key consistency | citation checks, deadline checks, contradiction checks | falsifiers, replication |
| `output` | delivered products | builds, releases | cuts, clips | tracks, albums | filed documents, decisions obtained | results, publications |
| `decision` | chosen alternatives with rationale | architecture decisions | creative decisions | arrangement choices | strategy decisions | methodological choices |
| `question` | open unknowns and hypotheses to test | open questions, TODOs | unresolved plot points | open arrangement ideas | open legal questions | hypotheses |

Versions, forks and lineage are **not** a role: they are a temporal dimension
that every entity of every role has.

### 3.5 Project kind, artifact type, paradigm, instance

| Term | Meaning |
|---|---|
| **Project kind** | A data file defining one domain (`software_app`, `film`, `music`, `legal_case`, `research`, …): its **domain kinds** (e.g. `module`, `scene`, `track`, `deadline`), each mapped to exactly one universal role, with slots, cardinalities, constraints, anchors and inference rules. A new kind of project = a new data file (R8). |
| **Artifact type** | A data file defining one kind of document/artifact (`conversation`, `brainstorm`, `specification`, `codebase`, `screenplay`, `score`, `pleading`, `evidence_record`, `email`, `recording_transcript`, …): how it is parsed into observations and claims, and the structure (IR schema) of its instances. |
| **Paradigm** | The umbrella term for project kinds and artifact types (both are templates matched against evidence). |
| **Instance** | A paradigm applied to a subject: a project entity (for project kinds) or an artifact (for artifact types). Its slots are filled by claims, each with its own assessment. Instances are partial by default; `absent` slots are first-class. |
| **Facet** | An optional sub-template of a project kind (e.g. `multiplatform` facet of `software_app` with a `platforms` slot, `pipeline` facet for staged transformations). Facets are reusable across kinds (a `pipeline` facet applies to film rendering and to data analysis). |

Mapping of the draft pack (loom/data/paradigms/*.json) onto this model:
`multiplatform_app` → project kind `software_app` + facet `multiplatform` ·
`agent_system` → project kind `software_app` + facet `agent` · `pipeline` →
facet `staged_transformation` (the 0.9.0 "cheap → expensive, gated,
cost-estimated" refinement is its canonical example) · `brainstorm`,
`specification`, `codebase` → artifact types · `version_history` → the
temporal dimension of every project (not a paradigm) · `coding_philosophy` →
not a paradigm: it is the Principle layer (§3.1) and its discovery (§6).

### 3.6 Morphism

A **structure-preserving map** between paradigms, stored as data:
`{from: kind/slot/relation, to: kind/slot/relation, conditions, transfer}`.
Two uses:

1. **Anchoring**: every domain kind maps to a universal role (§3.4). This is
   what makes a new domain usable on day one.
2. **Transfer across domains**: two domain kinds that map to the same role and
   satisfy the morphism's conditions are analogous (`software.unit_test ↔
   film.continuity_check`, both `check` over `part`). If one project fills a
   role that an analogous project leaves empty, the engine may infer — as
   `inferred`, with an Expected Property derived from the source role's
   constraints — that the target should have it (R8). Transfer depth is 1;
   transferred claims never chain.

Isomorphism search (R4) = finding partial homomorphisms from a paradigm's
pattern into the evidence graph, anchored on a subject, with constraint
propagation (no general subgraph isomorphism). Cross-subject similarity of
filled instances yields `analogous_to` relations.

### 3.7 Areas and generalizations inside artifacts (R10)

A brainstorm (or any artifact) may contain **generalizations** that delimit an
**Area** ("everything is data", "all about data sources: …"). An area is a
region of the model (a set of roles/kinds under a subject) with a generating
statement. The generating statement becomes a principle with
`scope = that area` and `validation_status = candidate`. Listed items become
observed claims in the area; the generalization infers unlisted members
(`inferred`, with the generalization as premise and its content as the
Expected Property) and flags empty areas as gaps.

---

## 4. Layer D — Intent and context

| Term | Meaning |
|---|---|
| **Goal** | What a prompt or task wants, typed by a **goal type** (data: `implement_part`, `write_artifact`, `extend_scene`, `verify_claim`, `compute_deadline`, `brainstorm`, `answer_question`, …). A goal type declares which roles, principle levels and evidence classes are relevant, default resolutions, and the budget split. |
| **Resolution** | The granularity at which an entity or claim cluster enters a context: `label` · `summary` · `full` · `raw` (the underlying observations). Summaries are derived artifacts with provenance (extractive by default; LLM summaries are `derived` + `model_knowledge`-free only when grounded, else `inferred`). |
| **ContextSet** | The goal-directed selection of entities, claims, principles and observations for one model call: maximises relevance × authority × freshness × confidence with diversity, closes over required dependencies, respects the token budget, and records **why** each item is included. Ordered in three bands: **stable prefix** (constitution, invariants, core preferences) → **project context** (slow-changing model of the project) → **goal-specific tail**. Selection is the primary saving; the ordering lets provider prefix caches help for free (R12, R13). |

---

## 5. Layer E — Decisions, actions, products

| Term | Meaning |
|---|---|
| **Decision** | A claim of predicate `decides`: the chosen alternative among recorded alternatives, with rationale (principles, affected values), date, status (`active` · `superseded` · `reverted`), and links to forks. |
| **Fork** | A branch point: in a conversation (edited message), in a design (alternatives), or in code lineage (two versions derived from the same base). Forks keep both sides; the abandoned side is not deleted. |
| **Status** | Of a part/component/feature, **per branch and version**: `implemented` · `partial` · `planned` · `abandoned` · `superseded` · `lost` (existed, then disappeared) · `restored`. Oscillation (lost → restored → lost again) is recorded, not overwritten. |
| **Task / Action / Plan / Checkpoint** | As in the TaskEngine: resumable, hashed, auditable. |
| **Product** | A materialized output of a project kind (code + tests + docs, a screenplay, a mix, a pleading). Generated by a **materializer** from instance state, under the applicable preferences as enforced checks; it lists the claims and principles it depends on (provenance) and is regenerated incrementally when they change (R11). A preference violation is a failing check, not a style note. |
| **Prediction** | An inferred claim about a future decision: "in situation D the owner will choose solution class E", produced by operators with supporting principles and confidence. Predictions are evaluated in the temporal holdout (§7). |

---

## 6. Discovery: from sources to the generator

1. **Catalog** (R1): stream every source, create units with locators and
   sketches, never touching core tables; score units against the
   **self-profile** (built from the repository, the MEGA MASTER and the owner's
   principles); select by policy rules and the owner's overrides; import only
   selected units, by locator. Discovered terms feed back into selection.
2. **Extract**: parse units per artifact type into observations and observed
   claims (entities by kind, relations, versions, statuses, decisions, forks,
   areas and generalizations, normative statements).
3. **Resolve**: entity resolution (PL + EN normalisation, aliases, context
   gates, split guards), lineage including code lineage (nearest revision by
   content per file, then a vote — this recovered 0.8.3 ← 0.7.9 and 0.9.0 ←
   0.7.10).
4. **Assess**: calibrate confidence, detect conflicts, apply user judgements.
5. **Generalize**: match paradigms (project kinds, artifact types) via roles
   and morphisms; discover and type principles (level, form, scope,
   protects); mine operators from sequences of decisions (situation features at
   decision time → chosen solution class); build competing models; run
   inference and extrapolation; produce predictions.
6. **Materialize**: dossiers, the self-description, backlog, extrapolated
   specifications, and — later — products.
7. **Learn**: owner judgements (confirm, reject, edit, merge, split) are
   events; repeated patterns become candidate lexicon entries, rules, operators
   or principles; candidates are evaluated on the benchmarks (§7) and promoted
   only by an explicit step (MEGA MASTER §2.H). Code never rewrites itself.

**Compression**: Loom stores observed claims + generators (principles,
operators, morphisms) + exceptions + provenance; derived and inferred claims
are recomputable from them (derived state is rebuildable, MEGA MASTER §2.F).
The ratio of reconstructed claims to stored generators, at a given fidelity,
is a tracked metric.

---

## 7. Evaluation

1. **Temporal holdout on real data** (the primary benchmark, R13 §9). Sources
   with dates: the owner's historical report (sessions 2026-01-21 → 02-08),
   git history (0.2.0 → 0.7.10, 2026-03-06 fix), snapshots 0.8.x (2026-03)
   and 0.9.0 (2026-04), MEGA MASTER (2026-09-16), requirements and note
   (2026-09-26). Cut at T; induce principles, operators, the project model and
   predictions from ≤ T; compare with what happened after T **at the level of
   abstraction and solution class** (did it predict: provider abstraction for
   new media, generalisation of async jobs, data-driven project kinds,
   intelligent context selection, preservation of optionality, provenance) —
   not feature names.
2. **Real lineage and status ground truth**: code lineage of the snapshots and
   the feature-status matrix in `docs/history/ANALIZA_v0.8.3_v0.9.0.md`.
3. **Synthetic corpora for scale and precision**: generated export archives
   (ChatGPT/Claude shapes, multi-GB padding) about a **fictional** owner and
   fictional projects that are structurally isomorphic to the real ones, with
   exact ground truth, plus a held-out corpus by a different author. They never
   imitate the real owner's words.
4. **Metrics**: selection recall/precision and noise-trap false positives;
   entity and alias F1; version/lineage accuracy; status accuracy per
   branch/version; decision/supersession/fork accuracy; principle recall and
   typing accuracy; operator predictive accuracy; slot value accuracy;
   **false-certainty rate** (inferable/absent reported as observed — hard
   gate); Expected-Property soundness; **calibration (ECE)**; context
   efficiency (tokens and answer quality vs naive baselines); determinism.

---

## 8. Invariants (enforced in code)

- **I1** Raw sources are immutable; observations are located and verifiable.
- **I2** Every claim has an assessment; nothing is presented without evidence
  class, origin and confidence.
- **I3** Inference ≠ fact: inferred and extrapolated values are never returned
  or displayed as observed; extrapolated values are never premises; transfer
  and analogy depth is 1; `model_knowledge` never outranks the owner's
  sources.
- **I4** The owner's judgement wins; it is recorded as an event and replayed on
  every rebuild.
- **I5** Determinism: same sources + same pack hash + same judgements → same
  ids and byte-identical outputs (ids are content-derived).
- **I6** Closed sets live in code: evidence classes, origins, principle levels
  and forms, validation statuses, universal roles, resolutions,
  Expected-Property predicates, operator ops. Open sets live in data: entity
  kinds, relation types, project kinds, artifact types, facets, morphisms,
  principles, operators, goal types, lexicons, policies.
- **I7** Competing models coexist; conflicts are explicit; nothing is silently
  overwritten.
- **I8** Every product lists what it depends on and passes the checks derived
  from the applicable principles and preferences.
- **I9** Capability honesty: when a capability is missing, the result is
  marked unverified at a lower evidence class — never a fabricated metric.
- **I10** (relaxed 2026-09-29, owner decision D1) Python compatibility is no longer
  required. The legacy Python-parity modules and core schema v4 stay as regression
  sentinels until a deliberate migration; this model lives in `loom_*` tables and new
  modules, and new schema is allowed when deliberate (migration + note).

---

## 9. Responsibilities (working split)

- **LEM** — epistemic representation and operations on knowledge, value and
  provenance states. Layers B–C of this model are where LEM maps in; the
  representation here is kept neutral so LEM can be adopted later without
  changing the semantics (open decision, MEGA MASTER §10).
- **Loom** — runtime and transformation: storage, catalog, graph, execution,
  materialization, project compiler, context engine, artifact pipelines.
- **ChatADHD** — interaction and model synchronisation: conversation,
  preference learning, corrections, intent, navigation, steering.

---

## 10. Glossary (PL ↔ EN, one meaning each)

| EN | PL | Not to be confused with |
|---|---|---|
| source | źródło | unit (a part of a source) |
| unit | jednostka (rozmowa, wiadomość, plik…) | observation (a located span inside a unit) |
| observation | obserwacja | claim (knowledge derived from observations) |
| claim | twierdzenie | fact (a claim that is observed and uncontested) |
| assessment | ocena epistemiczna | confidence (one field of the assessment) |
| evidence class | klasa dowodu | origin (who/what it comes from) |
| inference | wniosek (inferencja) | extrapolation (proposal for what does not exist yet) |
| expected property | oczekiwana własność | the inferred value itself |
| principle | zasada | preference (a principle owned by the user) |
| operator | operator transformacji | rule (a pack operator producing claims) |
| universal role | rola uniwersalna | domain kind (a data-defined kind mapped to a role) |
| project kind | rodzaj projektu | artifact type |
| artifact type | typ artefaktu | artifact (an instance) |
| paradigm | paradygmat (rodzaj projektu lub typ artefaktu) | instance |
| morphism | morfizm (odwzorowanie struktury) | alias/merge |
| area | obszar (zakreślony uogólnieniem) | theme/cluster |
| decision | decyzja | status |
| fork | rozgałęzienie | version |
| status | status (per gałąź i wersja) | evidence class |
| goal | cel prompta/zadania | intent (a role of the project) |
| resolution | rozdzielczość (etykieta/streszczenie/pełne/surowe) | confidence |
| context set | zestaw kontekstu | memory |
| product | produkt (zmaterializowany wynik) | artifact (any document, including sources) |
| prediction | przewidywanie | extrapolation |
