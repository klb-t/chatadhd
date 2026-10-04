# Integrator 9 — W4 replay accounting blocker, 2026-10-04

W4 `3caa6b4dcfb6412294bf816c1105bcf99afacdbc` is held for its author.
This archive preserves that exact executable source plus the independent
negative integration evidence. Main and the author's active branch are unchanged.

Combined sources: W2 `34cc920dd3cdb0c0fca0a514569b19111429583f` policy and W4
packet dispatcher/algebra. A freshly compiled static overlay links the existing,
hash-pinned W4 core; it is not a claimed full build of the combined branches.
The default policy setting is explicitly installed because that core predates W2.

## Observed result

The same `capabilities` request is sent twice, with operation ID
`synthetic-unresolved-replay`, cohort `synthetic-packet-capabilities`, and
`resources: {"calls": 1, "gpu_seconds": null}`.

| Measure | First request | Identical replay |
|---|---:|---:|
| `executed` | true | true |
| settlement status | unresolved | unresolved |
| recorded actual calls | 1 | 1 |
| measured calls samples | 1 | 1 |

`UsagePolicy::request` returns the durable unresolved reservation with
`authorized=true`. `capi_packet.cpp` uses that flag to execute again.
`complete` correctly refuses to train a duplicate sample for the same operation;
the dispatcher has therefore performed two executions while retaining one call.
The case is synthetic/local and performs zero provider calls.

## Return to the W4 author

Distinguish permission for a first execution from an unresolved reservation.
A settled-but-unresolved ID must not dispatch the packet again; later accounting
reconciliation is a separate operation. Add a regression for the request above.
Concurrent duplicate IDs also need an explicit execution-claim contract with W2;
the shared policy deliberately does not promise exactly-once external dispatch.
Do not weaken the policy or hide unknown resource amounts to suppress this case.

## Reproduce

The [portable runner](integrator-packet-replay-2026-10-04/run.py) extracts the
exact W2/W4 sources from local Git, freshly compiles the three overlay objects,
and links the actual W4 static build. Run this archived checkout's ordinary
server/CLI/shared vendored-SQLite development build first, then:

```sh
python3 docs/reports/integrator-packet-replay-2026-10-04/run.py \
  --repository . --packet-checkout . --packet-build loom/build/dev \
  --output /tmp/loom-packet-replay-evidence
```

The runner exits successfully only when the negative case is reproduced; it is
not an acceptance test. The original [runner](integrator-packet-replay-2026-10-04/run-recorded.py),
[harness](integrator-packet-replay-2026-10-04/harness.cc),
[commands/source/binary hashes](integrator-packet-replay-2026-10-04/manifest.json),
[raw results](integrator-packet-replay-2026-10-04/result.json) and
[complete log](integrator-packet-replay-2026-10-04/build-and-run.log) retain the
first execution. The recorded runner has its original scratch paths; the portable
runner adds caller-provided paths and does not change the replay case.
