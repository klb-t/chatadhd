#!/usr/bin/env python3
"""Export an actual D projection for independently tested E consumption."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser()
p.add_argument('--repo', type=Path, required=True)
p.add_argument('--sha', required=True)
p.add_argument('--output-dir', type=Path, required=True)
a = p.parse_args()
repo = a.repo.resolve()
if subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip() != a.sha:
    raise SystemExit('checkout SHA mismatch')
sys.path.insert(0, str(repo))
from loom.tools.resource_graph.core import ResourceGraph
source = Path(__file__).resolve().parent / 'fixtures/conversation.json'
graph = ResourceGraph()
graph.attach(source, logical_id='audit:resource:conversation')
packet = graph.project('audit:resource:conversation', depth=5)
a.output_dir.mkdir(parents=True, exist_ok=True)
for name, data in [('D-E-packet.json', packet), ('D-E-source.json', graph.describe('audit:resource:conversation'))]:
    (a.output_dir / name).write_text(json.dumps(data, indent=2) + '\n')
(a.output_dir / 'D-E-generator.json').write_text(json.dumps({
    'repo_sha': a.sha, 'fixture_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    'packet_id': packet['packet_id'], 'source': 'synthetic fixture, no user content',
    'actual_consumer': 'ResourceGraph.attach → project(depth=5)',
    'normalization': 'none; local source stat metadata varies after relocation',
    'counts': {k: len(packet[k]) for k in ('entities', 'claims', 'sources')}
}, indent=2) + '\n')
