# Additional research directions and discriminating experiments

2026-09-30. These are proposals, not newly asserted owner requirements or measured
model capabilities. All initial implementations and experiments below are
offline: scripted transports, deliberately authored mechanisms and already saved
responses. Paid/model-quality measurements wait for owner archive import and a
concrete authorized plan. Frontier models are first-class analysis instruments,
not merely expensive judges of cheap-model output. Every method can be configured
for selected material or the whole archive; no scope/model/reasoning/acceptance
limit is invented here.

## Immediate shortlist

| Priority | Distinct hypothesis | Baseline/control | Primary criterion and preserved result | Information sought |
|---|---|---|---|---|
| 1 | The same graph packet can support extraction, inventing structures/types, merge/split, criticism, gap discovery and whole-archive reasoning without a second truth store. | Current extraction-only candidate packet; no-op/manual roundtrip. | Exact source/claim roundtrip; proposal diff replay; every changed record carries origin, action and dependencies; original graph hash unchanged before acceptance. | Which universal operations and metadata the exchange contract actually needs, rather than guessing an irreversible schema. |
| 2 | Acceptance choice is independent of epistemic provenance. | Review preset versus configurable automatic acceptance on identical scripted outputs. | Identical candidate/source bytes; only acceptance/event projection differs; no origin or evidence laundering; no unconditional review ban. | Whether application defaults accidentally reduce user options or manufacture facts. |
| 3 | A physical source/graph prefix is required for a causal pipeline; later events must not alter earlier results. | Full extraction plus cutoff; prefix extraction with later valid/malformed/cross-source extensions. | Earlier request/prefix hash and output remain invariant; retain every failing extension; separately report mechanism and actual extraction quality. | Source-time filtering versus genuine availability/future-information independence. |
| 4 | A frontier agent's discovery→critique→repair loop can recover structures a single pass omits. | Same model single pass, and same total token/cost allowance with independent parallel proposals; scripted known defects first. | Node/edge/source-act P/R separately; false certainty; unresolved ambiguity; operation/time/cost denominators; all intermediate diffs retained. | Benefit of interaction/tool-use rather than merely extra compute or voting. |
| 5 | Representation loss predicts downstream errors better than undifferentiated “model confidence”. | Raw-source context, typed graph, summary, token/char/MiniLM views at matched task and context budget. | An explicit loss ledger per transform plus downstream answer/spec fidelity; counter-evidence and exceptions retained; missing capability stays unmeasured. | Which information each representation discards or invents, and where a richer representation is worth the cost. |
| 6 | A source-grounding critic can expose unsupported typed claims that locators cannot detect. | Exact locator validation; source-only critic; actor/direction/scope-aware critic; independent human-adjudicated rubric later. | Separate grounding P/R and false-certainty rate on seeded wrong direction/actor/negation/nonendorsement; quote bounds unchanged. | Whether semantic verification is a separable useful instrument, not circular model self-explanation. |

## Wider independent tracks

