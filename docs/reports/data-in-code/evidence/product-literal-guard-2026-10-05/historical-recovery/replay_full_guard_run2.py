#!/usr/bin/env python3
"""Replay complete historical-derived guard source against retained synthetic inputs.

This is a new replay, not recovery or replacement of the original run2 log.
The historical source is derived by documented patch reversal from an independently
SHA-verified run3 source. No repository product source is executed or modified.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


HERE = Path(__file__).resolve().parent
FIXTURE = HERE.parent / 'negative-run-2-fixture'


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def copied(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)


def main():
    archived_guard = HERE / 'guard-run2-derived.py'
    archived_schema = HERE / 'schema-run2-run3-verified.json'
    assert sha(archived_guard.read_bytes()) == '6278d048a8c9004cdbadbb0d48b9bcea513587c9a718749291cebaffeabfade2'
    assert sha(archived_schema.read_bytes()) == 'fbdffaf662c94af95731430f466238bb9eb53f499e0be010066f46e7cda1843d'
    with tempfile.TemporaryDirectory(prefix='historical-literal-guard-') as temporary:
        temporary = Path(temporary)
        tools, root = temporary / 'archived-tool-repo', temporary / 'synthetic-input-repo'
        guard = tools / 'loom/src/util/product_literal_guard.py'
        copied(archived_guard, guard)
        copied(archived_schema, tools / 'docs/contracts/product_literals.schema.json')
        source = root / 'loom/src/model/preserved.cpp'
        pack = root / 'loom/data/validation/product_literals.pack'
        schema = root / 'docs/contracts/product_literals.schema.json'
        copied(FIXTURE / 'source.cpp', source)
        copied(FIXTURE / 'policy.pack', pack)
        copied(archived_schema, schema)
        before = source.read_bytes()
        command = [sys.executable, '-B', str(guard), '--root', str(root), '--pack', str(pack),
                   '--schema', str(schema), '--check', '--report', str(source),
                   '--manifest', str(root / 'manifest.json')]
        bad = subprocess.run(command, cwd=root, capture_output=True, check=False, timeout=60)
        after = source.read_bytes()
        source.unlink()
        copied(FIXTURE / 'policy-revision2.pack', pack)
        empty_command = command.copy()
        empty_command[empty_command.index('--report') + 1] = str(root / 'empty-report.json')
        empty = subprocess.run(empty_command, cwd=root, capture_output=True, check=False, timeout=60)
        empty_report = json.loads((root / 'empty-report.json').read_bytes())
        result = {'schema': 'loom.product_literal_full_historical_replay/1',
                  'kind': 'New scoped subprocess replay; not an original log recovery.',
                  'source_provenance': 'Complete run2 source derived by reversing the recorded post-run2 protection patch from SHA-verified run3.',
                  'archived_guard_sha256': sha(archived_guard.read_bytes()),
                  'schema_sha256': sha(archived_schema.read_bytes()),
                  'fixture_policy_sha256': sha((FIXTURE / 'policy.pack').read_bytes()),
                  'replay_sha256': sha(Path(__file__).read_bytes()),
                  'overwrite_case': {'command': command, 'exit': bad.returncode,
                                     'before_sha256': sha(before), 'after_sha256': sha(after),
                                     'source_overwritten': before != after,
                                     'exact_overwritten_source': after.decode('utf-8'),
                                     'stdout': bad.stdout.decode('utf-8'), 'stderr': bad.stderr.decode('utf-8')},
                  'revision2_case': {'command': empty_command, 'exit': empty.returncode,
                                     'schema_rejected': any(error['code'] == 'configuration_error' for error in empty_report['issues']),
                                     'issue_codes': [error['code'] for error in empty_report['issues']],
                                     'stdout': empty.stdout.decode('utf-8'), 'stderr': empty.stderr.decode('utf-8')},
                  'repository_source_writes': 0, 'network_calls': 0}
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0 if bad.returncode == 2 and before != after and empty.returncode == 1 and not result['revision2_case']['schema_rejected'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
