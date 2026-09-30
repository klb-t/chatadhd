# W4 receipt — local AnalysisPlan graph execution

- base_sha: `b118c80e981c08ec6d7f9ab6aacc177979186cf2`
- head_sha (published implementation): `b5bad4c35a0831d490f942f53f672fa105aea250`
- local verified implementation: `7714b0f7dce8f1a609ec1d0091293708b98c00c4`
- identical implementation tree: `e7bfe517d0d6a11b397a066b4caa52245d1e728e`
- branch: `gpt/w4-graph-runtime-2026-09-30`
- Independent review and final tests completed before 2026-09-30T23:42:08+02:00.
  Start/review durations were not separately measured; no invented throughput claim.
- Integration: pending ROOT. No shared STATE, native schema, ABI, main or PR6 edits.

## Result

A new operator command `python3 -m loom.tools.coordination ... analysis-graph`
consumes an actual AnalysisPlan and pinned GraphPacket. It executes the existing
local diff preview/application algebra through the existing variant executor,
resource ledger and fenced SQLite coordination. Multiple dependent graph steps
use their predecessor's selected packet. No new scheduler or canonical graph.
See `loom/tools/coordination/ANALYSIS_GRAPH.md` and its runnable synthetic example.

Scope: new `analysis_graph.py`, additive command in `__main__.py`, two test files,
benchmark, example packet/plan and documentation under `loom/tools/coordination`.
Evidence lives in this receipt's adjacent directory. No code owned by W1/W2/W3/W5
was changed. The package's explicit coordination ownership was sufficient;
no shared native adapter allocation or schema change was required.

Immutable task inputs bind plan, packet, variant, acceptance, ledger/output
locations, adapter version, source revision and CLI input byte hashes. Method
attempts also bind code/packet identity without resetting resource budgets.
Before each graph callback, the current lease is renewed with fencing. Original
inputs, first outputs, uncertain reservations and late observations survive
process restart. Unknown outcome is never automatically retried, including when
only the outer lease is explicitly reconciled. Source bytes and epistemic
provenance remain in packet history; acceptance does not establish truth.

## Validation

Environment: Python 3.12, existing verification dependencies at
`/workspace/scratch/a371a1ca13b1/verification/deps`; exact versions and per-file
hashes in `W4-2026-09-30-graph-runtime/source-hashes.json`.
All commands ran from the W4 checkout with that directory in `PYTHONPATH`.

| Command | Result |
|---|---|
| `python3 -m unittest discover -s loom/tools/coordination -p 'test_*.py' -v` | 50/50, 29.103 s |
| `python3 -m unittest discover -s loom/tools/contracts -p 'test_*.py'` | 185/185, 34.749 s |
| `python3 -m unittest loom.tools.structure.agentic_graph_v1.test_packet loom.tools.structure.agentic_graph_v1.test_workflow -v` | 27/27, 1.059 s |
| `git diff --check` | pass |

Counts are separate unittest methods, not new CTest entries or model-quality
measurements. The first coordination run was 41/41 before the reviewer's nine
cases; it is historical, not additional evidence to add to 50. Independent agent
wrote and ran nine adversarial tests, including two real spawned workers and
workers killed by `os._exit` around inner reservation/outer completion. It found
no unresolved fencing/dispatch defect. Its ancestor-fsync caveat was fixed in
the implementation before the final 50-test run. No machine power-loss experiment.

Preserved execution limitations/failures:
- Initial default-Python invocation failed import because jsonschema was absent;
  using the existing test dependency directory resolved the environment issue.
- A broad structure discovery invocation returned exit 0 with only 13 progress
  dots and no unittest summary. Its incomplete log is retained. **No full-structure
  pass is claimed.** The 27 directly affected graph tests were then run explicitly.
- First benchmark invocation used a short SHA and was correctly rejected by the
  existing full-source-commit requirement before graph execution. The corrected
  five-sample experiment uses the full verified SHA, with no configuration tuning.
- No native rebuild/ABI change; no real exports or sealed validation accessed.

## Full-path timings

Protocol was committed before measurement in `.../PROTOCOL.md`. Five fresh
synthetic one-patch samples, then completed-task invocation per sample. Full
command: `python3 -m loom.tools.coordination.benchmark_analysis_graph
--source-commit 7714b0f7dce8f1a609ec1d0091293708b98c00c4 --samples 5
--output /workspace/scratch/56922fafc890/w4-timings.json`.

| Median | Seconds |
|---|---:|
| Raw decode + graph apply/preview | 0.017463 |
| Complete cold CLI (including interpreter, reads, DB, validation, artifacts, serialization) | 0.812404 |
| Completed-task CLI (no fresh graph callback) | 0.713238 |
| Cold adapter interior | 0.582813 |

The local graph callback itself took roughly 0.010–0.016 seconds. Total path
cost dominates this tiny packet; repeated invocation is not consistently faster
(each raw sample is retained). This is not isolated SQLite overhead, a general
cache benchmark, or real-archive throughput. CPU/wall callback measurements are
scoped; uninstrumented resource dimensions retain configured reservation policy.
Paid calls: 0. Network calls by the tested execution path: 0.

## Publication and recovery

Initial git push was blocked by automatic approval review as unverified egress.
The user-supplied README explicitly names klb-t/chatadhd and commit/push workflow;
the connected GitHub repository metadata confirmed that exact destination and
push permission. Rechecked push passed review but failed due to missing terminal
credentials. The connected GitHub API then published the exact tested tree on the
scoped branch. API author/committer metadata produces the different commit SHA
above; tree hash equality was verified. No force push or integration-line write.

Local verification history is retained; the scoped branch is recoverable from
GitHub. The subsequent receipt-only commit contains these logs and timing samples.
Its SHA is reported in the conversation; `head_sha` above identifies the tested
implementation rather than attempting a self-referential receipt commit hash.

## Limits and next step

This is a usable **local operator** path and packet projection, not native graph
application or model quality. The generic executor's outer success/acceptance
flags are qualified by per-method completion and application receipts. CLI users
must inspect `all_methods_completed`, unavailable/uncertain states and accepted
packet output. Explicit outer reconciliation cannot reset inner unknown attempts.

ROOT can cherry-pick the implementation and receipt commits after checking current
integration HEAD. Next native consumer should reuse this seam only after ROOT
assigns the store/API boundary. Separately profile schema construction and durable
I/O if repeated small local transformations become an interactive workload;
these five samples do not justify a new cache or weakened validation.
