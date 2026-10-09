#!/usr/bin/env python3
"""Run the audit against real TypeScript consumers without writing to checkout."""
import argparse
import hashlib
import os
import pathlib
import subprocess
import sys

p = argparse.ArgumentParser()
p.add_argument('--repo', required=True, type=pathlib.Path)
p.add_argument('--sha', required=True)
p.add_argument('--out', required=True, type=pathlib.Path)
p.add_argument('--scratch', required=True, type=pathlib.Path)
p.add_argument('--mode', choices=['both', 'reproduce', 'accept'], default='both')
p.add_argument('--filter', default='')
a = p.parse_args()
repo, out, scratch = a.repo.resolve(), a.out.resolve(), a.scratch.resolve()
actual = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()
expected = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', a.sha], text=True).strip()
if actual != expected:
    sys.exit('Checkout HEAD differs from requested --sha; use an isolated checkout.')
subprocess.run(['git', '-C', str(repo), 'diff', '--quiet', expected, '--'], check=True)
out.parent.mkdir(parents=True, exist_ok=True)
scratch.mkdir(parents=True, exist_ok=True)
loader = repo / 'node_modules/tsx/dist/loader.mjs'
if not loader.exists():
    sys.exit('Missing checkout tsx dependency. Install the pinned package-lock dependencies first.')
env = os.environ.copy()
env.update(WATCHDOG_AUDIT_REPO=str(repo), WATCHDOG_AUDIT_SHA=actual,
           WATCHDOG_AUDIT_SUITE_HASH=hashlib.sha256(pathlib.Path(__file__).with_name('suite.ts').read_bytes()).hexdigest(),
           WATCHDOG_AUDIT_OUT=str(out), WATCHDOG_AUDIT_SCRATCH=str(scratch),
           WATCHDOG_AUDIT_MODE=a.mode, WATCHDOG_AUDIT_FILTER=a.filter,
           DB_PATH=str(scratch / 'incidental-import.sqlite'),
           WATCHDOG_DIAGNOSTICS_DIR=str(scratch / 'diagnostics'))
cmd = ['node', '--import', str(loader), str(pathlib.Path(__file__).with_name('suite.ts'))]
sys.exit(subprocess.run(cmd, cwd=repo, env=env).returncode)
