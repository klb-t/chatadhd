#!/usr/bin/env python3
"""Reproduce rejected offline prompt fixtures, without changing production files."""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess

HERE = Path(__file__).resolve().parent


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', required=True, type=Path)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--case', choices=['both', 'ordered-json', 'legacy-wire'], default='both')
    parser.add_argument('--cxx', default='c++')
    args = parser.parse_args()
    repo = args.repo.resolve()
    build = (args.build_dir or repo / 'loom/build/dev').resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    libraries = [build / name for name in ('libloom_core.a', 'libloom_sqlite3_amalgamation.a', 'libloom_miniz.a')]
    for path in libraries:
        if not path.is_file():
            parser.error(f'missing matching prebuilt library: {path}')
    common = [args.cxx, '-std=c++20', '-O0', '-g0', '-I' + str(repo / 'loom/include'),
              '-I' + str(repo / 'loom/src'), '-I' + str(repo / 'loom/tests'),
              '-isystem', str(repo / 'loom/third_party/nlohmann'),
              '-I' + str(repo / 'loom/third_party/doctest'), '-I' + str(repo / 'loom/third_party/miniz')]
    link_flags = ['-Wl,--no-keep-memory', *map(str, libraries), '-pthread', '-ldl', '-lm', '-lssl', '-lcrypto']
    invocations = []

    def execute(label: str, argv: list[str]) -> subprocess.CompletedProcess[str]:
        result = subprocess.run(argv, cwd=repo, text=True, capture_output=True, check=False)
        (output / f'{label}.stdout.txt').write_text(result.stdout)
        (output / f'{label}.stderr.txt').write_text(result.stderr)
        invocations.append({'label': label, 'argv': argv, 'exit_code': result.returncode,
                            'stdout': f'{label}.stdout.txt', 'stderr': f'{label}.stderr.txt'})
        return result

    observations = {}
    if args.case in ('both', 'legacy-wire'):
        case = HERE / 'legacy-analysis-wire-order'
        registry_object = output / 'historical_registry.o'
        registry = case / 'registry/prompt_contract_legacy.cpp.fixture'
        result = execute('legacy-registry-compile', [*common[:4], '-I' + str(case / 'registry/include'),
            *common[4:], '-x', 'c++', '-c', str(registry), '-o', str(registry_object)])
        if result.returncode != 0:
            raise RuntimeError('archived registry compilation failed; see retained diagnostics')
        binary = output / 'legacy_wire_probe'
        # The standalone historical registry object precedes the normal core;
        # the core's production registry object must not replace this variant.
        result = execute('legacy-probe-link', [*common, '-x', 'c++',
            str(case / 'legacy_shared_builder_probe.cpp.fixture'), '-x', 'none',
            str(registry_object), *link_flags, '-o', str(binary)])
        if result.returncode != 0:
            raise RuntimeError('archived legacy helper link failed; see retained diagnostics')
        result = execute('legacy-wire-run', [str(binary), str(case / 'recovered_analysis.prompt')])
        actual = json.loads(result.stdout)
        first = json.loads((case / 'first_receipt.json').read_text())
        reproduced = result.returncode == 0 and actual == first
        observations['legacy_wire'] = {'expected_negative_reproduced': reproduced,
            'contract_hash': actual.get('prompt_contract_hash'),
            'wire_bytes_equal': actual.get('equal_body_bytes_count'),
            'body_semantics_equal': actual.get('equal_body_semantics_count'),
            'content_bytes_equal': actual.get('equal_content_count'),
            'comparison': 'entire new receipt equals historical receipt, including all six request body hashes',
            'quality_result': 'rejected exact-wire compatibility; not an accepted default'}
        if not reproduced:
            raise RuntimeError('legacy output differs from the original 0/3 wire receipt')

    if args.case in ('both', 'ordered-json'):
        if (repo / 'loom/include/loom/usage_policy.h').exists():
            raise RuntimeError('exact original 16-case oracle requires the recorded pre-W2 build context')
        case = HERE / 'ordered-json-oracle'
        binary = output / 'ordered_json_oracle_probe'
        result = execute('ordered-oracle-link', [*common, '-x', 'c++',
            str(case / 'semantic_prompt_integration.cpp.fixture'), '-x', 'none',
            *link_flags, '-o', str(binary)])
        if result.returncode != 0:
            raise RuntimeError('archived order-oracle compilation failed; see retained diagnostics')
        result = execute('ordered-oracle-run', [str(binary), '--no-colors=1'])
        cases = re.search(r'test cases:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed', result.stdout)
        checks = re.search(r'assertions:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed', result.stdout)
        reproduced = result.returncode == 1 and cases is not None and checks is not None
        reproduced = reproduced and tuple(map(int, cases.groups())) == (16, 15, 1)
        reproduced = reproduced and tuple(map(int, checks.groups())) == (242, 240, 2)
        observations['ordered_json_oracle'] = {'expected_negative_reproduced': reproduced,
            'cases': tuple(map(int, cases.groups())) if cases else None,
            'assertions': tuple(map(int, checks.groups())) if checks else None,
            'quality_result': 'rejected order-sensitive fixture oracle; not native field/raw-output loss'}
        if not reproduced:
            raise RuntimeError('original 15/16,240/242 ordered-JSON oracle result was not reproduced')

    record = {'schema': 'loom.archived_negative_prompt_replay_result/1',
        'status': 'expected_negative_results_reproduced',
        'completed_utc': datetime.now(timezone.utc).isoformat(),
        'paid_calls': 0, 'live_provider_calls': 0,
        'production_files_modified': False, 'new_core_build': False,
        'observations': observations, 'invocations': invocations,
        'archive_sources': [{'path': str(p.relative_to(HERE)), 'sha256': sha(p)}
            for p in sorted(HERE.rglob('*')) if p.is_file()],
        'linked_libraries': [{'path': str(p), 'sha256': sha(p), 'bytes': p.stat().st_size} for p in libraries]}
    (output / 'replay_receipt.json').write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'status': record['status'], 'observations': observations, 'paid_calls': 0}, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
