#!/usr/bin/env python3
"""Validate the audit bundle, not the audited products or secret-free universality."""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--root', type=Path, required=True, help='Repository or staged artifact root')
p.add_argument('--output', type=Path, required=True)
p.add_argument('--checkout', action='append', default=[], help='repo_full_name=checkout path for source-range checks')
a = p.parse_args()
root = a.root.resolve()
reports = root / 'docs/reports/ecosystem-audit-2026-10-09/pass4'
tools = root / 'tools/ecosystem-audit-2026-10-09/pass4'
checkouts = dict(x.split('=', 1) for x in a.checkout)
errors, files, source_checks = [], [], 0
for top in [reports, tools]:
    for f in sorted(top.rglob('*')):
        if not f.is_file() or '__pycache__' in f.parts or f == a.output.resolve():
            continue
        try:
            raw = f.read_bytes()
            text = raw.decode('utf-8-sig')
            if '\0' in text:
                raise ValueError('NUL in text artifact')
            if f.suffix == '.json':
                json.loads(text)
            elif f.suffix == '.jsonl':
                for line in text.splitlines():
                    if line.strip(): json.loads(line)
            elif f.suffix == '.py':
                ast.parse(text)
            files.append({'path': str(f.relative_to(root)), 'sha256': hashlib.sha256(raw).hexdigest()})
        except Exception as e:
            errors.append({'path': str(f), 'error': type(e).__name__ + ': ' + str(e)})
index = json.loads((tools / 'test-index.json').read_text())
for suite in index['suites']:
    scripts = [arg.replace('{audit_root}', str(tools)) for arg in suite['argv'] if arg.startswith('{audit_root}/')]
    if len(scripts) != 1 or not Path(scripts[0]).is_file():
        errors.append({'suite': suite['id'], 'error': 'Missing or ambiguous actual runner script'})
for row in json.loads((reports / 'coverage-index.json').read_text())['module_ranges']:
    if row['repo'] not in checkouts:
        continue
    try:
        raw = subprocess.check_output(['git', '-C', checkouts[row['repo']], 'show', row['sha'] + ':' + row['file']], stderr=subprocess.DEVNULL)
        assert hashlib.sha256(raw).hexdigest() == row['source_sha256'], 'source byte drift'
        assert all(1 <= start <= end <= len(raw.splitlines()) for start, end in row['ranges']), 'invalid line range'
        source_checks += 1
    except Exception as e:
        errors.append({'source': row['file'], 'sha': row['sha'], 'error': str(e)})
result = {'schema': 'klbt.audit.pass4.bundle-validation/1', 'status': 'FAIL' if errors else 'PASS',
          'text_files': len(files), 'source_range_rows_checked': source_checks,
          'executable_entries': len(index['suites']), 'errors': errors,
          'meaning': 'Syntax, paths and pinned source bounds only; no claim of product correctness or exhaustive privacy scanning.'}
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
raise SystemExit(bool(errors))
