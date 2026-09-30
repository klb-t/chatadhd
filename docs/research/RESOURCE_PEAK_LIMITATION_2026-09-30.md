# AnalysisPlan peak accounting: observed limitation

Date: 2026-09-30. Review baseline: `e466c6a93fcaf825024a9c8e6286044a6671b08f`.
Scope: the Python callback reference in
[`analysis_plan_ref.py`](../../loom/tools/contracts/analysis_plan_ref.py), not a
provider bill, native chat execution, or an actual memory measurement.

## Observed result

Two different variants execute concurrently against one `ResourceLedger`.
Each reserves `memory_bytes = 10`; each later returns the fixture quantity
`memory_bytes = 7`. While both callbacks are pending, `ledger.usage()` reports
`20`. After both complete, it reports `7`:

```json
{"held_concurrently": "20", "after_both_completed": "7", "both_completed": true}
```

This was reproduced locally using two Python threads and the existing test
fixtures. The quantities are scripted inputs. The fixture labels them
`instrument_measured` to exercise that accounting branch; no memory instrument
was used. In particular, this result does **not** establish that actual
simultaneous memory use was 14. Separate per-call maxima do not establish a
simultaneous global maximum.

## Why this happens and what it means

`ResourceLedger._usage()` recomputes a peak dimension as the larger of:

- the greatest **individual** completed quantity (and opening measured balance);
- the sum of currently held reservations plus a proposed new reservation.

It does not persist the previously observed sum of concurrent reservations or
the intervals needed to reconstruct overlap. Reopening the ledger cannot
recover a historical peak that was never recorded. Consequently,
[`ANALYSIS_PLAN.md`](../contracts/ANALYSIS_PLAN.md)'s phrase “retaining the
greatest accounted peak” overstates the implementation for concurrent attempts.

This reproduction does not demonstrate an admission bypass: both reservations
were checked while outstanding. Capacity reuse after completion is intentional.
The defect is the meaning of the historical peak report, not proof that current
admission ignored a reservation. Cumulative accounting is outside this result.

## Reproduction

From the repository root, using the declared contract-test Python dependencies
(`jsonschema`, `referencing`, and their installed dependencies):

```python
from tempfile import TemporaryDirectory
from threading import Barrier, Event, Thread

from loom.tools.contracts import analysis_plan_ref as ref
from loom.tools.contracts.test_analysis_plan_ref import plan, result

p = plan()
p["variant_axes"] = {"worker": {"values": [0, 1]}}
p["methods"][0]["variant_axes"] = ["worker"]
p["methods"][0]["reservation"]["memory_bytes"] = "10"
p["resource_limits"]["memory_bytes"]["limit"] = "20"
entered = Barrier(3)
release = Event()
outputs = []

with TemporaryDirectory() as directory:
    ledger = ref.ResourceLedger(directory, p["resource_limits"], budget_id="shared")

    def callback(*args):
        entered.wait(timeout=5)
        if not release.wait(timeout=5):
            raise RuntimeError("fixture synchronization timed out")
        return result(measured="7")  # scripted quantities, not instrumentation

    def worker(index):
        outputs.append(ref.execute_variant(
            p, index, ledger,
            {("future:analysis", "local:scripted"): callback},
            capabilities=["local"],
        ))

    threads = [Thread(target=worker, args=(i,)) for i in (0, 1)]
    for thread in threads:
        thread.start()
    entered.wait(timeout=5)
    during = ledger.usage()["memory_bytes"]
    release.set()
    for thread in threads:
        thread.join(timeout=5)
        assert not thread.is_alive()
    assert len(outputs) == 2
    print({
        "held_concurrently": during,
        "after_both_completed": ledger.usage()["memory_bytes"],
        "both_completed": all(
            output["results"]["one"]["state"] == "completed"
            for output in outputs
        ),
    })
```

## Proposed follow-up, not an implemented change

Keep separate names and records for current admission load, historical maximum
reserved load, and actual instrumented peak. A durable maximum-reservation
receipt can establish the second; it cannot establish the third. Preserve
capacity reuse and caller-configured limits. Add an overlapping-attempt replay
test when implementing those records. No new ceiling or spending permission
follows from this finding.
