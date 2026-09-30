# Independent T7 review — first findings, 2026-09-30

Reviewer: separate repository-reconciliation agent, after implementation and
first development outcomes. This is independent checking of supplied software
and artifacts; **not an independently authored semantic validation corpus**.
Only `synthetic_dev/ground_truth.json` and the public development run artifacts
were read. No holdout/validation key, API, secret, or canonical graph was used.
Implementation, policy, thresholds and first outcomes were not changed.

## Decision

Keep the optional candidate-only prototype as a reproducible **development
graph-completion mechanism**. No production promotion or claim of temporal
prediction, text extraction, or situation-aware reasoning is supported.
The measured v2 improvement establishes a local-premise eligibility effect;
its weighted structural ranking has no demonstrated capability benefit over
an eligibility-matched frequency control. A second premise-representation
counterexample needs a future version or a documented restriction.

## Independent reconstruction and first outputs

New reviewer file: `loom/tools/seeding/test_independent.py`. Fixture labels and
all counters are reconstructed without importing prototype `metrics` or
`build_graph`. Prototype imports are confined to three adversarial mechanism
probes. Review-test SHA-256 at the first successful run:
`fcab41f13d4c87b0f8aedec11a6e697bc1bbe8882bcee2b479a35347b57878d6`.

First test execution, preserved here before follow-up implementation:

```text
python -m unittest loom.tools.seeding.test_independent -v
Ran 10 tests in 1.357s
OK
```

Combined author/reviewer rerun:

```text
python -m unittest discover -s loom/tools/seeding -p 'test_*.py' -v
Ran 28 tests in 2.026s
OK
```

The frozen v1, first v2 and metadata-corrected v2 runs each contain 61 cases:
46 one-element masks plus 15 no-removal controls, from five project folds.
Cases within a project share data; these are not 61 independent samples.
Checks independently confirmed:

- All fixture/protocol/policy/code hashes and v1 original uncompressed hashes.
- Exactly reconstructed visible target edges, control identities and gold masks;
  no target prose, source quotes, event histories or oracle properties occur in
  the predictor target view.
- Donor projects differ from the target; candidate labels and source paths are
  donor-derived; every preserved relation exists on both sides. Only donor
  vocabulary is available, and the targets' hidden project-specific feature
  names have zero coverage.
- **1,836 metric dictionaries**: three runs × nine groups × two budgets ×
  (two primary methods + 32 random seeds). TP, FP, FN, emitted/positive
  denominators, precision, recall, abstentions and vocabulary coverage agree.
  Random expectation was independently recounted for all 54 group/budget/run
  combinations; all **5,856 serialized random rankings** were reproduced from
  donor vocabulary, seed and case id.
- All random/frequency score dictionaries are identical between v1 and v2;
  the metadata-corrected v2 has the exact same complete score dictionary as
  the original v2. Current candidate `known_at = predicted_at`; donor historical
  know-times and `premises_known_at` are unknown, with creation dates separate.

## Measured outcomes and the simpler control

Numbers below are `TP / emitted` for precision and `TP / hidden` for recall.
False positives on no-removal controls remain in the denominator; they mean
closed-world reconstruction errors, not proof that an extrapolation is false.

| All tasks | Budget 1 precision / recall | Budget 3 precision / recall |
|---|---|---|
| v1 weighted mapping | 7/61 / 7/46 | 16/181 / 16/46 |
| v2 local-premise mapping | 13/55 / 13/46 | 15/149 / 15/46 |
| Original unfiltered frequency | 8/61 / 8/46 | 16/181 / 16/46 |
| Same v2 eligible pool, frequency ranking | **14/55 / 14/46** | **16/149 / 16/46** |

The last row is an **exploratory reviewer ablation**, proposed after inspection
of existing development outcomes, not a preregistered quality result. It orders
the *same* retained v2 candidates by supporting donor count, then lexical label;
weighted Jaccard scores are ignored. Eligibility, hypotheses, provenance and
per-case budget remain fixed. No target labels are used by the reranker.

For capabilities, both rankings produce identical ordered top-1 and top-3
lists in every case: top-1 precision **6/9**, recall **6/10**; top-3 precision
**6/11**, recall **6/10**. The entire reported capability gain against unfiltered
frequency is therefore reproducible without weighted global graph ranking.
Overall the simpler control gains one role hit: `proj.watchdog/roles/000`
predicts `intent`, while weighted mapping predicts `constraint`. No new method
is promoted or selected by this review; preserve this control for subsequent
experiments rather than tuning the development ranking.

## Counterexamples and limits

1. **Independent application premises are conjoined.** `build_graph` unions
   `principle_evidence` over all decisions applying the same operator. With a
   donor applying `shared` once under `pA` and independently under `pB`, v2
   requires `{pA,pB}` on the target. A target carrying `{pA}` is rejected even
   though one complete donor application chain is present. The independent
   probe confirms this abstention. Separate application witnesses/alternative
   premise sets are needed before treating this as faithful local-chain
   transfer. It is a new representation counterexample; no measured score
   change on the existing corpus is claimed.

2. **Masked applications leave their oracle principle evidence visible.** A
   project with one `op.hidden` decision and one `pr.signature` retains that
   exact principle edge after the operator is hidden. No hidden id or raw text
   is passed, and this follows the stated v2 protocol. It is permissible for
   supplied-premise graph completion, but may give an answer-correlated oracle
   signature. LOPO donor isolation does not make it a realistic inference or
   extraction benchmark. A deployment benchmark must independently specify
   which premise observations are available before the hidden decision.

