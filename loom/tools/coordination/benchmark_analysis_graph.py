"""Reproduce the small synthetic CLI mechanism timings; no inference/quality score."""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
import time
from .saved_workflow import decode_json
from ..structure.agentic_graph_v1 import packet as codec


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-commit', required=True)
    parser.add_argument('--samples', type=int, default=5)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.samples < 1: parser.error('samples must be positive')
    examples = Path(__file__).resolve().parent / 'examples'
    rows = []
    for index in range(args.samples):
        with tempfile.TemporaryDirectory(prefix='loom-w4-timing-') as temporary:
            path = Path(temporary)
            start = time.perf_counter()
            plan = decode_json((examples/'graph-review-plan.json').read_bytes())
            packet = decode_json((examples/'graph-review-packet.json').read_bytes())
            config = plan['methods'][0]['config']
            codec.apply_diff(packet, config['diff'], config['policy'])
            codec.preview_diff(packet, config['diff'])
            raw_seconds = time.perf_counter() - start
            command = [sys.executable, '-m', 'loom.tools.coordination', '--database', str(path/'coord.sqlite3'),
                'analysis-graph', '--task', 'benchmark', '--source-commit', args.source_commit,
                '--owner', 'benchmark', '--plan', str(examples/'graph-review-plan.json'),
                '--packet', str(examples/'graph-review-packet.json'), '--output-root', str(path/'artifacts'),
                '--ledger-directory', str(path/'ledger')]
            outputs, durations = [], []
            for _ in range(2):
                start = time.perf_counter()
                result = subprocess.run(command, check=True, capture_output=True, text=True)
                durations.append(time.perf_counter() - start); outputs.append(json.loads(result.stdout))
            first, repeated = outputs
            assert first['execution']['outcome']['all_methods_completed']
            assert not repeated['execution']['acquired']
            measurement = first['execution']['outcome']['analysis_result']['results']['one']['result']['measurements']
            rows.append({'sample': index, 'raw_algebra_seconds': raw_seconds,
                'cold_cli_seconds': durations[0], 'deduplicated_cli_seconds': durations[1],
                'cold_adapter_seconds': first['timing']['adapter_wall_seconds'],
                'callback_measurements': measurement})
    report = {'schema': 'loom.w4_mechanism_timing/1', 'source_commit': args.source_commit,
        'samples': rows, 'network_calls': 0, 'data': 'repository synthetic graph-review example',
        'medians': {key: statistics.median(row[key] for row in rows) for key in
            ('raw_algebra_seconds', 'cold_cli_seconds', 'deduplicated_cli_seconds', 'cold_adapter_seconds')},
        'limitation': 'Total operator path comparison, not isolated coordination overhead or model quality.'}
    with args.output.open('x') as stream: json.dump(report, stream, indent=2); stream.write('\n')


if __name__ == '__main__': main()
