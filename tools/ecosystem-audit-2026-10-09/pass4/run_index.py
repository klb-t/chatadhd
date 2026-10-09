#!/usr/bin/env python3
"""Run a named PASS4 suite on a pinned checkout; argv only, no shell evaluation."""
import argparse
import json
from pathlib import Path
import string
import subprocess
import sys


def render(entry, values):
    required = {key for arg in entry['argv'] for _, key, _, _ in string.Formatter().parse(arg) if key}
    missing = required - values.keys()
    if missing:
        raise ValueError('Missing values: ' + ', '.join(sorted(missing)))
    return [arg.format_map(values) for arg in entry['argv']]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('suite', nargs='?')
    p.add_argument('--list', action='store_true')
    p.add_argument('--repo', type=Path)
    p.add_argument('--sha')
    p.add_argument('--output', type=Path)
    p.add_argument('--set', action='append', default=[], metavar='NAME=VALUE')
    p.add_argument('--show', action='store_true', help='Resolve argv without executing the suite')
    a = p.parse_args()
    root = Path(__file__).resolve().parent
    entries = json.loads(root.joinpath('test-index.json').read_text())['suites']
    if a.list:
        for e in entries:
            print(e['id'] + ': ' + e['scope'])
        return 0
    entry = next((e for e in entries if e['id'] == a.suite), None)
    if entry is None:
        p.error('Choose a known suite from --list')
    values = {'python': sys.executable, 'audit_root': str(root)}
    if a.repo: values['repo'] = str(a.repo.resolve())
    if a.sha: values['sha'] = a.sha
    if a.output: values['output'] = str(a.output.resolve())
    reserved = set(values) | {'repo', 'sha', 'output'}
    for item in a.set:
        name, sep, value = item.partition('=')
        if not sep or not name or name in reserved or name in values:
            p.error('Invalid or repeated dependency name: ' + name)
        values[name] = value
    try:
        argv = render(entry, values)
    except ValueError as e:
        p.error(str(e))
    if a.show:
        print(json.dumps({'suite': entry['id'], 'argv': argv, 'shell': False}, indent=2))
        return 0
    if a.repo is None or a.sha is None or a.output is None:
        p.error('--repo, --sha and --output are required to execute')
    observed = subprocess.check_output(['git', '-C', str(a.repo), 'rev-parse', 'HEAD'], text=True).strip()
    if observed != a.sha:
        p.error('Checkout HEAD differs from requested SHA')
    subprocess.run(['git', '-C', str(a.repo), 'diff', '--exit-code', a.sha, '--'], check=True,
                   stdout=subprocess.DEVNULL)
    result = subprocess.run(argv, shell=False)
    print(json.dumps({'suite': entry['id'], 'exit_code': result.returncode,
                      'meaning': 'See per-case categories: reproduction PASS is not product PASS.'}))
    return result.returncode


if __name__ == '__main__':
    sys.exit(main())
