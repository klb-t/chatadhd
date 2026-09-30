# Source-view lifecycle v1 — preregistered independent diagnostic DEV

Registered 2026-09-30 before method evaluation on these newly authored cases.
The fixture author has not read the new graph-panel method responses or sealed
validation. No API, credential, index, model, prediction or production graph is
used to author/freeze this fixture. This is **judgment-only diagnostic DEV**,
not a holdout or a free-extraction benchmark.

## Question and fixed panel

Does a method distinguish retained historical denial/withdrawal from the
**current source commitment** after reaffirmation? The existing Jev graph v1
recipe asks for an active positive assertion but phrases refutation as denial
or withdrawal "by as_of". Both could be returned after a source denies then
reaffirms the relation. This is a source-based hypothesis, not a finding from
method outputs. Parent will compare unchanged v1 against a separately frozen
recipe clarifying active refutation; first responses stay separate.

Author exactly **12 short conversations**, six whole families with one PL and
one EN version each, **four queries per case / 48 queries total**:

1. positive assertion → explicit denial → explicit reaffirmation;
2. positive withdrawal without replacement, followed by irrelevant speech;
3. attributed quoted withdrawal, without reporter endorsement;
4. repeated positive assertions at distinct times;
5. a different speaker's reaffirmation cannot erase the first speaker's denial;
6. withdrawal of a denial without replacement → no current position, with
   silence and other-attribution controls.

All families are diagnostic development. Translations remain together and are
correlated; no family/language version is promoted to an independent sample.
Family labels occur only in evaluator-side gold/manifest. Inputs retain the
existing graph-panel case/query shape, allowing unmodified `query_payload` to
construct each physical causal prefix. Never send the gold, full manifest,
diagnostic family names or later turns as state.

## Declared latest-active-source-view policy

Policy identity: **`source_view.latest_active_commitment/1`**. Unit of state is
the exact `(relation, source proposition, target proposition, attributed
speaker)` at `as_of`, independently of reality:

- `supported`: the latest applicable source commitment is an explicit positive
  assertion, including a reaffirmation after an earlier denial/withdrawal.
- `refuted`: an active explicit negative commitment, or an explicit withdrawal
  of a previous **positive** commitment that has not been replaced. Positive
  withdrawal maps to this label by the declared source-commitment policy; it
  is **not** an assertion that the relation is false in the world and does not
  fabricate a negative source-assertion edge.
- `unknown`: no applicable stance, missing/irrelevant speech, another speaker's
  stance, or explicit withdrawal of a **negative** commitment with no new
  position. Withdrawing a denial does not logically assert the positive relation.

Explicit replacement events deactivate the old commitment in the latest view,
but never delete the old observation/assertion. Reaffirmation replaces the old
negative commitment; a historical negative is not simultaneously current
refutation. A repeated positive statement is a new independently dated source
assertion, not a second evidence-independent world fact, and does not imply a
correction/supersession event without explicit revision language.

Quoted speech belongs to the named quoted speaker, not the reporter. An
attributed reported withdrawal changes only that quoted speaker's source view
under this policy; it is not verified as a real-world speech act. Irrelevant
speech and silence do not reactivate a withdrawn assertion or erase an active
denial. Direction, relation type, attribution and cutoff must all match.

## Research records and compatibility boundary

`inputs_dev.json` has the existing
`loom.research.graph_methods_panel.inputs/1` root and unchanged case fields:
id, language, source_id, turns, node_inventory, judgment_queries. Turns have
exact text/speaker/known_at. Gold retains every source assertion and separately
dated status event, exact full-turn quotes, source/turn identity, character and
UTF-8 byte coordinates. Content truth remains **unverified** throughout.

Gold's ternary `judgments` are compatible with the existing judgment scorer.
Lifecycle annotations are a **research gold extension**, not a production ABI:
`superseded` points to an explicit replacement assertion, while `withdrawn`
has `superseded_by: null`. The current graph extraction compiler requires a
replacement assertion and cannot represent withdrawal alone. Do not coerce
null withdrawal into supersession or claim extraction compatibility. No current
fixture/compiler/scorer is changed by this panel.

Every judgment includes the source-view trace and supporting evidence IDs.
Gold earlier/later views use only events/assertions with known_at <= as_of;
future conclusions are not backfilled. The raw source graph remains historical
evidence, and the active view is a reconstructable policy projection.

## Freeze, integrity and later method scoring

Freeze source input, gold, policy, authoring code and adapter dependency hashes
in `manifest.json` before any predictions. Mechanical checks establish the
12/48 inventory, exact quote coordinates, unique source/query IDs, attribution,
null-withdrawal preservation, active/historical distinction, repeated-event
retention and that unmodified `query_payload` supplies no future turn. Mutating
future turns must not affect an earlier request. These checks are **mechanism
validity**, not measured Jev/LLM quality.

Later compare methods on identical supplied prefixes/queries, saving first raw
responses, exact recipe/model/provider hashes, costs and failures. Primary
metrics: full 3×3 confusion, per-class TP/FP/FN precision/recall, supported vs
refuted confusion, unknown vs refuted confusion, invalid coverage, and family /
language breakdowns. Keep all 48 query denominators; missing/invalid are wrong,
not guessed unknown. Always-unknown is the fixed baseline. Reaffirmation
temporal pairs and distinct-speaker/quoted-speaker controls must be reported
separately. A binary both-positive outcome is a conflict/invalid view, not fact.

No current method outcome, paid authorization, natural-text performance,
world-truth accuracy, model calibration or validation release follows from this
fixture. Parent separately freezes/authorizes any v1/v2 model experiment.
