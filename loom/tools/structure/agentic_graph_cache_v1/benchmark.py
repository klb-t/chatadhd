"""Preregistered, source-pinned CPU/memory cache comparison, never model quality."""
from __future__ import annotations

import argparse
from copy import deepcopy
import gc
import hashlib
import json
from pathlib import Path
import platform
import statistics
import time
import tracemalloc

from . import cache as module
from .test_cache import histories, policy, MODEL, AUTO

codec = module.codec
COUNTS = (0, 5, 20, 50, 100)
METHODS = ('strict', 'whole_packet', 'verified_history')
CONDITIONS = ('cold', 'warm_same_head', 'warm_extension', 'valid_last_event_fork', 'valid_invalidated_old_prefix')
DOC = Path(__file__).resolve().parents[4] / 'docs/research/agentic_graph_cache_v1'


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def save(path, value):
    path.write_bytes(codec.safe.canonical(value) + b'\n')


def read(path):
    return codec.safe.parse_json(path.read_bytes())


def build_fixtures():
    original, changed = histories(max(COUNTS)), histories(max(COUNTS), prefix='old-prefix-changed')
    selected = {}
    for count in COUNTS:
        selected['normal_' + str(count)] = original[count]
        if count:
            selected['normal_' + str(count - 1)] = original[count - 1]
            diff = deepcopy(original[count]['history'][-1]['diff'])
            diff['proposal_id'] = 'competing-last-event-' + str(count)
            fork, _ = codec._candidate(original[count - 1], diff, validate_result=False)
            selected['last_fork_' + str(count)] = fork
            selected['old_prefix_' + str(count)] = changed[count]
    return selected


def condition_keys(count, condition):
    if count == 0 and condition in CONDITIONS[2:]:
        return None, None
    target = ('last_fork_' if condition == 'valid_last_event_fork' else
              'old_prefix_' if condition == 'valid_invalidated_old_prefix' else 'normal_') + str(count)
    prime = None if condition == 'cold' else 'normal_' + str(count - 1 if condition == 'warm_extension' else count)
    return target, prime


def expected_path(method, condition):
    if method == 'strict':
        return 'strict_baseline'
    if condition == 'warm_same_head':
        return 'whole_packet_hit'
    if method == 'verified_history' and condition in ('warm_extension', 'valid_last_event_fork'):
        return 'verified_immediate_parent_hit'
    return 'strict_fallback'


def freeze(directory):
    directory.mkdir(parents=True, exist_ok=False)
    fixtures = build_fixtures()
    source_paths = [Path(__file__), Path(module.__file__), Path(codec.__file__), Path(codec.safe.__file__),
                    Path(__file__).with_name('test_cache.py'), DOC / 'PROTOCOL.md', DOC / 'preset.json',
                    DOC / 'mechanism_tests_final_before_benchmark.txt']
    source_records = [{"path": str(path.resolve()), "sha256": sha(path.read_bytes())} for path in source_paths]
    # All scripted measured/priming heads must pass the unchanged strict codec
    # before timing. Fixture construction is outside the timed validation task.
    for packet in fixtures.values():
        codec.validate_packet(packet)
    save(directory / 'fixtures.json', fixtures)
    config = {'schema': 'loom.graph_packet_cache_benchmark/1', 'counts': list(COUNTS),
              'methods': list(METHODS), 'conditions': list(CONDITIONS), 'timed_repetitions': 3,
              'memory_repetitions': 1, 'timed_planned_cells': 225, 'timed_not_applicable': 27,
              'timed_applicable_cells': 198, 'memory_planned_cells': 75, 'memory_not_applicable': 9,
              'memory_applicable_cells': 66, 'policy': read(DOC / 'preset.json'),
              'fixtures_sha256': sha((directory / 'fixtures.json').read_bytes()),
              'fixture_records': {key: {'packet_id': value['packet_id'], 'canonical_bytes': len(codec.safe.canonical(value)),
                                         'canonical_sha256': sha(codec.safe.canonical(value))}
                                  for key, value in fixtures.items()},
              'fixture_preflight_strict_passed': len(fixtures), 'source_records': source_records,
              'runtime': {'python': platform.python_version(), 'platform': platform.platform()},
              'timed_order': 'method rotation by (repetition + history_index + condition_index) modulo 3',
              'memory_order': 'fixed declared methods; no timing claims from tracemalloc run',
              'semantic_graph': {'entities': 2, 'claims': 1, 'sources': 1}, 'model_quality_measured': False,
              'network_calls': 0, 'paid_calls': 0, 'validation_holdout_read': False,
              'application_receipt_checks': 'each applicable condition/method once outside timed probes',
              'memory_scope': 'tracemalloc starts before cache constructor/prefill; fixture allocation excluded; retained return included',
              'setup_scope': 'cache constructor and full strict priming; strict baseline has zero setup',
              'limitations': ['scripted empty-diff journal; fixed small current graph; no model quality',
                              'Python trusted-runtime binding guard, not a hostile interpreter sandbox',
                              'tracemalloc does not report full process RSS or allocator/native peak']}
    save(directory / 'FREEZE.json', config)
    return config


