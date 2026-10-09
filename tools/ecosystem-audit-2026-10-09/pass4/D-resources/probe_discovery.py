#!/usr/bin/env python3
"""Record real availability of D domain discovery without a substitute executor."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser()
p.add_argument('--repo', type=Path, required=True)
p.add_argument('--sha', required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
repo = a.repo.resolve()
if subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip() != a.sha:
    raise SystemExit('checkout SHA mismatch')
sys.path.insert(0, str(repo))
from loom.tools.resource_graph.core import ResourceGraph
fixture = Path(__file__).resolve().parent / 'fixtures/conversation.json'
before = fixture.read_bytes()
graph = ResourceGraph()
graph.attach(fixture, logical_id='probe')
try:
    graph.discover('probe')
    evidence = {'status': 'implemented_requires_further_acceptance'}
except ModuleNotFoundError as error:
    evidence = {'status': 'BLOCKED_MISSING_IMPLEMENTATION', 'exception_type': type(error).__name__, 'missing_module': error.name}
except Exception as error:
    evidence = {'status': 'ERROR', 'exception_type': type(error).__name__}
evidence['source_unchanged'] = fixture.read_bytes() == before
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(json.dumps({'sha': a.sha, 'id': 'D4-B05', 'category': 'integration', 'evidence': evidence}, indent=2) + '\n')
raise SystemExit(1 if evidence['status'] != 'implemented_requires_further_acceptance' else 0)