3. **Matching principles do not establish situation applicability.** A target
   free/reversible step and a donor expensive/irreversible step with shared
   `budget` principle still produce a `cost_gate` candidate. The implementation
   has no situation-predicate input or check. It correctly marks target
   applicability and truth unverified; this is an explicit limitation rather
   than promotion to fact. Broad premise overlap alone cannot justify choosing
   the operator. The README already notes analogous NoteFlow false positives.

4. **Temporal independence remains unavailable.** Global operator definitions
   may originate from the whole oracle, including the held-out project's
   annotated examples; only their use is donor-restricted. No temporal cut or
   source-know-time is supplied. Prediction-file-before-scoring is a software
   boundary, not predict-before-observe evidence. Original snapshots' old
   source-date/know-time semantics remain intact and labelled.

5. **Current protocol wording still contains an obsolete contract sentence.**
   The v1 protocol's epistemics section says `known_at` records the latest input
   source timestamp; its appended result/correction section and current code
   instead use candidate creation time. This is an audit/documentation ambiguity,
   not an observed current-output time leak. Clarify the current protocol while
   retaining immutable frozen original versions.

The strongest next measurement is an eligibility-matched ranking control plus
per-application premise preservation and separately observed situation predicates,
followed by a fresh independent corpus. Preserve the original failed v1 result,
the simpler baselines, and the capability/role/feature denominators separately.

## Follow-up ratchet before the application-alternative correction

Parent requested a separate corrected policy/version after preserving the
eligibility-matched ranking control. The independent union probe was converted
from a characterization test into the expected-correct behavior: a `pA` target
must retain the `shared` candidate justified by the complete `pA` application,
without making `pB` a required premise. These are the **first failure outputs**
before the implementation correction; no failed result was replaced by a pass.

Under the original union policy:

```text
test_distinct_application_premises_remain_alternative_witnesses ... FAIL
AssertionError: Lists differ: [] != ['shared']
Ran 1 test in 0.004s
FAILED (failures=1)
```

After binding the ratchet to the author's proposed new policy key
`capability_mapping = preserve_application_alternatives` (not yet implemented):

```text
test_distinct_application_premises_remain_alternative_witnesses ... FAIL
AssertionError: Items in the second set but not the first: 'pA'
Ran 1 test in 0.004s
FAILED (failures=1)
```

The new key was initially ignored by the old implementation, producing an
unfiltered candidate with an empty premise mapping; this failure protects the
new policy's actual witness contract. Existing v1/v2 immutable artifacts and
the initial 10/10 characterization review remain unchanged.

### Author's preserved ranking-control run

`results/synthetic_lopo_v3_ranking_control_first/` now records the independent
control as a fourth method without changing any existing eligibility or
ranking. Review confirms **8/8 artifact checks** (2.098 s), including all frozen
hashes, every control's donor witness/pool equality, exact ordered-label equality
with the independent reranker, and all **18 additional grouped/budget metric
dictionaries**. Every earlier full score dictionary is unchanged relative to
metadata-corrected v2. First new measurements confirm the exploratory reviewer
estimate exactly: top-1 14/55 precision and 14/46 recall; top-3 16/149 and 16/46.
The capability rankings still coincide. This run's protocol explicitly
acknowledges the estimate was known before its execution; it is a reproducibility
control, not a newly blind confirmation. The independent OR-witness ratchet
remains failing pending its separate policy correction.

### Corrected application alternatives — final independent verification

The author added a separate `preserve_application_alternatives` policy, keeping
the old union policy and all frozen outcomes. The initially failing independent
ratchet now passes: target `pA` selects only the first application's premise
mapping/source, both applications remain independently preserved, and a single
application requiring `pA AND pB` still abstains for target `pA`. The author also
rejects unknown mapping-policy values and clarified the current `known_at`
protocol wording; the original frozen protocol is retained. Initial findings
1 and 5 above are therefore addressed for the new optional policy/current docs.

The fifth retained run directory (including the metadata-only replay),
`synthetic_lopo_v4_application_alternatives_first`, was independently verified
against its hashes and the previous v3 run. **Every complete score dictionary
is identical**, including the matched-frequency and all 32 random controls.
From the original oracle decisions, the reviewer independently reconstructed
all **22 applicable application witnesses** in the saved capability candidates:
each conjunction is nonempty and satisfied by visible target principles; each
decision id, donor project, source locator and source path agrees with the
oracle, and historical source know-time remains unknown. No target-source
application enters donor provenance. This fixes representation of derivation
alternatives; it provides **no additional quality improvement on this corpus**.

Final executed checks:

```text
python -m unittest loom.tools.seeding.test_independent -v
Ran 13 tests in 3.356s
OK

python -m unittest discover -s loom/tools/seeding -p 'test_*.py'
Ran 36 tests in 3.431s
OK
```

Final review-test SHA-256:
`4f918867507c5b9e6a8b934dd7926ccb84f4970c915f319fb18187ae119613b3`.
All first failure outputs and first review conclusions above remain preserved.
Remaining limits are source-to-graph extraction, answer-correlated oracle premise
availability, situation applicability, temporal independence, novel-label
generation and independently measured semantic quality. Candidate-only
integration remains the supported decision; the simpler ranking alternative
remains preferable on these measured development rows.
