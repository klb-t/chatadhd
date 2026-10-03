#!/usr/bin/env python3
from datetime import datetime,timezone
import hashlib
import importlib.util
import json
import multiprocessing
from pathlib import Path
import sys

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2];sys.path.insert(0,str(ROOT))
SOURCE=ROOT/'loom/tools/structure/agentic_graph_v1/packet.py'


def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def main():
    snapshot=HERE/'packet_audited_source_08.py'
    with snapshot.open('xb') as f:f.write(SOURCE.read_bytes())
    spec=importlib.util.spec_from_file_location('loom.tools.structure.agentic_graph_v1.philosophy_cycle_snapshot_08',snapshot)
    p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
    freeze={'schema':'loom.philosophy_watch_cycle_freeze/1','frozen_at_utc':datetime.now(timezone.utc).isoformat(),
            'source_sha256':sha(snapshot),'probe_sha256':sha(__file__),'protocol_sha256':sha(HERE/'PROTOCOL_08_PACKET_CYCLES.md')}
    with (HERE/'PACKET_CYCLE_FREEZE_08.json').open('x') as f:json.dump(freeze,f,indent=2);f.write('\n')
    ctx=multiprocessing.get_context('fork');q=ctx.Queue()
    def worker():
        value={};value['self']=value;q.put({'state':'entered'})
        try:p.validate_json_resources(value)
        except Exception as exc:q.put({'state':'rejected','exception_type':type(exc).__name__,'exception':str(exc)})
        else:q.put({'state':'accepted'})
    child=ctx.Process(target=worker);child.start();entered=q.get(timeout=2);assert entered['state']=='entered'
    child.join(0.2)
    if child.is_alive():
        child.terminate();child.join(2);outcome={'state':'did_not_return_after_entered','terminated':True,'entered_deadline_seconds':0.2}
    else:outcome=q.get(timeout=2)
    shared={'value':'valid JSON shared twice'};p.validate_json_resources({'a':shared,'b':shared})
    report={'schema':'loom.philosophy_watch_cycle_results/1','finished_at_utc':datetime.now(timezone.utc).isoformat(),
            'source_sha256':sha(snapshot),'cyclic_python_json':outcome,'shared_acyclic_reference_accepted':True,
            'paid_requests':0,'scope':'JSON-validity counterexample, not a latency/model-quality benchmark'}
    with (HERE/'PACKET_CYCLE_FIRST_RESULTS_08.json').open('x') as f:json.dump(report,f,indent=2);f.write('\n')
    print(json.dumps(report))


if __name__=='__main__':main()
