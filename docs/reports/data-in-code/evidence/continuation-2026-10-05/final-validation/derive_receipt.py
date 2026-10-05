"""Derive a receipt from existing actual gate outputs; never rerun product tests."""
import argparse
import hashlib
import json
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo-root', type=Path, required=True)
    parser.add_argument('--proof', type=Path, required=True)
    args = parser.parse_args()
    repo, proof = args.repo_root.resolve(), args.proof.resolve()
    snapshot = json.loads((proof / 'tested-source-snapshot.json').read_text())
    changed = [name for name, row in snapshot['files'].items()
               if not (repo / name).is_file() or sha(repo / name) != row['sha256']]
    current = json.loads((proof / 'coverage.json').read_text())
    legacy = json.loads((proof / 'coverage-legacy.json').read_text())
    count_keys = ['valid', 'manifest_entries', 'junit_entries', 'executed_entries',
                  'unexecuted_entries', 'executed_native_cases',
                  'executed_native_assertions', 'executed_python_cases',
                  'skipped_python_cases', 'entries']
    differences = {key: {'current': current.get(key), 'legacy': legacy.get(key)}
                   for key in count_keys if current.get(key) != legacy.get(key)}
    statuses = {name: int((proof / (name + '.exit')).read_text())
                for name in ['build', 'build-glob', 'ctest', 'web-install',
                             'web-build', 'runtime-generator', 'usage-generator',
                             'coverage', 'coverage-legacy']}
    cli_proof = proof.parent / 'cli-fullcore-after'
    cli = json.loads((cli_proof / 'comparison.json').read_text())
    cli_capture = json.loads((cli_proof / 'receipt.json').read_text())
    counts = {key: current.get(key) for key in count_keys if key != 'entries'}
    expected_inputs = {'manifest': sha(proof / 'ctest-manifest.json'),
                       'junit': sha(proof / 'ctest.xml')}
    input_hash_match = (current.get('input_sha256') == expected_inputs
                        and legacy.get('input_sha256') == expected_inputs)
    valid = (not changed and not differences and all(value == 0 for value in statuses.values())
             and current.get('valid') is True and legacy.get('valid') is True
             and cli.get('valid') is True and input_hash_match)
    artifacts = {}
    for path in sorted(proof.rglob('*')):
        if path.is_file() and path != proof / 'receipt.json' and '__pycache__' not in path.parts:
            artifacts[str(path.relative_to(proof))] = {'bytes': path.stat().st_size, 'sha256': sha(path)}
    result = {
        'schema': 'loom.thread11.final_gate_receipt/1', 'date': '2026-10-05',
        'valid': valid, 'base_main': 'e4109df7e4af22b461def5f7d62e268d9b9a8825',
        'source_commit': snapshot['source_head'], 'source_tree': snapshot['source_tree'],
        'source_files_verified': len(snapshot['files']), 'changed_source_files': changed,
        'statuses': statuses, 'coverage': counts, 'guard_observation_differences': differences,
        'coverage_input_sha256': current.get('input_sha256'),
        'legacy_input_sha256': legacy.get('input_sha256'),
        'actual_input_sha256': expected_inputs, 'guard_input_hash_match': input_hash_match,
        'policy_provenance': current.get('policy_provenance'),
        'fullcore_cli': cli, 'fullcore_cli_capture': {
            'path': '../cli-fullcore-after/receipt.json',
            'sha256': sha(cli_proof / 'receipt.json'),
            'bytes': (cli_proof / 'receipt.json').stat().st_size,
            'source_ref': cli_capture['source_ref'], 'binary': cli_capture['binary'],
            'domains': cli_capture['domains'], 'help_bytes': cli_capture['help_bytes'],
            'version_bytes': cli_capture['version_bytes'],
            'notes': 'The complete capture/inspection streams are retained at the referenced path, without duplicating all profile JSON in this gate receipt.',
        },
        'artifact_sha256': artifacts,
        'boundaries': [
            'Gate statuses are the observed exit codes retained by the root shell sessions; this script only derives and rechecks evidence.',
            'Source snapshot was captured after the build and while CTest ran; all R42 sources were frozen before CTest started. Later changes are documentation/evidence only.',
            'C++ RuntimeProfile layout changed: full native build and linked consumers are tested together, without old-ABI objects.',
            'The existing catalog-scale opt-in remains explicitly unexecuted; it contributes no executed cases or assertions.',
            'Synthetic literal-guard tests are included in CTest; the actual repository R42 audit is separately negative and is not accepted by this receipt.',
            'Three import domains are UNWIRED; successful inspection does not activate W5 or the W2 config bridge.',
            'Thread9 main acceptance is not claimed; this is the thread11 branch gate on the stated base.',
        ],
    }
    (proof / 'receipt.json').write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n')
    print(json.dumps({'valid': valid, 'source_commit': result['source_commit'],
                      'counts': counts, 'statuses': statuses,
                      'changed_sources': changed, 'guard_differences': differences}, indent=2))
    return 0 if valid else 1


if __name__ == '__main__':
    raise SystemExit(main())
