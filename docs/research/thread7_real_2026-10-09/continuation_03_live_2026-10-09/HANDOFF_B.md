# C live-boundary fixes for B and A

Code commits, pushed on C only:

- `a522ff1509c7b6dd733294a2c7b66c56b20c072b`: A4-C-001/002 and adjacent
  A2-C-003 reproduced at `cb16b336`, then fixed.
- `37d40d3d0759552a135ec6576a9065ba0e39bab2`: A2-C-001/004 reproduced at
  `cb16b336`, then fixed; approved-byte consumption closes a later peer-reproduced
  file reread race.

The pinned A findings are from `9f931da3bb1d1001f9b7d865914ae195d9255b7e`.
No runtime, UI, shared contract, STATE/INDEX or B branch changed.
`FINAL_TESTS.json` binds **1597/1597 structure PASS, zero skips**, 46.86 seconds,
to `37d40d3d`. Full stdout/JUnit and the repository coverage verifier are in
`gate-evidence.zip`. This is the structure gate, not the whole B/native gate.

New connector output identifies producer `loom.thread7_prepared_connector/2`.
Historical output is unchanged. Reusing an old queue identity for a v2 spec must
still fail. Actual live real-source binding requires `source_content_verified`
and retains distinct native-content/context/normalized-file hash domains.
Legacy test input with no actual source bytes can be declared-hash compatible
but remains explicitly unverified.

Payer synchronization now rejects mismatched request bytes/body, requested
model/provider/route and campaign before appending any result in that batch.
The existing ledger is still the sole money authority. It does not authenticate
the historical endpoint URL. Tests corrected stale fixture metadata only;
their prior assertions were not weakened.

Public exporters require a separately versioned reviewed-input descriptor.
Do not mint approval automatically from arbitrary supplied input. New safe
result snapshots need a reviewed descriptor revision. Strict rejection is
retained while public exceptions contain no rejected private value. The generic
lossless graph codec and `loom.method_graph/1` / `loom.method_run_trace/1`
contracts are unchanged.

Exact targeted recheck from the repo root (capture both streams fully):

```sh
TMPDIR=/var/tmp PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=/tmp/thread7-python-deps:. python3 -m unittest -v loom.tools.structure.test_prepared_connector_binding_v2 loom.tools.structure.test_experiment_payer_binding_v2 loom.tools.structure.test_experiment_public_projection_v1 > thread7-live-targeted.log 2>&1
```

The complete gate's exact registration, dependency paths, compiler/source
hashes and pre-run checkpoint are preserved in `gate-evidence.zip`. The previous
native executable was absent in this session, so the original small validator
was rebuilt from the tested checkout; no native source changed.

New API spend and paid trials: zero. Credential handoff was delivered first;
private receiver remains outside Git. Current key usage, reservations and
available authorized balance are still unknown. This handoff does not mark a
new phase dispatch-ready and contains no private source/request/response bytes.
