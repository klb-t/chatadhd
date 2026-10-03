"""Preserved100-event sibling-fork policy probe, not a timing benchmark."""
from __future__ import annotations
import argparse
from copy import deepcopy
from pathlib import Path

from . import cache as module
from .chain_benchmark import load_frozen, sha, save, read, DOC
from .test_cache import MODEL, AUTO

codec = module.codec


def freeze(directory):
    config, heads = load_frozen(directory)
    diff = deepcopy(heads[100]['history'][-1]['diff']); diff['proposal_id'] = 'alternative-final-proposal-100'
    fork, _ = codec._candidate(heads[99], diff, validate_result=False); codec.validate_packet(fork)
    save(directory / 'retention_fork_fixture.json', fork)
    paths = [Path(__file__), Path(__file__).with_name('test_retention_fork.py'),
             DOC / 'RETENTION_FORK_PROTOCOL.md', Path(module.__file__), Path(codec.__file__)]
    save(directory / 'RETENTION_FORK_FREEZE.json', {
        'source_records': [{'path': str(path.resolve()), 'sha256': sha(path.read_bytes())} for path in paths],
        'fixture_sha256': sha((directory / 'retention_fork_fixture.json').read_bytes()),
        'chain_freeze_sha256': sha((directory / 'FREEZE.json').read_bytes()),
        'head100_packet_id': heads[100]['packet_id'], 'head99_packet_id': heads[99]['packet_id'],
        'fork_packet_id': fork['packet_id'], 'strict_preflight_passed': True,
        'arms': [{'max_entries': 128, 'expected_path': 'verified_immediate_parent_hit'},
                 {'max_entries': 1, 'expected_path': 'strict_fallback'}],
        'timing_measured': False, 'model_quality_measured': False, 'paid_calls': 0})


def run(directory):
    config, heads = load_frozen(directory); frozen = read(directory / 'RETENTION_FORK_FREEZE.json')
    for record in frozen['source_records']:
        if sha(Path(record['path']).read_bytes()) != record['sha256']:
            raise RuntimeError('retention_probe_source_changed')
    if sha((directory / 'retention_fork_fixture.json').read_bytes()) != frozen['fixture_sha256']:
        raise RuntimeError('retention_probe_fixture_changed')
    fork = read(directory / 'retention_fork_fixture.json')
    diff = codec.empty_diff(fork, proposal_id='unchanged-application', origin=MODEL)
    expected_packet, expected_receipt = codec.apply_diff(fork, diff, AUTO)
    results = []
    for arm in frozen['arms']:
        policy = dict(config['policy'], max_entries=arm['max_entries'])
        cache = module.VerifiedPacketCache(policy); cache.validate_packet(heads[100])
        result, receipt = cache.validate_with_receipt(fork)
        applied, application_receipt = codec.apply_diff(result, diff, AUTO)
        if (receipt['path'] != arm['expected_path'] or codec.safe.canonical(result) != codec.safe.canonical(fork) or
                codec.safe.canonical(applied) != codec.safe.canonical(expected_packet) or
                codec.safe.canonical(application_receipt) != codec.safe.canonical(expected_receipt) or
                codec.invert_application(application_receipt, applied) != fork):
            raise RuntimeError('retention_probe_equivalence_failed')
        results.append({'max_entries': arm['max_entries'], 'path': receipt['path'], 'exact_packet_bytes_equal': True,
                        'exact_application_receipt_bytes_equal': True, 'exact_inverse': True, 'stats': receipt['stats']})
        save(directory / 'retention_fork_first.json', {'complete': False, 'rows': results})
    save(directory / 'retention_fork_first.json', {'complete': True, 'rows': results,
                                                 'timing_measured': False, 'semantic_quality_measured': False})


def main(argv=None):
    parser = argparse.ArgumentParser(); parser.add_argument('action', choices=('freeze', 'run'))
    parser.add_argument('--directory', type=Path, required=True); args = parser.parse_args(argv)
    if args.action == 'freeze': freeze(args.directory)
    else: run(args.directory)


if __name__ == '__main__': main()
