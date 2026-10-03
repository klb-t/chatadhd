# Analysis optimization: executable paired DEV experiment

Owner correction, 2026-10-02: prioritize improving schematic/structural analysis
through actual comparisons of question wording and cheap-model parameters.
Repository recovery and runtime performance are supporting work, not evidence
that model analysis improved.

**Status: 432 exact requests prepared, zero new model calls, no quality gain
claimed.** Access to this programme's OpenRouter credential is not configured
in the recovered runtime. Public endpoint/pricing GET requests succeeded;
authenticated account usage and remaining spend are still unknown.

## First study and contrasts

All arms use the same 48 author-independent W3 DEV queries: 12 synthetic PL/EN
conversations with four temporal/directional queries each. They are inspected
development material, not blind holdout or 48 independent conversations.
This first study targets source commitment, speaker, direction and withdrawal;
it does not by itself measure free graph extraction or all structural analysis.

| Contrast | Changed variable | Requests |
|---|---|---:|
| `j_active` → `j_directed` | Only the generic false-refutation criterion: ordered endpoints, speaker, persistent withdrawal and embedded negation | 48 + 48 |
| `j_directed` → `j_roles` | Original source retained exactly, plus a redundant data-only role card; questions unchanged | 48 additional |
| `j_directed` → `j_split` | Same two questions sent in separate calls, then recombined; no label changes | 96 additional |
| `g_brief_t0` ↔ `g_rules_t0` | Short semantic contract versus explicit source/direction/withdrawal rules | 48 + 48 |
| `g_brief_t03` ↔ `g_rules_t03` | Same prompt comparison at temperature 0.3 | 48 + 48 |
| Each GPT prompt, 0 ↔ 0.3 | Temperature only; input, schema, provider, output allowance and top-p fixed | Reuses the four GPT arms |

Jev: `typesafe/jev-1.13`, TypeSafe only, no fallback, $0.042/M input and $0
output cap. Cheap LLM: `openai/gpt-4.1-mini`, OpenAI endpoint only, no fallback,
supported parameters required, $0.4/M input and $1.6/M output caps, 192 output
tokens, `top_p=1`, strict JSON schema. These are experiment settings, not product
limits. Endpoint snapshots and actual retrieval times are in `preflight/`.

The TypeSafe API documents state, instructions and criteria as the relevant
decision inputs, and question IDs as routing keys not used in inference. It
does not expose a temperature field in that request schema; this study does
not invent one. Primary references checked 2026-10-02:
[API](https://docs.typesafe.ai/api),
[state](https://docs.typesafe.ai/concepts/state), and
[OpenRouter Decisions example](https://openrouter.ai/blog/tutorials/how-to-use-jev/).

## Frozen assessment

- Exact request, fixture, recipe, scoring/client code and source hashes are
  frozen in `prepared/freeze.json`; the builder never reads gold labels into
  requests. Gold bytes are hashed. The developer has inspected DEV examples.
- Primary accuracy uses all 48 planned queries per arm. Missing, invalid and
  conflicting answers are explicit failures, not silently dropped observations.
- Report per-class precision/recall and confusion, PL/EN and family slices,
  unknown→refuted errors, paired semantic corrections/regressions and separate
  availability changes. Threshold stays strictly greater than 0.5 for Jev.
- Keep every raw probability/response, input/output tokens, latency, actual
  cost and unknown-cost reservation. Compare cost per correct usable query;
  the two split calls together count as one query and both costs count.
- No significance, calibration or generalization claim from this dependent DEV
  sample. No universal winner, automatic routing or canonical graph promotion.
- Repeats for stability are a separately frozen next stage, not retries that
  replace an inconvenient first answer. A failed request is retained.

Ten offline boundary checks pass: exact inventory, controlled interventions,
source preservation, split-answer completeness, threshold/conflict handling,
isolated temperature and prompt changes, runner validation and missing ledgers.
An empty-run negative control reports 0/48 available for all eight scored arms
and `model_quality_measured: false`. These verify the experiment machinery,
not model quality.

## Execute after restoring access

1. Read `../../STATE.md` and the cost audit. The existing key cap is $2,
   non-resetting. Current usage must be checked using `GET /api/v1/key` before
   any new inference. Do not run GitHub Actions or use another project's key.
2. The full request allowance is **$0.5638576**. Retain a conservative
   **$0.00738793** for historical unknown costs: require at least **$0.57124553**
   current available allowance for the complete plan. This is not a prediction
   of actual billing. The old $1.23206716 metadata is not a current balance.
   Resolve outstanding/uncertain attempts before continuing; unexecuted older
   W3/role preparations must not be launched concurrently as duplicate work.
3. Configure only `OPENROUTER_API_KEY_FILE` pointing to the private recovered
   credential outside Git. Do not print it, commit it or include it in evidence.
4. Verify frozen files. Refresh endpoint evidence into a new preparation if
   older than 24 hours; preserve original request bodies and old snapshots.
5. Execute one arm at a time with the existing immutable-attempt clients. Stop
   the campaign on unknown billing, identity/price drift or a stopped ledger;
   do not continue another arm to bypass a stop. Check the full remaining
   reservation and historical uncertainty against the current key before each
   arm. Schedule counterparts adjacent and retain exact execution times/order.

From the repository root (the example runs only the first Jev arm):

```bash
python3 -m loom.tools.structure.analysis_optimization_v1 verify --prepared docs/research/analysis_optimization_2026-10-02/prepared
python3 -m loom.tools.structure.jev_live_pilot run --manifest docs/research/analysis_optimization_2026-10-02/prepared/j_active/manifest.json --run-dir /private/analysis-runs/j_active
```

The GPT arms use `python3 -m loom.tools.structure.openrouter_runner run
<manifest> --run-dir <arm-directory>`. That client provides `inspect-key` for
its own manifest; the operator must also include the cross-arm/unknown amounts
above. These commands are not an alternative to the preceding account gate.

After execution, join preserved output to gold with:

```bash
python3 -m loom.tools.structure.analysis_optimization_v1 score --prepared docs/research/analysis_optimization_2026-10-02/prepared --runs /private/analysis-runs --output /private/analysis-score.json
```

## Next studies after this measured result

The existing T3 fixture contains 36 PL/EN cases and 208 frozen request bodies
for expressed/inferred relations, instruction-object names, flat/hierarchical
Choice and structural granularity. Forty historical calls were targeted earlier;
that does not mean the entire grid was executed. Reconcile their IDs against
saved ledgers before selecting additional calls. Then separately measure
source-grounded schema extraction with and without schema hints, retaining
format success, source validity and structural correctness as separate metrics.
Do not use a better downstream selection score to conceal extraction errors.
