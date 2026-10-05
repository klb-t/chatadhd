#!/usr/bin/env python3
"""Retain the exact failing fixture and replay the pre-fix output branch.

This is a bounded branch reproduction, not a claim that the complete historical
guard source was recovered. Current guard/source/schema identities are recorded.
All writes below operate only on temporary synthetic roots.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
GUARD = ROOT / 'loom/src/util/product_literal_guard.py'


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def pre_fix_output_branch(guard, root, pack, schema, report_path, manifest_path):
    # Historical run2 writer logic, before adding protection roots on errors.
    inputs = {pack.resolve(), schema.resolve()}
    destinations = [report_path.resolve(), manifest_path.resolve()]
    try:
        report, manifest = guard.audit(root, pack, schema)
        code = 0 if report['valid'] else 1
    except Exception as error:
        report = {'schema': guard.REPORT_SCHEMA, 'valid': False,
                  'counts': {'files': 0, 'candidates': 0, 'allowed': 0,
                             'unclassified': 0, 'blocked_files': 0, 'stale_allowlist': 0},
                  'findings': [], 'issues': [guard.issue('configuration_error', str(error))],
                  'io': {'network_calls': 0, 'source_writes': 0, 'generator_output_writes': 0}}
        manifest = {'schema': guard.MANIFEST_SCHEMA, 'source_files': []}
        code = 2
    protected = set(inputs)
    protected.add(Path(guard.__file__).resolve())
    for source in manifest['source_files']:
        protected.add((root / source['path']).resolve())
        for field in source.get('generation', {}).get('inputs', []):
            protected.add((root / field['path']).resolve())
    scopes = [root / scope for scope in manifest.get('declared_scopes', [])]
    if any(path in protected or any(path == scope.resolve() or path.is_relative_to(scope.resolve())
                                   for scope in scopes) for path in destinations):
        return 2
    for path, value in ((report_path, report), (manifest_path, manifest)):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return code


def main():
    spec = importlib.util.spec_from_file_location('negative_branch_current_guard', GUARD)
    guard = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(guard)
    fixture = HERE / 'negative-run-2-fixture'
    with tempfile.TemporaryDirectory(prefix='literal-negative-replay-') as temporary:
        root = Path(temporary)
        source = root / 'loom/src/model/preserved.cpp'
        source.parent.mkdir(parents=True)
        source.write_bytes((fixture / 'source.cpp').read_bytes())
        pack, schema = root / 'policy.pack', root / 'schema.json'
        shutil.copyfile(fixture / 'policy.pack', pack)
        shutil.copyfile(fixture / 'schema.json', schema)
        before = source.read_bytes()
        legacy_exit = pre_fix_output_branch(guard, root, pack, schema, source, root / 'legacy-manifest.json')
        overwritten = source.read_bytes()
        source.write_bytes(before)
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            fixed_exit = guard.main(['--root', str(root), '--pack', str(pack), '--schema', str(schema),
                                     '--check', '--report', str(source), '--manifest', str(root / 'fixed-manifest.json')])
        fixed = source.read_bytes()
        source.unlink()
        shutil.copyfile(fixture / 'policy-revision2.pack', pack)
        empty_report, _ = guard.audit(root, pack, schema)
        empty_exit = 0 if empty_report['valid'] else 1
        observation = {'schema': 'loom.product_literal_negative_branch_replay/1',
                       'historical_full_guard_recovered': False,
                       'reproduction': 'Exact source/invalid-reason fixture and retained pre-fix output branch; current validation mechanism.',
                       'guard_sha256': sha(GUARD.read_bytes()),
                       'replay_sha256': sha(Path(__file__).read_bytes()),
                       'fixture_files': [{'path': p.name, 'sha256': sha(p.read_bytes())} for p in sorted(fixture.iterdir()) if p.is_file()],
                       'source_before_sha256': sha(before),
                       'pre_fix_exit': legacy_exit, 'pre_fix_source_overwritten': overwritten != before,
                       'pre_fix_overwritten_sha256': sha(overwritten),
                       'fixed_exit': fixed_exit, 'fixed_source_unchanged': fixed == before,
                       'fixed_stdout': stdout.getvalue(), 'fixed_stderr': stderr.getvalue(),
                       'revision2_schema_rejected': any(row['code'] == 'configuration_error' for row in empty_report['issues']),
                       'revision2_empty_scope_exit': empty_exit,
                       'revision2_issue_codes': [row['code'] for row in empty_report['issues']],
                       'network_calls': 0, 'repository_source_writes': 0}
        print(json.dumps(observation, ensure_ascii=False, indent=2))
        return 0 if legacy_exit == 2 and overwritten != before and fixed_exit == 2 and fixed == before and empty_exit == 1 else 1


if __name__ == '__main__':
    raise SystemExit(main())
