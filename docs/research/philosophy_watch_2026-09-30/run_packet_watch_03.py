#!/usr/bin/env python3
"""Snapshot, freeze and execute independent packet tests preserving first failures."""
from datetime import datetime, timezone
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import unittest

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
SOURCE=ROOT/'loom/tools/structure/agentic_graph_v1/packet.py'


def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def main():
    snapshot=HERE/'packet_audited_source_03.py'
    with snapshot.open('xb') as f:f.write(SOURCE.read_bytes())
    files=[SOURCE,snapshot,HERE/'test_packet_watch_03.py',HERE/'PROTOCOL_03_PACKET.md',Path(__file__)]
    frozen={'schema':'loom.philosophy_watch_packet_freeze/1','frozen_at_utc':datetime.now(timezone.utc).isoformat(),
            'files_sha256':{str(p.relative_to(ROOT)):sha(p) for p in files},
            'paid_requests':0,'validation_reads':0,'owner_archive_reads':0,'canonical_writes':0}
    with (HERE/'PACKET_FREEZE_03.json').open('x',encoding='utf-8') as f:
        json.dump(frozen,f,indent=2);f.write('\n')
    spec=importlib.util.spec_from_file_location('philosophy_packet_tests_03',HERE/'test_packet_watch_03.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    suite=unittest.defaultTestLoader.loadTestsFromModule(module);stream=io.StringIO()
    result=unittest.TextTestRunner(stream=stream,verbosity=2).run(suite)
    with (HERE/'PACKET_FIRST_TEST_OUTPUT.log').open('x',encoding='utf-8') as f:f.write(stream.getvalue())
    report={'schema':'loom.philosophy_watch_packet_results/1','finished_at_utc':datetime.now(timezone.utc).isoformat(),
            'planned':suite.countTestCases(),'ran':result.testsRun,'failures':len(result.failures),'errors':len(result.errors),
            'successful':result.wasSuccessful(),'tested_snapshot_sha256':sha(snapshot),
            'production_source_still_same':sha(SOURCE)==sha(snapshot),
            'scope':'authored codec/diff mechanisms, not model semantic quality',
            'failed_cases':[str(t) for t,_ in result.failures+result.errors]}
    with (HERE/'PACKET_FIRST_RESULTS.json').open('x',encoding='utf-8') as f:
        json.dump(report,f,indent=2);f.write('\n')
    print(json.dumps(report));return int(not result.wasSuccessful())


if __name__=='__main__':raise SystemExit(main())