def load_frozen(directory):
    config = read(directory / 'FREEZE.json')
    if sha((directory / 'fixtures.json').read_bytes()) != config['fixtures_sha256']:
        raise RuntimeError('benchmark_fixtures_changed')
    for record in config['source_records']:
        if sha(Path(record['path']).read_bytes()) != record['sha256']:
            raise RuntimeError('benchmark_source_changed:' + record['path'])
    if config['policy'] != read(DOC / 'preset.json'):
        raise RuntimeError('benchmark_policy_changed')
    return config, read(directory / 'fixtures.json')


def create_cache(method, config):
    if method == 'strict':
        return None
    selected = dict(config['policy'], mode=method)
    return module.VerifiedPacketCache(selected)


def validate(cache, packet):
    if cache is None:
        return codec.validate_packet(packet), {'path': 'strict_baseline', 'stats': None}
    return cache.validate_with_receipt(packet)


def prepare(method, config, fixtures, prime_key):
    wall, cpu = time.perf_counter_ns(), time.process_time_ns()
    cache = create_cache(method, config)
    if cache is not None and prime_key is not None:
        cache.validate_packet(fixtures[prime_key])
    return cache, {'setup_wall_ns': time.perf_counter_ns() - wall,
                   'setup_cpu_ns': time.process_time_ns() - cpu,
                   'stats_after_setup': cache.stats() if cache is not None else None}


def equivalence(directory, config, fixtures):
    rows = []
    target_expected = {}
    for count in COUNTS:
        for condition in CONDITIONS:
            target_key, prime_key = condition_keys(count, condition)
            if target_key is None:
                continue
            packet = fixtures[target_key]
            if target_key not in target_expected:
                diff = codec.empty_diff(packet, proposal_id='equivalence-' + target_key, origin=MODEL)
                applied, receipt = codec.apply_diff(packet, diff, AUTO)
                target_expected[target_key] = (diff, codec.safe.canonical(applied), codec.safe.canonical(receipt))
            diff, expected_packet, expected_receipt = target_expected[target_key]
            for method in METHODS:
                cache, _ = prepare(method, config, fixtures, prime_key)
                result, diagnostic = validate(cache, packet)
                applied, receipt = codec.apply_diff(result, diff, AUTO)
                if (codec.safe.canonical(result) != codec.safe.canonical(packet) or
                        codec.safe.canonical(applied) != expected_packet or
                        codec.safe.canonical(receipt) != expected_receipt or
                        diagnostic['path'] != expected_path(method, condition) or
                        codec.invert_application(receipt, applied) != packet):
                    raise RuntimeError('benchmark_equivalence_failed:' + target_key + ':' + method + ':' + condition)
                rows.append({'count': count, 'condition': condition, 'method': method, 'path': diagnostic['path'],
                             'exact_validation_bytes_equal': True, 'exact_applied_bytes_equal': True,
                             'exact_application_receipt_bytes_equal': True, 'inverse_exact': True})
                save(directory / 'equivalence_first.json', {'rows': rows, 'complete': False})
    save(directory / 'equivalence_first.json', {'rows': rows, 'complete': True, 'passed': len(rows)})


def timed(directory):
    config, fixtures = load_frozen(directory)
    if (directory / 'timed_first.json').exists():
        raise RuntimeError('first_timed_results_already_exist')
    equivalence(directory, config, fixtures)
    rows = []
    for history_index, count in enumerate(COUNTS):
        for condition_index, condition in enumerate(CONDITIONS):
            target_key, prime_key = condition_keys(count, condition)
            for repetition in range(3):
                offset = (history_index + condition_index + repetition) % len(METHODS)
                order = METHODS[offset:] + METHODS[:offset]
                for method in order:
                    if target_key is None:
                        rows.append({'count': count, 'condition': condition, 'method': method,
                                     'repetition': repetition, 'applicable': False, 'reason': 'no history event at zero'})
                        continue
                    cache, setup = prepare(method, config, fixtures, prime_key)
                    wall, cpu = time.perf_counter_ns(), time.process_time_ns()
                    result, diagnostic = validate(cache, fixtures[target_key])
                    measured = {'wall_ns': time.perf_counter_ns() - wall, 'cpu_ns': time.process_time_ns() - cpu}
                    raw = codec.safe.canonical(result)
                    if raw != codec.safe.canonical(fixtures[target_key]) or diagnostic['path'] != expected_path(method, condition):
                        raise RuntimeError('benchmark_timed_result_mismatch')
                    rows.append({'count': count, 'condition': condition, 'method': method, 'repetition': repetition,
                                 'applicable': True, 'target_key': target_key, 'prime_key': prime_key,
                                 'canonical_bytes': len(raw), 'canonical_sha256': sha(raw),
                                 'path': diagnostic['path'], 'stats': diagnostic['stats'], **setup, **measured})
                    save(directory / 'timed_first.json', {'rows': rows, 'complete': False})
        print(json.dumps({'timed_history_completed': count, 'rows_preserved': len(rows)}), flush=True)
    save(directory / 'timed_first.json', {'rows': rows, 'complete': True,
                                         'applicable': sum(row['applicable'] for row in rows),
                                         'not_applicable': sum(not row['applicable'] for row in rows)})


