# Conditional W1 semantic / latest W2 ledger proof

The native offline fixture passed **6/6** cases with **0 paid calls** against
actual W2 commit `bbc95f73672f3c9c4f2cd8153108da1513a80dc3`. The helper's
default pin was updated only after the real six-case success.
`semantic_w2_conditional.json` pins the W1 sources, exact public W2 Git blobs,
compiler/objects, root archive and link order; `_raw.json` retains complete
synthetic request/ledger receipts. W2 production source copies remain scratch
only; no cherry-pick, mock policy or substituted config function was used.

Five existing explicit-preset cases are preserved byte-for-byte: cold ledger
admission; configured input-byte baseline 1 blocking all HTTP pending approval;
correct receipt confirmation and one same-run dispatch/cache resume; reported
actual output 2 settling the baseline; and missing token/cost usage remaining
unknown with output cap as provenance. A sixth case leaves
`loom_usage_policy` absent: the actual latest W2 effective-options/settings
functions resolve the preset and canonical hash without opening a ledger, then
the required semantic guard dispatches once and learns actual 2. The config key
remains absent. This covers the new effective-options cold-config path. W2's
separate `Config::get` fallback implementation and C ABI are not merged/tested.

`loom/src/policy/usage_policy.cpp` is **byte-identical** between the historical
W2 `34cc920dd3cdb0c0fca0a514569b19111429583f` and latest commit, SHA-256
`790bca06403a46ee59894801e251e4fad6afe9e39d57d5f3b3a679f7486158b9`. Thus
null/baseline/reservation/admission/recovery behavior and the diagnostic
`overrun=true` for a known actual replacing a null estimate are unchanged.
That diagnostic is not evidence of an expected consumption increase.

This source-level conditional build is not a full merged W2 build, public ABI
test or CTest result. Exactly-once evidence covers the tested same-run
checkpoint/cache path, not concurrent dispatch locking. Scripted usage/cost
values are fixture inputs, not actual provider spending. W2 default values
remain legacy C++ pending its authoritative profile migration; this passing
behavior proof does not remove that W2 readiness blocker.

The former **5/5** source/proof remains publicly reproducible at W1 commit
`aed85b8d5b90db9c3055918e4864f8273bd3d411`, pinned to W2 `34cc920`, and is
also preserved in scratch `precision-checks/semantic-w2-five-public-aed85b8`.
Earlier source-race/link resource failures executed zero fixture cases and
remain separate historical infrastructure evidence; they are not latest-W2
quality results. Normal W1 production sources were unchanged by this replay.

From the matching W1 repository version and completed `loom/build/dev` core,
run each phase sequentially using a fresh scratch directory:

```sh
TASK_SEMANTIC_W2_DIR=$(mktemp -d)
python3 loom/src/extract/tests/semantic_w2_replay.py.fixture objects --scratch "$TASK_SEMANTIC_W2_DIR"
python3 loom/src/extract/tests/semantic_w2_replay.py.fixture link --scratch "$TASK_SEMANTIC_W2_DIR" --core-dir loom/build/dev
python3 loom/src/extract/tests/semantic_w2_replay.py.fixture run --scratch "$TASK_SEMANTIC_W2_DIR" --output "$TASK_SEMANTIC_W2_DIR/result.json"
```

The pinned public W2 commit must be available locally. `--w2-ref` selects an
explicit successor whose actual source hashes are recorded independently.
No provider transport, credential retrieval or paid call occurs.
