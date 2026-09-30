"""The original scripted overlap counterexample; not a memory measurement."""
from pathlib import Path
from tempfile import TemporaryDirectory
from threading import Barrier, Event, Thread
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
from loom.tools.contracts import analysis_plan_ref as ref
from loom.tools.contracts.test_analysis_plan_ref import plan, result

p = plan()
p['variant_axes'] = {'worker': {'values': [0, 1]}}
p['methods'][0]['variant_axes'] = ['worker']
p['methods'][0]['reservation']['memory_bytes'] = '10'
p['resource_limits']['memory_bytes']['limit'] = '20'
entered = Barrier(3)
release = Event()
outputs, failures = [], []
with TemporaryDirectory(prefix='n5-original-overlap-') as directory:
    ledger = ref.ResourceLedger(directory, p['resource_limits'], budget_id='shared')

    def callback(*args):
        entered.wait(timeout=10)
        if not release.wait(timeout=10): raise RuntimeError('fixture release timed out')
        return result(measured='7')

    def worker(index):
        try:
            outputs.append(ref.execute_variant(p, index, ledger,
                {('future:analysis', 'local:scripted'): callback}, capabilities=['local']))
        except BaseException as error:
            failures.append(type(error).__name__)

    threads = [Thread(target=worker, args=(i,)) for i in (0, 1)]
    for thread in threads: thread.start()
    try:
        entered.wait(timeout=10)
        during = ledger.usage()['memory_bytes']
    finally:
        release.set()
        for thread in threads: thread.join(timeout=10)
    assert not failures and len(outputs) == 2 and not any(t.is_alive() for t in threads), failures
    report = {'held_concurrently': during, 'after_both_completed': ledger.usage()['memory_bytes'],
        'both_completed': all(r['results']['one']['state'] == 'completed' for r in outputs)}
    if hasattr(ledger, 'accounting'): report['resource_accounting'] = ledger.accounting()

report.update(source_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
    source_sha256=hashlib.sha256(Path(ref.__file__).read_bytes()).hexdigest(),
    fixture_evidence='scripted mechanism quantities, not instrumented memory or actual global peak',
    command='PYTHONUSERBASE=/workspace/scratch/a371a1ca13b1/verification/python-userbase python3 -B docs/coordination/receipts/N5-2026-10-01-resource-peaks/reproduce.py')
print(json.dumps(report, indent=2, sort_keys=True))