def memory(directory):
    config, fixtures = load_frozen(directory)
    if (directory / 'memory_first.json').exists():
        raise RuntimeError('first_memory_results_already_exist')
    rows = []
    for count in COUNTS:
        for condition in CONDITIONS:
            target_key, prime_key = condition_keys(count, condition)
            for method in METHODS:
                if target_key is None:
                    rows.append({'count': count, 'condition': condition, 'method': method, 'applicable': False,
                                 'reason': 'no history event at zero'})
                    continue
                gc.collect(); tracemalloc.start()
                cache, _ = prepare(method, config, fixtures, prime_key)
                setup_current, setup_peak = tracemalloc.get_traced_memory(); tracemalloc.reset_peak()
                result, diagnostic = validate(cache, fixtures[target_key])
                current, peak = tracemalloc.get_traced_memory(); tracemalloc.stop()
                if codec.safe.canonical(result) != codec.safe.canonical(fixtures[target_key]):
                    raise RuntimeError('benchmark_memory_result_mismatch')
                rows.append({'count': count, 'condition': condition, 'method': method, 'applicable': True,
                             'setup_traced_current_bytes': setup_current, 'setup_traced_peak_bytes': setup_peak,
                             'call_traced_current_bytes': current, 'call_traced_peak_bytes': peak,
                             'call_traced_peak_over_setup_current_bytes': peak - setup_current,
                             'path': diagnostic['path'], 'stats': diagnostic['stats']})
                save(directory / 'memory_first.json', {'rows': rows, 'complete': False})
                del cache, result, diagnostic
        print(json.dumps({'memory_history_completed': count, 'rows_preserved': len(rows)}), flush=True)
    save(directory / 'memory_first.json', {'rows': rows, 'complete': True,
                                          'applicable': sum(row['applicable'] for row in rows),
                                          'not_applicable': sum(not row['applicable'] for row in rows)})


def summarize(directory):
    config, _ = load_frozen(directory)
    timed_result, memory_result, checked = (read(directory / name) for name in
                                           ('timed_first.json', 'memory_first.json', 'equivalence_first.json'))
    if not all(value['complete'] for value in (timed_result, memory_result, checked)):
        raise RuntimeError('benchmark_results_incomplete')
    medians = []
    for count in COUNTS:
        for condition in CONDITIONS:
            baseline = None
            for method in METHODS:
                samples = [row for row in timed_result['rows'] if row['applicable'] and
                           (row['count'], row['condition'], row['method']) == (count, condition, method)]
                if not samples:
                    continue
                median = {key: statistics.median(row[key] for row in samples)
                          for key in ('wall_ns', 'cpu_ns', 'setup_wall_ns', 'setup_cpu_ns')}
                if method == 'strict':
                    baseline = median
                memory_row = next(row for row in memory_result['rows'] if row['applicable'] and
                                  (row['count'], row['condition'], row['method']) == (count, condition, method))
                medians.append({'count': count, 'condition': condition, 'method': method, 'repetitions': len(samples),
                                'median': median, 'wall_speedup_over_strict': baseline['wall_ns'] / median['wall_ns'],
                                'cpu_speedup_over_strict': baseline['cpu_ns'] / median['cpu_ns'],
                                'wall_with_setup_speedup_over_strict':
                                    baseline['wall_ns'] / (median['wall_ns'] + median['setup_wall_ns']),
                                'canonical_bytes': samples[0]['canonical_bytes'], 'path': samples[0]['path'],
                                'cache_stats': samples[0]['stats'], 'memory': memory_row})
    summary = {'freeze_sha256': sha((directory / 'FREEZE.json').read_bytes()),
               'timed_applicable': timed_result['applicable'], 'timed_not_applicable': timed_result['not_applicable'],
               'memory_applicable': memory_result['applicable'], 'memory_not_applicable': memory_result['not_applicable'],
               'equivalence_checks_passed': checked['passed'], 'model_quality_measured': False,
               'rows': medians, 'decision': 'analysis_required_keep_opt_in_only_if_correctness_and_informative_condition_gain'}
    save(directory / 'SUMMARY_FIRST.json', summary)
    return summary


def main(argv=None):
    parser = argparse.ArgumentParser(); parser.add_argument('action', choices=('freeze', 'timed', 'memory', 'summarize'))
    parser.add_argument('--directory', type=Path, required=True); args = parser.parse_args(argv)
    if args.action == 'freeze':
        result = freeze(args.directory)
        print(json.dumps({'freeze': str(args.directory / 'FREEZE.json'), 'preflight': result['fixture_preflight_strict_passed']}))
    elif args.action == 'timed': timed(args.directory)
    elif args.action == 'memory': memory(args.directory)
    else:
        result = summarize(args.directory)
        print(json.dumps({key: value for key, value in result.items() if key != 'rows'}))


if __name__ == '__main__':
    main()
