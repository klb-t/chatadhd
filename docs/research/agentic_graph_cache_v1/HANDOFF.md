# Verified packet cache handoff — 2026-09-30

This is a separate optional resource method under `loom/tools/structure/agentic_graph_cache_v1/`.
The previously committed strict codec/source-only agentic graph work is untouched.
Root owns integration, Git index/commit/push and any paid model execution. This
lane made zero network/paid/key operations and read no validation/old holdout.

## Completed and verified

`VerifiedPacketCache` exposes `validate_packet`, `validate_with_receipt`,
`stats`, `clear`, `reconfigure` over the existing native GraphPacket contract.
Whole-byte memoization is the simple control; verified immediate-parent reuse
checks a complete last-event backwards/forwards proof before using privately
authorized exact parent bytes. Entries retain immutable canonical bytes, not
caller objects, and bind strict/serializer source, exposed runtime helper/native
vocabulary snapshots, policy and resource limits. Runtime must be trusted and
unmodified at construction; the guard catches later drift, not hostile Python.
Memory/LRU/bypass/unlimited/zero-storage options are data. Public validation
diagnostics do not authorize admission or certify content truth. Ordinary native
application/inverse receipts remain byte-identical. Removed sources and model
origin/known_at remain exactly in the native journal.

First resource matrix: 198 applicable timed observations plus 27 explicit N/A,
three repetitions;66 separate memory probes plus 9 N/A;66 exact graph/application
receipt/inverse/path comparisons. Histories 0/5/20/50/100. Source and metric first
files retained, hypotheses frozen before outcomes. At 100, warm same-head strict
602.591ms vs whole 7.859 ms; extension strict 573.402 ms vs parent 21.332 ms. Expensive
priming and higher retained memory remain separate costs; one prepared isolated
extension loses after including its 581.407 ms preparation. Empty graphs/cold or
old-prefix misses generally favor strict.

Follow-up cold-through100 trajectory: 1,212 timed observations and 202 traced
finalist observations, all exact bytes/expected paths. Inclusive cold-start median
strict 25.302 s, whole128 26.029 s, parent128 1.343 s, parent1 1.366 s. Parent1 retains
130,844 charged bytes vsparent128 6,806,850; traced current 723,729 vs 7,397,325.
Two-case sibling fork confirms parent1 must fall back while parent128 reuses
head99; both remain exact. Keep one-entry only as a single-advancing-head preset.

Own 17 targeted tests passed before metrics; nonempty native journal follow-up
four tests/67 structured cases passed; one-entry sibling-fork regression passed.
Full standalone structure discovery completed 821/821 tests in 21.735 s, including
all 22 new cache tests. Expected negative argparse messages occur in its log.
Cache policy Draft2020-12 schema and four policy instances passed. Independent
watch 13/13 corrected binding cases and 6/6 own native journal cases passed.

Corrections preserved: independent cache revision17 found transitive runtime
helper rebinding; fixed cache guard and identical independent revision18 passed.
Native audit's first authored fixture had wrong Alias shape, failed before any
cache case, then corrected typed fixture passed; first source/log/zero-case result
retained. Frozen strict packet never changed.

## Source identity and artifacts

Strict packet SHA256:
`1b949dad8319f21713b984367cba162d1330f188d26557b21b264ef10910bbc4`.
Cache SHA256:
`bb6ac98489a85a807eb2879d877943c2f38843d852c8ffe1aaa4d0a67e225425`.
First matrix FREEZE SHA256:
`982f85d568142f1c54dc0ac2c92f464f8279f5cddba6c76f557adadacb9ab415`.
Chain FREEZE SHA256:
`166b6bd0a027cca66f475b583edaf1898616b6a01a836a3b1087bd6bc34a7e03`.

`FIRST_RESULTS.md`, `CHAIN_RESULTS.md`, `CACHE_INTERFACE.md`, both protocols,
JSON policy schema,128-entry and last-head presets explain the outcome.
`first_series/` and `chain_first/` retain every first measurement and fixture.
Watch artifacts are independent under `philosophy_watch_2026-09-30/` revisions
17/18/24. `SOURCE_PACKAGE_FREEZE.json` records the final package/owned artifact
hashes after this handoff. This subagent has not used the index or made a commit;
root will record the integration commit in the main session log.

## Reproduction

From repo root, use a fresh output directory rather than overwrite first files:

```bash
TMPDIR=/var/tmp python -m unittest discover -s loom/tools/structure -p 'test_*.py'
TMPDIR=/var/tmp python -m loom.tools.structure.agentic_graph_cache_v1.benchmark freeze --directory /var/tmp/cache-matrix-reproduction
TMPDIR=/var/tmp python -m loom.tools.structure.agentic_graph_cache_v1.benchmark timed --directory /var/tmp/cache-matrix-reproduction
TMPDIR=/var/tmp python -m loom.tools.structure.agentic_graph_cache_v1.benchmark memory --directory /var/tmp/cache-matrix-reproduction
TMPDIR=/var/tmp python -m loom.tools.structure.agentic_graph_cache_v1.benchmark summarize --directory /var/tmp/cache-matrix-reproduction
TMPDIR=/var/tmp python -m loom.tools.structure.agentic_graph_cache_v1.chain_benchmark freeze --directory /var/tmp/cache-chain-reproduction
TMPDIR=/var/tmp python -m loom.tools.structure.agentic_graph_cache_v1.chain_benchmark timed --directory /var/tmp/cache-chain-reproduction
TMPDIR=/var/tmp python -m loom.tools.structure.agentic_graph_cache_v1.chain_benchmark memory --directory /var/tmp/cache-chain-reproduction
TMPDIR=/var/tmp python -m loom.tools.structure.agentic_graph_cache_v1.retention_fork_probe freeze --directory /var/tmp/cache-chain-reproduction
TMPDIR=/var/tmp python -m loom.tools.structure.agentic_graph_cache_v1.retention_fork_probe run --directory /var/tmp/cache-chain-reproduction
```

Declare the project's jsonschema dependency/PYTHONPATH as needed for the broader
suite. Each fresh freeze captures its actual source paths/runtime; archived old
freezes retain original execution paths. Shared-host timing is not deterministic.

## Not verified and best next step

Native full CTest was not rerun by this lane after cache addition; root/native
validation owner should run it before integration. The original strict codec's
native 74/74 run preceded this resource method. No current-graph size scaling,
nonempty-history timing, measured semantic extraction quality, GPU behavior,
cross-ecosystem semantics or production cache integration is claimed. Strict/
whole cumulative traced memory was deliberately not repeated. No default changed.

Root can expose these as optional AnalysisPlan validator callbacks/policy data
without altering GraphPacket or adding a store. Next informative cache experiment
would vary current native graph size and nonempty event payload under the same
strict equality gates; preserve the simpler whole memo and no-cache baselines.
Any paid extraction work remains a separate root-authorized protocol/ledger.
