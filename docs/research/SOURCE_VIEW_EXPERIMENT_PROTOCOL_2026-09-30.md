# Source-view intervention, frozen before its first model calls

Hypothesis: a refutation question phrased as whether a speaker has ever denied
or withdrawn a relation can retain historical denial after that same speaker
reaffirms it. A recipe explicitly asking for the latest applicable commitment
should reduce this error without changing support, attribution or the temporal
prefix. This hypothesis was identified in an independent mechanism audit of the
original graph recipe, before lifecycle model outcomes existed.

The independently authored `source_view_lifecycle_v1` contains 12 new bilingual
development conversations and 48 queries (18 supported, 14 refuted, 16 unknown).
It covers reaffirmation, positive withdrawal, attributed quoted withdrawal,
repetition, another speaker's reply, withdrawn denial and silence. This is a
targeted diagnostic development panel, not a blind validation corpus. Whole
conversations and their bilingual templates are correlated.

Two recipes receive identical node inventories, queries and physically truncated
source prefixes. Historical-refute v1 exactly reuses the graph-panel Jev recipe.
Active-refute v2 changes only q02 instructions/criteria. q01 support, state,
questions' type, provider/model, fixed strictly-greater-than-0.5 threshold and
scoring remain identical. The active question states that a later affirmation
by the same attributed speaker replaces that speaker's old denial; another
speaker cannot withdraw it. Withdrawing a negative commitment never affirms
the positive relation. These are policies of source commitment, not world truth.

The intervention is a recipe/task-profile measurement, not a model-global
reliability estimate or repeated-call stability experiment. Compare full
three-class confusion, per-class precision/recall and availability, retaining
every planned query, six family profiles, both languages and every discordant
ID. Each first response and probability is kept. Conflicting support/refutation
is unavailable, never forced into a correct label. Report unknown billing
separately. An always-unknown baseline scores 16/48 with zero positive/negative
recall; undefined precision remains null.

Keep v2 only as an experimental recipe if it improves refutation errors without
concealing support/unknown regressions. Otherwise investigate its counterexamples
and retain both variants. No threshold tuning, gold change, paid retry, canonical
graph mutation or content-confidence-to-fact promotion is authorized by this
protocol. The withdrawal events are a research extension and remain outside
the current extraction ABI.

Execution uses the unchanged bounded Jev runner: two immutable manifests of
48 first requests, USD 0.048 reservation each, USD 0.10 batch ceiling, and the
same existing **nonresetting global USD 2 key limit**. No fresh per-job USD 2
is allocated. Root accounting additionally retains USD 0.001366 for the prior
graph GPT HTTP 400 with unknown cost. Fresh key GETs precede paid requests;
mandatory usage and exact raw/ledger cost consistency are replayed offline.

`source_view_experiment.py` prepares specs without reading gold or a key,
hash-pins the independent fixture, preserves prefixes and changes only q02.
Eight mechanism tests pass before inference, including future mutation,
unchanged q01, isolated recipe data, unavailable denominators and hard billing
mismatch failure. These tests do not measure model quality. Fixture integrity
has 15 separate authored mechanism checks.
