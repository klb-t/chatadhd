# Empirical profiles v1 — conditional research projection

`profiles.json` projects already measured first results into **12 task/recipe
profiles**. It makes zero new model calls, changes no canonical graph/store,
model routing or confidence policy, and has no global model reliability score.
This is `loom.research.empirical_profiles/1`, explicitly
`research_projection_not_canonical`, not a replacement T4 contract.

The first projection was created at **2026-09-30T03:22:25.721855+00:00** and is
preserved exactly as `first_projection.json`;
SHA-256 `f2e25f579cea38aca5e71e1e62f820d0bb11b23c39961b3b6ee0a6be57c3b3f3`.
The corrected `profiles.json` is revision2, created at
**2026-09-30T03:31:22.114813+00:00**, SHA-256
`bd0eefa76c41526f4d7c458a95ad0accb12f34869781217b69519cdad44bda99`.
These timestamps record when the derived artifacts were created. Exact original
metric-publication `known_at` was not recorded and remains **null**. Each batch
separately preserves the actual first-response start/completion interval from
its ledger. Completion timestamps do not establish when a metric was scored or
published. No result is backdated to projection creation or a date-only report.

## Scope and denominator inventory

| Profile / operation | Effective evidence scope | Primary result with denominator |
| --- | --- | --- |
| `context.topic_needed` | 8 authored development conversations, 48 causal queries, 84 supplied topic bits | TP36 FP1 FN0 TN47; precision36/37, recall36/36; exact sets47/48 |
| `context.claim_selection` | Same 48 queries, 44 eligible supplied Claim bits | TP16 FP2 FN0 TN26; precision16/18, recall16/16; exact sets46/48; nonempty-candidate subset22/24 |
| `graph.jev_dual_noul` | 24 new DEV conversations, 96 supplied attributed/directed/temporal edge queries | 86/96 correct; 90/96 semantic labels; six retained dual-channel conflicts |
| `graph.gpt_ternary_v2` | Same 96 graph queries, separate ternary JSON recipe | 81/96 correct, 96/96 labels; supported P31/38 R31/36, refuted P22/30 R22/24, unknown P28/28 R28/36 |
| `graph.gpt_source_assertions` | 24 source packets with node inventory; no judgment queries sent | TP56 FP6 FN4; record precision56/62, recall56/60 |
| `graph.gpt_status_events` | Same extraction requests, distinct status-event operation | Frozen strict convention TP0 FP9 FN6; precision0/9, recall0/6; convention ambiguity retained |
| `graph.gpt_judge_v1_transport_failure` | Separate original JSON-mode plan; one HTTP400, 47 never attempted | 0/48 label coverage; conditional semantic accuracy unavailable; one unknown-cost attempt |
| `pairs.gpt_same_structure` | 48 previously inspected exploratory pairs; inherited “validation” subset is not blind | TP32 FP0 FN0 TN16; precision32/32, recall32/32 |
| `t3.expressed.object_meaningful` | Known authored T3 first16 cases, expressed-source question | TP5 FP0 FN0 TN11, all16 available; Brier0.0278125 |
| `t3.expressed.string` | Same cases/question, string-format recipe | TP5 FP0 FN0 TN11, all16 available; Brier0.03416875 |
| `t3.inferred.object_meaningful` | Same cases, bounded candidate-inference question | TP10 FP0 FN0 TN6, all16 available; Brier0.03981875 |
| `t3.inferred.string` | Same inference task, string-format recipe | TP10 FP0 FN0 TN6, all16 available; Brier0.02984375 |

Exact task/recipe/model/provider keys, per-class confusion and family/language
counts remain in the JSON. Jev graph supported P34/34 R34/36, refuted P19/23
R19/24 and unknown P33/33 R33/36 are separate. Its correction-family refutation
recall is only **1/6**; the aggregate must not conceal it. GPT's corresponding
correction-family refutation recall is4/6. Missing/conflicting answers stay in
their gold-class FN denominators and are not guessed `unknown`.
Jev's ten unsuccessful judgments include the six conflicts; the two recorded
failure-mode counts overlap and must not be added as sixteen separate errors.

The context selector has 24 empty-Claim queries; these must not inflate a claim
selection capability claim. At the predeclared .2/.8 selective band it retains
33/36 topic positives but **3/16 Claim positives**, despite zero retained errors.
Selecting unknown-time Claims does not establish a historical prior. The older
pair48/T3 named validation subsets are already known exploratory evidence;
sealed new validation labels were not accessed. Current lifecycle method
responses and future validation releases are excluded from this version.

These are correlated authored families, translations, candidate pools and
temporal views. They do not measure natural-text population accuracy,
archive-wide retrieval, free node discovery or world truth. Positive bounded
T3 inference remains a research candidate, not an observed source assertion.
The four T3 task/format profiles share requests; perfect decisions do not choose
a format winner, and the recorded paired Brier intervals include zero.

## Instrument, recipe and provenance

Actual Jev ledger identifiers are `typesafe/jev-1.13-20260917` / `TypeSafe`,
requested via `typesafe/jev-1.13` with provider constraint `typesafe`. GPT first
response metadata was hash checked across all completed included GPT calls and
returned `openai/gpt-4.1-mini` / `OpenAI`, requested with provider constraint
`openai`. GPT's returned identifier is an **unversioned alias**, not a proven
immutable weights snapshot. Jev's dated identifier also does not verify weights.
The failed V1 request has no returned model/provider identity; only its request
constraints are known. No inherited assessment across versions/providers.

