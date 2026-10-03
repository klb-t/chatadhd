"""Independent local-only probes; writes each first outcome exclusively."""
from pathlib import Path
from copy import deepcopy
from itertools import product
from unittest.mock import patch
import hashlib, json, threading, traceback

from loom.tools.contracts import analysis_plan_ref as ref
from loom.tools.contracts.test_analysis_plan_ref import plan, result

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

def save(name, value):
    with (HERE / name).open('x') as f:
        json.dump(value, f, indent=2, sort_keys=True); f.write('\n')

def execute(p, ledger, callback):
    return ref.execute_variant(p, 0, ledger,
        {('future:analysis', 'local:scripted'): callback}, capabilities=('local',))

sources = ['loom/tools/contracts/analysis_plan_ref.py',
    'loom/tools/contracts/test_analysis_plan_ref.py', 'docs/contracts/analysis_plan.schema.json']
save('SOURCE_BEFORE.json', {s: hashlib.sha256((ROOT / s).read_bytes()).hexdigest() for s in sources})

# This deliberate corruption touches only an exclusively owned synthetic ledger.
p = plan(); p['resource_limits']['money_usd']['limit'] = '1'
ledger = ref.ResourceLedger(HERE / 'corrupt_completion_ledger', p['resource_limits'], budget_id='shared')
execute(p, ledger, lambda *_: result())
completion = next(ledger.directory.glob('attempt-*/completion.json'))
original = json.loads(completion.read_bytes()); altered = deepcopy(original)
altered['measurements']['money_usd'] = '0'
save('ORIGINAL_SYNTHETIC_COMPLETION.json', original)
completion.write_text(json.dumps(altered), encoding='utf-8')
counterexample = {'expected': 'reject inconsistent completion/result measurements', 'rejected': False}
try:
    counterexample['usage_after_completion_only_mutation'] = ledger.usage()
    another = deepcopy(p); another['id'] = 'independent-after-corruption'; calls = []
    response = execute(another, ledger, lambda *_: (calls.append(True) or result()))
    counterexample['extra_callback_admitted'] = bool(calls)
    counterexample['second_state'] = response['results']['one']['state']
except ref.PlanError as e:
    counterexample.update(rejected=True, rejection=e.code)
save('FIRST_COMPLETION_MUTATION.json', counterexample)

p = plan(); ledger = ref.ResourceLedger(HERE / 'invalid_measurement_ledger', p['resource_limits'], budget_id='shared')
response = result({'first_candidate': 'preserve this exact returned output'})
response['measurements']['money_usd'] = '-1'
save('RETURNED_FIRST_RESPONSE.json', response)
r = execute(p, ledger, lambda *_: response)
save('FIRST_INVALID_MEASUREMENT.json', {
    'expected': 'uncertain + retained reservation + preserved returned first response',
    'state': r['results']['one']['state'], 'usage': ledger.usage(),
    'attempt_files': sorted(f.name for f in next(ledger.directory.glob('attempt-*')).iterdir()),
    'returned_output_saved_by_executor': any('preserve this exact returned output' in f.read_text()
        for f in next(ledger.directory.glob('attempt-*')).glob('*.json'))})

p = plan(); ledger = ref.ResourceLedger(HERE / 'interrupted_completion_ledger', p['resource_limits'], budget_id='shared')
calls = []; original_write = ref._write_new
def fail_completion(path, value):
    if Path(path).name == 'completion.json': raise OSError('scripted completion storage failure')
    return original_write(path, value)
with patch.object(ref, '_write_new', fail_completion):
    first = execute(p, ledger, lambda *_: (calls.append(True) or result()))
second = execute(p, ledger, lambda *_: (calls.append(True) or result()))
save('FIRST_COMPLETION_WRITE_FAILURE.json', {
    'callback_count': len(calls), 'first_state': first['results']['one']['state'],
    'second_state': second['results']['one']['state'], 'usage': ledger.usage(),
    'result_saved': bool(list(ledger.directory.glob('attempt-*/result.json')))})

p = plan(); p['resource_limits']['money_usd']['limit'] = '2'
instances = [ref.ResourceLedger(HERE / 'concurrent_ledger', p['resource_limits'], budget_id='shared') for _ in range(4)]
barrier = threading.Barrier(4); release = threading.Event(); entered = []; outputs = []; errors = []; lock = threading.Lock()
def worker(index):
    q = deepcopy(p); q['id'] = 'concurrent-' + str(index)
    def callback(*_):
        with lock:
            entered.append(index)
            if len(entered) == 2: release.set()
        if not release.wait(5): raise RuntimeError('concurrency probe timed out')
        return result()
    try:
        barrier.wait(timeout=5); response = execute(q, instances[index], callback)
        with lock: outputs.append(response['results']['one']['state'])
    except Exception as exc:
        with lock: errors.append(type(exc).__name__)
threads = [threading.Thread(target=worker, args=(i,)) for i in range(4)]
for thread in threads: thread.start()
for thread in threads: thread.join(10)
save('FIRST_CONCURRENT_DISTINCT.json', {'entered_callback_count': len(entered), 'states': sorted(outputs),
    'errors': errors, 'workers_finished': all(not t.is_alive() for t in threads), 'usage': instances[0].usage()})

p = plan(); p['variant_axes'] = {'a': {'values': ['x', 'y']},
    'b': {'range': {'start': 2, 'stop': 7, 'step': 2}}, 'c': {'values': [False, True]}}
expected = [dict(zip(('a','b','c'), values)) for values in product(('x','y'), (2,4,6), (False,True))]
actual = [ref.variant_at(p, i) for i in range(ref.cardinality(p))]
ledger = ref.ResourceLedger(HERE / 'irrelevant_axis_ledger', p['resource_limits'], budget_id='shared'); calls = []
for i in range(ref.cardinality(p)):
    ref.execute_variant(p, i, ledger, {('future:analysis','local:scripted'): lambda *_:(calls.append(True) or result())}, capabilities=('local',))
save('FIRST_VARIANT_PRODUCT.json', {'cardinality': ref.cardinality(p), 'independent_product_matches': actual == expected,
    'unused_axis_callback_count': len(calls), 'attempt_count': len(list(ledger.directory.glob('attempt-*')))})
print('First independent probes preserved')
