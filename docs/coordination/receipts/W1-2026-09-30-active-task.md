# W1 — ActiveTaskSpec request compiler

## Identity and coordination

- `base_sha`: `b118c80e981c08ec6d7f9ab6aacc177979186cf2`.
- `head_sha` (implemented and verified source): `2bc3c4f9ec0c97e8161cfdb35e0919aebc1ee8d1`.
- Prior planning checkpoint: `dfa28a7d95e18c35a480081c53503f970e20ad6e`.
- Branch: `gpt/w1-active-task-2026-09-30`.
- Owner: this independent W1 conversation, explicitly started by the owner on
  2026-09-30 and continued after the helper-agent interruption.
- Remote integration HEAD was checked with `git ls-remote` and matched base.
  No W1 remote branch was found at start. Work uses a separate worktree/build.
- Scope: `loom/src/chat/`, additive `ChatOptions` fields in
  `loom/include/loom/chat_engine.h`, two new test files and these receipts.
  No exported C ABI, runtime, context engine, web, schema or frozen instrument
  changes. Shared STATE/index, integration branch, main and PR6 are unchanged.
- No direct channel to the other conversation's integrator was available.
  The scoped additive header change is included for its review; this is not
  a claim of cross-conversation file locking.

## Implemented increment

Explicitly supplied versioned ActiveTaskSpec now reaches the actual chat
request through existing `ChatOptions`, `loom_chat_ex` and the HTTP facade.
The deterministic renderer rebuilds the instruction from active/contested
statements, preserving conditions, exceptions, unresolved corrections and
executor-only qualifications. Arbitrary supplied compiled prose is not trusted.

Every source event binds to an existing active message in the current native
conversation by ID and exact UTF-8 text SHA-256. The covered refinement history
is replaced by default, or explicitly appended. The current follow-up is sent
once. Original source rows remain intact. Accepted supplied/compiled products,
version chain, bindings and native snapshots survive in user message metadata,
even after provider failure or trace opt-out. Validation precedes current-message
persistence; compilation trace and captured request agree.

Full options, version rules and evidence boundaries:
[W1 native request contract](W1-2026-09-30-contract.md).

## Verification at the exact source commit

| Measurement | Result | Scope |
|---|---|---|
| Native build | Pass | Debug, warnings as errors, vendored SQLite, shared ABI, CLI and HTTP server |
| Full CTest | **79/79 entries; 0 failed, 0 skipped; 121.43 s** | Entire configured suite; source hashes unchanged after run |
| Focused gate | **5/5 entries; 7.67 s** | Subset of full run; do not add the counts |
| New native W1 suite | **19/19 cases, 186 assertions** | Included in one CTest entry; synthetic developer-authored counterexamples |
| Actual C ABI / native HTTP | **1/1 unittest method; 3 loopback provider calls; 0 remote calls** | Seed, compiled request, explicit append; invalid binding causes neither HTTP nor a new message |
| Independent review | No remaining blocking issue | Source review only; no independent execution result claimed |

The loopback scenario is executed in both the focused and full runs: three
provider calls per run, six total across those two executions. Those are not
six distinct quality examples or external model calls. Native fake-transport
calls are separate in-process mechanism tests. No model inference was used.

The 424 doctest cases marked skipped in the W1 *source-file-filtered output*
are the other source files excluded by that CTest entry. They are scheduled
by their respective full-suite entries; the full CTest report has zero skipped
entries. Historical T1/T2/catalog measurements are not new W1 quality claims.

[Independent review](W1-2026-09-30-review.md).
[Machine-readable results](W1-2026-09-30-verification/results.json).
[Full CTest output](W1-2026-09-30-verification/full.log).
[Full details](W1-2026-09-30-verification/full-details.log).
Source/binary hashes, JUnit, build logs and checksummed artifact manifest are
in the same verification directory.

### Commands and environment

Configure from `/workspace/scratch/23947acf329f`:

```sh
env PYTHONPATH=/workspace/scratch/a371a1ca13b1/verification/deps \
  /workspace/scratch/a371a1ca13b1/verification/deps/cmake/data/bin/cmake \
  -S chatadhd-w1/loom -B verification/build -G Ninja \
  -DCMAKE_MAKE_PROGRAM=/workspace/scratch/a371a1ca13b1/verification/deps/bin/ninja \
  -DCMAKE_BUILD_TYPE=Debug -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_SHARED=ON \
  -DLOOM_WERROR=ON -DLOOM_BUILD_CLI=ON -DLOOM_BUILD_SERVER=ON

env PYTHONPATH=/workspace/scratch/a371a1ca13b1/verification/deps \
  /workspace/scratch/a371a1ca13b1/verification/deps/cmake/data/bin/cmake \
  --build verification/build -j4

env TMPDIR=/var/tmp \
  PYTHONPATH=/workspace/scratch/23947acf329f/chatadhd-w1:/workspace/scratch/a371a1ca13b1/verification/deps \
  PYTHONUSERBASE=/workspace/scratch/a371a1ca13b1/verification/python-userbase \
  PYTHONDONTWRITEBYTECODE=1 \
  /workspace/scratch/a371a1ca13b1/verification/deps/cmake/data/bin/ctest \
  --test-dir verification/build -j4 --output-on-failure \
  --output-junit /workspace/scratch/23947acf329f/verification/full.xml
```

Focused gate uses the same environment and
`-R 'unit.test_chat|compat.test_chat_active_task_http|compat.test_abi_compat'`.

### Preserved interruptions and corrections

Helper agents produced the compiler, integration draft and counterexamples;
they then reported a usage limit and stopped. The coordinator completed the
integration, and a later separate reviewer completed the source review.

An interrupted build left two zero-byte generated objects:
`src/semantic/analyzer.cpp.o` and `src/semantic/semantic_llm.cpp.o`. The resumed
link failed with `member ... is not an object`. Only those generated objects
were removed and rebuilt. Initial/resume/repaired logs are retained. The final
build and tests use stable sources; no passing run is claimed for the failed
intermediate link. No acceptance gate or timeout was weakened.

Review found a database/ChatEngine lock inversion; the final implementation
uses consistent Database -> ChatEngine locking and retains serialized initial
conversation creation. Retained revision metadata is revalidated and maximal
unsigned version arithmetic cannot wrap.

## Limits and next integration step

The native adapter consumes caller-authored clauses; it does not infer current
intent or verify semantic fidelity. It accepts existing native messages and
`native:active` scope, not arbitrary imported branches or current-turn source
placeholders. Locators and graph references are retained claims; text bindings
do not prove their external identities or provenance dates. Independent memory
and context channels can still mention rejected ideas. This increment does not
register the projection as a separate knowledge-graph Product or add web editing.

Integrate this branch after the publication block below is resolved. W2 can use
the supplied goal/statements as explicit retrieval inputs while preserving its
independent scope/detail controls. Do not duplicate ActiveTaskSpec or treat this
mechanism test as evidence of extraction/answer quality.

## Publication block

The attempted `git push -u origin gpt/w1-active-task-2026-09-30` was rejected by
automatic approval review. Its stated reason was that the destination was
unverified and W1 implementation authorization did not explicitly authorize
sharing these changes there. Subsequent read-only GitHub metadata confirmed
`klb-t/chatadhd` is the intended public repository and the connection has push
permission. Push was not retried after the rejection and no alternate write
route was used. No remote branch/update is claimed.

All implementation and evidence are committed locally. The owner is asked to
approve pushing this concrete W1 branch to the public repository; it does not
merge main, update the integration branch or change repository visibility.