Each strict fragment points to a precise saved recipe source. The sidecar binds
literal prompt/recorded recipe hashes, or a clearly identified post-result
identity hash of the saved frozen question object. Those derived identity
digests are not claimed as new preregistration. Context also retains the dynamic
question-map hash and scorer hash. The 24 source entries record exact current
SHA-256, real commit membership when available, and ZIP member/container identity
where applicable. At this first snapshot, **12 sources matched committed
container bytes and 12 did not**. `repository_base_commit` is a checkout stamp,
not a claim that uncommitted result bytes occurred in that commit. Later commits
do not rewrite these original provenance statuses.

The projection reads explicit first-result reports/manifests/ledgers and the
T3 archive's manifest/ledger, not gold files, model credentials or indexes.
Existing reports supply their recorded gold hashes and counts. Raw GPT response
metadata inspection is limited to returned model/provider identities and byte
hash verification; the projection does not rescore model responses.

## Cost and timing boundary

| Request batch | Actual attempts | Usage-reported known USD | Recorded request seconds, sum |
| --- | ---: | ---: | ---: |
| Context topic/Claim shared |48|0.005774496|11.919899|
| Graph Jev dual Noul |96|0.002989266|22.004745|
| Graph GPT V2 ternary |96|0.0196232|99.851777|
| Graph GPT assertions/events shared |24|0.0188832|94.708727|
| Original GPT V1 failure |1|unknown|0.254900|
| GPT structure pairs |48|0.0111276|35.247451|
| T3 four question/format profiles shared |32|0.000950964|8.187729|

Costs live once at request-batch level, not once per profile or per question.
All included Jev generation audit GETs were unavailable; their recorded usage
is available but independently verified invoice totals remain null. Other
runner paths did not provide an independent generation invoice audit either.
For failed V1, JSON's `known_usage_total_usd: "0"` is the sum of **zero known
usage entries**, alongside `known_usage_cost_attempts:0` and
`unknown_usage_cost_attempts:1`; it is **not a claim of a free request**. Its
USD0.001366 reservation is retained separately and is not an actual charge.
No shared key balance delta is assigned to these potentially concurrent arms.

Jev recorded timers exclude generation/key GETs. The broader attempt intervals
include local parsing/persistence and accounting operations, so their residual
is not pure GET time. GPT timings follow the frozen runner's request timer.
These are sums/distributions of recorded durations, not end-to-end wall-clock
or a hardware-controlled model latency comparison. Per-question latency/cost
remain null; multiquestion request timing is never divided into invented
independent question timings.

## Existing contract checks and explicit mapping gap

The unchanged T4 `$defs/profile` validates **12/12 fragment shapes**. Separate
artifact checks cover unique profile IDs, population reconciliation, ratio
arithmetic, evidence references, unallocated question cost/latency, known usage
totals against original scores and saved GPT response hashes. These are not
the existing Python validator's full-envelope semantic validation.

The richer research root deliberately fails `loom.model_profiles/1` root
validation: its schema/fields, timestamp/status metadata and honest uncommitted
source records have no complete existing envelope mapping. The full T4 source
schema requires a source commit; no fake HEAD mapping or weakened contract is
used. Exact response/measurement times, semantic conflicts versus transport
states, requested/returned aliases, billing availability and observed ECE need
explicit later mapping. An unconstrained `batches` object could carry some of
these fields, but the existing schema/validator would not establish their
meaning or correctness. No migration is performed here.

T3 per-arm empirical five-bin ECE remains a diagnostic metric, while strict
`calibration.ece` remains null and `empirically_certified` false. The historical
T4 generator remains valid with14 profiles; its existing **35/35 tests** pass.
No new mirror-implementation tests were added. A read-only independent contract
review confirmed these fragment/full-document boundaries.

Reproduce without overwrite (dependency path is the already installed session
environment, not a new package install):

```sh
PYTHONPATH=/workspace/scratch/34e008d7a951/research-recovery/contract-deps \
  python -B docs/research/empirical_profiles_v1/build_profiles.py --check
```

The first plain-Python attempt failed before artifact creation because
`jsonschema` was not on its path; the original error is retained. The already
installed dependency environment produced the first projection and an identical
offline re-derivation. Logs and `VALIDATION.json` preserve these checks.

An independent read-only reviewer recounted all12 profiles' numerators,
denominators and Brier/ECE scalars against their cited first-score aggregates.
Two provenance repairs followed with the original projection preserved: failed
V1 now explicitly has no returned-identifier evidence, and reproduction checks
the ZIP container hash/blob as well as the member hash. A container-only mutation
with unchanged manifest member was rejected in an in-memory probe; no source
bytes changed. The exact pre-repair builder is retained as `first_builder.py`,
SHA-256 `c58eb49baa90f8fe39b8047ccfcecb5d6b289028fd2500894c397dff7accb142`;
it was reconstructed by reversing only the recorded repair and checking that
its hash equals the independently recorded original hash. The same in-memory
container-only counterexample is accepted by that original builder and rejected
by the repaired one. This is a provenance-mechanism result, not a model result.
All12 profiles, measurements, costs and source provenance rows
are identical between the preserved first projection and corrected revision;
`provenance_repair_check.json` records the comparison.

Keep this version as the measured first-result projection. A later lifecycle or
validation release belongs in a separately identified version with its own
recipe, population/blinding scope and source hashes; it must not overwrite
these DEV results or enable automatic promotion.
