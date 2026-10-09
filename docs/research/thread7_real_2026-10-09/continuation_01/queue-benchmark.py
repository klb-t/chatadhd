"""Finite scheduling sample of a huge synthetic mechanical matrix; no dispatch."""
from pathlib import Path
import hashlib
import itertools
import json
import math
import platform
import tempfile
import time
import tracemalloc

from loom.tools.structure import experiment_workflow_v1 as w
from loom.tools.structure.test_experiment_workflow_v1 import spec


def benchmark(sample=1000):
    s=spec()
    s['variants']={'mode':'matrix','axes':[{'name':'model','values':['mechanical/a','mechanical/b']},
        *[{'name':'axis_'+str(i),'values':list(range(10))} for i in range(20)]]}
    total=math.prod(len(x['values']) for x in s['variants']['axes'])*s['repetitions']*len(s['scope'])
    result={'schema':'loom.research_queue_benchmark/1','kind':'synthetic_mechanism_only',
            'python':platform.python_version(),'combinations_including_repetitions':str(total),
            'sample_jobs':sample,'live_provider_calls':0,'owner_campaign_new_cost_usd':'0','strategies':[]}
    for mode in ('blocked','controlled_random','cache_aware'):
        s['id']='benchmark-'+mode;s['ordering']={'mode':mode,'seed':1709,'window_size':64}
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'queue.sqlite'
            tracemalloc.start();started=time.perf_counter()
            q=w.Queue(path);count=q.add(itertools.islice(w.ordered_jobs(s),sample))
            elapsed=time.perf_counter()-started
            _,peak=tracemalloc.get_traced_memory();tracemalloc.stop()
            semantic=w.digest(q.snapshot());q.db.close()
            reopened=w.Queue(path);same=w.digest(reopened.snapshot())==semantic
            duplicate_add=reopened.add(itertools.islice(w.ordered_jobs(s),sample));reopened.db.close()
            result['strategies'].append({'mode':mode,'seed':1709,'window_size':64 if mode=='cache_aware' else None,
                'prepared':count,'elapsed_seconds':elapsed,'python_traced_peak_bytes':peak,
                'sqlite_bytes':path.stat().st_size,'restart_content_equal':same,'duplicate_insertions':duplicate_add,
                'sample_snapshot_sha256':semantic})
    result['limits']=['tracemalloc measures Python allocations, not total RSS or SQLite native memory',
        'finite sample only; does not certify disk capacity for the full matrix',
        'timings depend on host load; no model quality or provider admission measured',
        'cache-aware is bounded window sorting; prefix_grouped remains legacy global materialization']
    return result


if __name__=='__main__':
    print(json.dumps(benchmark(),indent=2))
