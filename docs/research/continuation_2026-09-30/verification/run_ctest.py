from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
mode = sys.argv[1]
source = ROOT / 'baseline-source' if mode == 'baseline' else ROOT.parent / 'chatadhd'
build = ROOT / ('baseline-build' if mode == 'baseline' else 'native-dev')
ctest = ROOT / 'deps/cmake/data/bin/ctest'
env = dict(os.environ,
           PYTHONDONTWRITEBYTECODE='1',
           PYTHONUSERBASE=str(ROOT / 'python-userbase'),
           PYTHONPATH=str(source) + ':' + str(ROOT / 'deps'),
           TMPDIR='/var/tmp')
command = [str(ctest), '--test-dir', str(build), '-j4', '--output-on-failure',
           '--output-junit', str(ROOT / f'{mode}-ctest.xml')]
metadata = {
    'command': command,
    'source': str(source),
    'source_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip(),
    'source_tree': subprocess.check_output(['git', 'rev-parse', 'HEAD^{tree}'], cwd=source, text=True).strip(),
    'dirty_files': subprocess.check_output(['git', 'status', '--porcelain', '--', 'loom'], cwd=source, text=True).splitlines(),
    'sqlite': 'vendored 3.47.2', 'preset': 'dev', 'server': True,
    'cmake_version': '4.4.3', 'compiler': 'GNU 13.3.0',
    'environment': {k: env[k] for k in ['PYTHONDONTWRITEBYTECODE', 'PYTHONUSERBASE', 'PYTHONPATH', 'TMPDIR']},
    'binary_sha256': {},
    'native_sources_sha256': {},
}
for directory in ['loom/include', 'loom/src', 'loom/tests', 'loom/cli', 'loom/server']:
    for path in sorted((source / directory).rglob('*')):
        if path.suffix in ('.cpp', '.h', '.c') and path.is_file() and 'fixtures' not in path.parts:
            metadata['native_sources_sha256'][str(path.relative_to(source))] = hashlib.sha256(path.read_bytes()).hexdigest()
for name in ['loom_tests', 'loom_compat_tool', 'loom_candidate_graph_native_tool', 'libloom.so.0.1.0', 'cli/loom', 'server/loom-server']:
    path = build / name
    if path.exists():
        metadata['binary_sha256'][name] = hashlib.sha256(path.read_bytes()).hexdigest()
(ROOT / f'{mode}-ctest-environment.json').write_text(json.dumps(metadata, indent=2) + '\n')
start = time.monotonic()
with (ROOT / f'{mode}-ctest.log').open('w') as log:
    result = subprocess.run(command, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT)
summary = {'exit_code': result.returncode, 'seconds': time.monotonic() - start}
xml = ROOT / f'{mode}-ctest.xml'
if xml.exists():
    cases = ET.parse(xml).getroot().findall('.//testcase')
    summary.update(test_entries=len(cases),
                   passed=sum(c.find('failure') is None and c.find('skipped') is None for c in cases),
                   failed=[c.attrib['name'] for c in cases if c.find('failure') is not None],
                   skipped=[c.attrib['name'] for c in cases if c.find('skipped') is not None])
(ROOT / f'{mode}-ctest-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
sys.exit(result.returncode)