| ID | Hypothesis/operation | Baseline and one variable | Criterion; keep / revert / investigate rule |
|---|---|---|---|
| D07 | **Ambiguity-preserving graph construction.** A model can output competing interpretations and explicit unresolved slots instead of choosing an unsupported reading. | Single chosen interpretation versus explicit alternatives; same evidence. | Supported interpretation recall, unsupported commitment precision, ambiguity coverage and downstream forced-error rate. Keep if recovered alternatives help without false-certainty increase; unranked alternatives are not a truth probability. |
| D08 | **Model-discovered types and operators.** Frontier models propose unnamed patterns, type definitions and reusable transformations, with examples and scope. | Fixed manually supplied types; same input, no examples/gold leaked into discovery. | Definition portability to unseen project/domain, candidate coverage, validity of typed instances, and downstream reasoning gain. Keep competing definitions until data distinguishes them. |
| D09 | **Inverse decision-generator induction.** Recover principles/operators that predict later solution classes, not merely restate feature history. | Recency/nearest-decision retrieval and feature-name matching. | Predict-before-observe genealogy, solution-class P/R, counterfactual principle removal, temporal leakage checks. Do not touch forbidden existing blind answers; require a new authorized time split. |
| D10 | **Counterfactual evidence necessity.** Remove one supporting observation/principle and recompute consequences. | Same packet with irrelevant removal. | Correct dependency invalidation, alternate support retained, products invalidated only when support genuinely lost. No induced world-truth claim from a source manipulation. |
| D11 | **Order of graph transformations matters.** Extraction→resolution and resolution→extraction may produce different merges/edges. | Matched inputs/operators with one order swap. | Identity precision/recall, source binding and reversible merge/split diffs; preserve both orderings. Investigate disagreements before selecting a default recipe. |
| D12 | **Branch-aware status and genealogy.** A statement valid on one fork must not silently supersede another fork or rewrite history. | Flat latest-turn state versus explicit branch lineage. | Per-branch status accuracy, reconstructed historical snapshots, convergence versus same_as confusion. Keep original fork provenance and all abandoned alternatives. |
| D13 | **Evidence independence accounting.** Copies, quotations and model summaries of one observation should not count as independent corroboration. | Naive number-of-supports versus source/derivation dependency graph. | Duplicate-source amplification, effective independent-support denominator and contradiction sensitivity. Corroboration is not automatically world truth. |
| D14 | **Budgeted active query selection.** Choose the next instrument/task/recipe based on expected discriminating information, not one global reliability score. | Fixed cheapest-first and random query allocation. | Error reduction or ambiguity resolution per cost/time; retain acquisition scores and all attempts; compare matched total allowance. User budget remains configurable; unknown cost cannot be certified zero. |
| D15 | **Reasoning representation transport.** A useful abstraction in one domain may transfer via an explicit structure-preserving map rather than word similarity. | Token/embedding similarity and direct structural alignment. | Mapping property soundness, transfer P/R, exception violations and predicted_at before target confirmation. Transfer output stays inferred/extrapolated; no assumed global manifold. |
| D16 | **Compression sufficiency.** Generators + exceptions + source pointers may reconstruct task-relevant knowledge more cheaply than large histories. | Full history and flat summary at fixed tasks. | Reconstruction fidelity, exception/negation loss, answer/spec quality and token count separately. Keep only savings with acceptable measured fidelity; no scalar “compression score” hides loss. |
| D17 | **Minimal distinguishable argument sets.** Preserve several premise-minimal support/counter-support paths rather than one shortest explanation. | Shortest-path proof or top-1 retrieved source. | Alternative-support recall, counter-evidence reachability, bounded enumeration completeness and cost. A bound hit is incomplete/unavailable, not all alternatives found. |
| D18 | **Local geometry without global linearity.** Small local charts may distinguish reasoning forms that one embedding distance collapses. | Single cosine metric; graph edit/WL/alignment baselines. | Held-out local neighborhood precision/recall and transport consistency after controlling topic/style/length/renderer. Never interpret similarity as probability or truth. |
| D19 | **Sufficient context by thesis.** A product plan can retrieve support, objections and gaps independently for each thesis. | Whole-history and one flat retrieval query at matched budget. | Per-thesis evidence/objection recall, unsupported output and spec fidelity; exact included/omitted source reasons. No channel may veto another without a declared selected policy. |
| D20 | **Adversarial critique of graph semantics.** Seed a plausible false merge, swapped direction, stale commitment or topic-only edge, then ask a frontier critic/agent to challenge it. | Locator/type validator and same model self-critique without sources. | Seeded-defect recall and false accusation precision; inability to decide retained, not fabricated resolution. Counterexample generation and evaluation use separate packets. |
| D21 | **Profile drift per task/recipe/version.** A new prompt or provider version may fix one task and worsen another even with stable hard decisions. | Frozen previous profile/recipe. | Per-class P/R, calibration where ground truth permits, coverage/cost/latency and family-specific failures. Keep first profiles and validity intervals; do not inherit a global reliability number. |
| D22 | **Values and Pareto alternatives.** A frontier method can explain how competing choices protect different values instead of forcing a scalar utility winner. | Scalar weighted score or one recommended action. | Omitted trade-off/constraint rate, source fidelity and later owner correction; uncertainty about values remains explicit. User can select any acceptance/default strategy. |
| D23 | **Cross-modal evidence alignment.** Align audio speaker/time, transcript, attachment, code and diagram references while retaining uncertainty and raw bytes. | Text-only extraction with attachment filenames. | Alignment P/R, unsupported speaker/time precision, reconstruction fidelity and unresolved pointers. Initial tests are scripted; absence of real owner modalities remains a stated gap. |
| D24 | **Research self-model in the graph.** Experiments, instruments, packets, first responses, metrics, decisions and failure hypotheses can share ordinary graph provenance. | Disconnected reports/CSV artifacts and hand-made model trust. | Queryable metric denominator lineage, exact replay and no hypothesis→fact promotion. This is a projection of canonical experiment/source evidence, not a rival knowledge store. |

## Design of comparisons

Model complexity, number of calls, reasoning settings, input representation,
candidate supply, tool access and acceptance policy are independent variables.
Comparisons change one declared variable, or declare a task contrast rather than
claiming a matched ablation. Model first proposals and explanations are retained
as instrument outputs, not ground truth. Agent roles can specialize (discover,
criticize, bind sources, resolve identities, reconstruct time, propose types,
choose probes), but independently supplied evidence is needed to establish a
real diversity benefit; unanimous agents can share an identical wrong assumption.

For optimization, a broad offline screen selects useful mechanisms; expensive
finalists receive a separately frozen real-archive experiment. Development,
validation and holdout remain separate. A discovered counterexample becomes DEV
for later methods, so it cannot retroactively certify a new method as blind.
Store first outcomes and denominators, compare to a simple baseline, then decide
keep/revert/investigate. Do not run a full Cartesian product for its own sake.
