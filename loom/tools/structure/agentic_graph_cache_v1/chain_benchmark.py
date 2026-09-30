"""Frozen trajectory/retention follow-up, no network or model-quality claim."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import time
import tracemalloc

from . import cache as module
from .test_cache import histories

codec = module.codec
DOC = Path(__file__).resolve().parents[4] / 'docs/research/agentic_graph_cache_v1'
ARMS = ('strict', 'whole128', 'parent128', 'parent1')
CHECKPOINTS = (0, 5, 20, 50, 100)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def save(path, value):
    path.write_bytes(codec.safe.canonical(value) + b'\n')


def read(path):
    return codec.safe.parse_json(path.read_bytes())


def freeze(directory):
    directory.mkdir(parents=True, exist_ok=False)
    heads = histories(100)
    codec.validate_packet(heads[-1])  # strict replay proves every reconstructed prefix
    save(directory / 'fixtures.json', heads)
    paths = [Path(__file__), Path(module.__file__), Path(codec.__file__), Path(codec.safe.__file__),
             Path(__file__).with_name('test_cache.py'), DOC / 'CHAIN_PROTOCOL.md', DOC / 'preset.json',
             DOC / 'first_series/SUMMARY_FIRST.json', DOC / 'mechanism_tests_after_native_audit.txt']
    config = {'schema': 'loom.graph_packet_cache_chain_benchmark/1', 'arms': list(ARMS),
              'head_count': len(heads), 'repetitions': 3, 'timed_head_count': 1212,
              'memory_arms': ['parent128', 'parent1'], 'memory_repetitions': 1, 'memory_head_count': 202,
              'checkpoints': list(CHECKPOINTS), 'policy': read(DOC / 'preset.json'),
              'fixture_sha256': sha((directory / 'fixtures.json').read_bytes()),
              'fixture_records': [{'packet_id': packet['packet_id'], 'canonical_sha256': sha(codec.safe.canonical(packet)),
                                   'canonical_bytes': len(codec.safe.canonical(packet))} for packet in heads],
              'source_records': [{'path': str(path.resolve()), 'sha256': sha(path.read_bytes())} for path in paths],
              'preflight': 'strict final-head backwards/forwards replay of all100events succeeded',
              'runtime': {'python': platform.python_version(), 'platform': platform.platform()},
              'arm_order': 'rotation by repetition modulo4', 'fixture_allocation_traced': False,
              'constructor_in_total_time': True, 'result_checks_timed': False, 'semantic_quality_measured': False,
              'paid_calls': 0, 'network_calls': 0, 'strict_whole_cumulative_traced_memory_measured': False}
    save(directory / 'FREEZE.json', config)


def load_frozen(directory):
    config = read(directory / 'FREEZE.json')
    if sha((directory / 'fixtures.json').read_bytes()) != config['fixture_sha256']:
        raise RuntimeError('chain_fixtures_changed')
    for record in config['source_records']:
        if sha(Path(record['path']).read_bytes()) != record['sha256']:
            raise RuntimeError('chain_source_changed:' + record['path'])
    return config, read(directory / 'fixtures.json')


def make_cache(arm, config):
    if arm == 'strict': return None
    policy = dict(config['policy'], mode='whole_packet' if arm == 'whole128' else 'verified_history')
    if arm == 'parent1': policy['max_entries'] = 1
    return module.VerifiedPacketCache(policy)


def validate(cache, head):
    if cache is None:
        return codec.validate_packet(head), {'path': 'strict_baseline', 'stats': None}
    return cache.validate_with_receipt(head)


def check_result(arm, index, result, diagnostic, config):
    raw = codec.safe.canonical(result)
    if sha(raw) != config['fixture_records'][index]['canonical_sha256']:
        raise RuntimeError('chain_result_bytes_differ')
    expected = 'strict_baseline' if arm == 'strict' else 'verified_immediate_parent_hit' if index and arm.startswith('parent') else 'strict_fallback'
    if diagnostic['path'] != expected or (arm == 'parent1' and diagnostic['stats']['entries'] > 1):
        raise RuntimeError('chain_cache_path_or_retention_drift')


def timed(directory):
    config, heads = load_frozen(directory)
    out = directory / 'timed_first.jsonl'
    totals = []
    with out.open('xb') as sink:
        for repetition in range(3):
            order = ARMS[repetition:] + ARMS[:repetition]
            for arm in order:
                wall, cpu = time.perf_counter_ns(), time.process_time_ns()
                cache = make_cache(arm, config)
                constructor_wall, constructor_cpu = time.perf_counter_ns() - wall, time.process_time_ns() - cpu
                cumulative_wall = cumulative_cpu = 0
                for index, head in enumerate(heads):
                    wall, cpu = time.perf_counter_ns(), time.process_time_ns()
                    result, diagnostic = validate(cache, head)
                    elapsed_wall, elapsed_cpu = time.perf_counter_ns() - wall, time.process_time_ns() - cpu
                    cumulative_wall += elapsed_wall; cumulative_cpu += elapsed_cpu
                    check_result(arm, index, result, diagnostic, config)
                    row = {'arm': arm, 'repetition': repetition, 'events': index, 'wall_ns': elapsed_wall,
                           'cpu_ns': elapsed_cpu, 'cumulative_wall_ns': cumulative_wall, 'cumulative_cpu_ns': cumulative_cpu,
                           'constructor_wall_ns': constructor_wall, 'constructor_cpu_ns': constructor_cpu,
                           'total_with_constructor_wall_ns': cumulative_wall + constructor_wall,
                           'total_with_constructor_cpu_ns': cumulative_cpu + constructor_cpu,
                           'packet_id': result['packet_id'], 'exact_bytes_equal': True,
                           'path': diagnostic['path'], 'stats': diagnostic['stats']}
                    sink.write(codec.safe.canonical(row) + b'\n'); sink.flush()
                totals.append(row)
                print(json.dumps({'timed_run_completed': arm, 'repetition': repetition,
                                  'total_wall_ns': row['total_with_constructor_wall_ns']}), flush=True)
                del cache, result, diagnostic
    save(directory / 'timed_totals_first.json', {'complete': True, 'runs': totals, 'head_observations': 1212})


def memory(directory):
    config, heads = load_frozen(directory)
    out = directory / 'memory_first.jsonl'; totals = []
    with out.open('xb') as sink:
        for arm in config['memory_arms']:
            tracemalloc.start(); cache = make_cache(arm, config)
            for index, head in enumerate(heads):
                result, diagnostic = validate(cache, head)
                current, peak = tracemalloc.get_traced_memory()
                check_result(arm, index, result, diagnostic, config)
                row = {'arm': arm, 'events': index, 'traced_current_bytes': current, 'traced_peak_bytes': peak,
                       'exact_bytes_equal': True, 'path': diagnostic['path'], 'stats': diagnostic['stats']}
                sink.write(codec.safe.canonical(row) + b'\n'); sink.flush()
            tracemalloc.stop(); totals.append(row); del cache, result, diagnostic
            print(json.dumps({'memory_run_completed': arm, 'final_current': current, 'peak': peak}), flush=True)
    save(directory / 'memory_totals_first.json', {'complete': True, 'runs': totals, 'head_observations': 202,
                                                 'strict_whole_cumulative_memory_measured': False})


def main(argv=None):
    parser = argparse.ArgumentParser(); parser.add_argument('action', choices=('freeze', 'timed', 'memory'))
    parser.add_argument('--directory', type=Path, required=True); args = parser.parse_args(argv)
    if args.action == 'freeze': freeze(args.directory)
    elif args.action == 'timed': timed(args.directory)
    else: memory(args.directory)


if __name__ == '__main__': main()
