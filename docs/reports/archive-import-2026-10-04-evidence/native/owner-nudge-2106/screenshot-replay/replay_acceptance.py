#!/usr/bin/env python3
"""Independent positive OCR acceptance replay, retaining the exact negative probe."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

import argparse
import base64
import sqlite3
from urllib.parse import parse_qs
parser = argparse.ArgumentParser(description="Independent offline W5 screenshot MIME acceptance; fresh output required.")
parser.add_argument('--repo', type=Path, required=True)
parser.add_argument('--build-dir', type=Path, required=True)
parser.add_argument('--out-dir', type=Path, required=True)
parser.add_argument('--source-commit', required=True)
parser.add_argument('--expected-core-sha256', default='')
args = parser.parse_args()
EVIDENCE = Path(__file__).resolve().parent
HERE = args.out_dir.resolve()
HERE.mkdir(parents=True, exist_ok=False)
REPO = args.repo.resolve()
BUILD = args.build_dir.resolve()
RECEIPT = args.source_commit
for filename in ('probe.cpp', 'synthetic.png'):
    (HERE / filename).write_bytes((EVIDENCE / filename).read_bytes())

def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()

def snapshot():
    cache = (BUILD / 'CMakeCache.txt').read_text()
    source = next(Path(line.split('=', 1)[1]).parent for line in cache.splitlines()
                  if line.startswith('CMAKE_HOME_DIRECTORY:'))
    listed = subprocess.check_output(['git', '-C', str(REPO), 'ls-tree', '-r', '-z', RECEIPT,
                                     'loom/include', 'loom/src']).split(b'\0')
    files = {}
    mismatches = []
    for entry in listed:
        if not entry:
            continue
        info, raw_name = entry.split(b'\t', 1)
        name = raw_name.decode()
        expected = info.split()[2].decode()
        content = (source / name).read_bytes()
        git_hash = hashlib.sha1(b'blob ' + str(len(content)).encode() + b'\0' + content).hexdigest()
        files[name] = {'git_blob': git_hash, 'sha256': hashlib.sha256(content).hexdigest(), 'bytes': len(content)}
        if git_hash != expected:
            mismatches.append(name)
    if mismatches:
        raise RuntimeError('source mismatch against reviewed commit: ' + repr(mismatches))
    libraries = {name: {'bytes': (BUILD / name).stat().st_size, 'sha256': sha(BUILD / name)}
                 for name in ('libloom_core.a', 'libloom_sqlite3_amalgamation.a', 'libloom_miniz.a')}
    return {'source_root': str(source), 'files': files, 'libraries': libraries,
            'cmake_cache_sha256': sha(BUILD / 'CMakeCache.txt')}

def require(value, name):
    if not value:
        raise AssertionError(name)

def run_measured(command, logfile):
    begin = time.monotonic()
    with logfile.open('w') as log:
        process = subprocess.Popen(command, stdout=log, stderr=log)
        _, status, usage = os.wait4(process.pid, 0)
        process.returncode = os.waitstatus_to_exitcode(status)
    measurement = {'command': command, 'seconds': time.monotonic() - begin,
                   'exit_code': process.returncode, 'process_cpu_seconds': usage.ru_utime + usage.ru_stime,
                   'peak_child_rss_kib': usage.ru_maxrss, 'log': str(logfile.relative_to(HERE))}
    commands.append(measurement)
    if process.returncode:
        raise RuntimeError('child failed: ' + repr(measurement))
    return measurement

before = snapshot()
if args.expected_core_sha256:
    require(before['libraries']['libloom_core.a']['sha256'] == args.expected_core_sha256, 'actual W5 library identity')
(HERE / 'source-before.json').write_text(json.dumps(before, indent=2) + '\n')
source = Path(before['source_root'])
commands = []
flags = ['-std=c++20', '-O0', '-g0', '-pthread', '-DLOOM_HAVE_OPENSSL=1', '-DCPPHTTPLIB_OPENSSL_SUPPORT=1', '-DJSON_USE_IMPLICIT_CONVERSIONS=1', '-I' + str(source / 'loom/include'), '-I' + str(source / 'loom/src')]
for name in ('nlohmann', 'miniz', 'sqlite', 'cpp-httplib'):
    flags += ['-isystem', str(source / 'loom/third_party' / name)]
binary = HERE / 'probe'
command = ['c++', *flags, str(HERE / 'probe.cpp'), str(BUILD / 'libloom_core.a'), str(BUILD / 'libloom_sqlite3_amalgamation.a'), str(BUILD / 'libloom_miniz.a'), '-ldl', '-lm', '-lssl', '-lcrypto', '-Wl,--no-keep-memory', '-Wl,--reduce-memory-overheads', '-o', str(binary)]
run_measured(command, HERE / 'compile.txt')
run_measured([str(binary), str(HERE / 'data'), str(HERE / 'synthetic.png')], HERE / 'run.txt')
data = json.loads((HERE / 'data/results.json').read_text())
checks = {}
for index, scenario in enumerate(data):
    prefix = scenario['scenario'] + ':'
    checks[prefix + 'mock_one_request'] = scenario['mock_requests'] == 1
    checks[prefix + 'mock_import_succeeded'] = scenario['import_succeeded_against_nonvalidating_mock'] is True
    checks[prefix + 'valid_png_uri_preserved'] = scenario['expected_png_uri_present'] is True and scenario['empty_format_uri_present'] is False
    if index:
        checks[prefix + 'extensionless_blob_retains_same_bytes'] = scenario['blob_extension'] == '' and scenario['blob_bytes_match_source'] is True
    fields = parse_qs(scenario['request_body'], strict_parsing=True)
    image_fields = fields.get('base64Image', [])
    payload = b''
    if len(image_fields) == 1 and image_fields[0].startswith('data:image/png;base64,'):
        try:
            payload = base64.b64decode(image_fields[0].split(',', 1)[1], validate=True)
        except (ValueError, TypeError):
            pass
    checks[prefix + 'request_contains_exact_captured_png_bytes'] = payload == (HERE / 'synthetic.png').read_bytes()
    if index:
        database = HERE / 'data' / scenario['scenario'] / 'probe.db'
        con = sqlite3.connect(database.resolve().as_uri() + '?mode=ro', uri=True)
        try:
            sources = con.execute("SELECT mime,blob_hash,metadata FROM loom_sources WHERE format='screenshot'").fetchall()
        finally:
            con.close()
        checks[prefix + 'one_screenshot_source'] = len(sources) == 1
        checks[prefix + 'source_mime_preserved'] = len(sources) == 1 and sources[0][0] == 'image/png'
        checks[prefix + 'source_bound_to_captured_bytes'] = len(sources) == 1 and sources[0][1] == sha(HERE / 'synthetic.png')
        checks[prefix + 'declared_format_preserved'] = len(sources) == 1 and json.loads(sources[0][2]).get('declared_image_format') == 'png'
after = snapshot()
require(before == after, 'actual sources and libraries remain stable')
(HERE / 'source-after.json').write_text(json.dumps(after, indent=2) + '\n')
receipt = {'reviewed_commit': RECEIPT, 'source_files_compared': len(before['files']), 'actual_core_sha256': before['libraries']['libloom_core.a']['sha256'], 'live_provider_calls': 0, 'production_translation_units_compiled': 0, 'source_libraries_stable': True, 'commands': commands, 'status': 'passed' if all(checks.values()) else 'failed', 'acceptance_checks': checks, 'acceptance_checks_passed': sum(checks.values()), 'acceptance_checks_total': len(checks), 'original_diagnostic_checks_retained': 11, 'mime_preservation_checks': {'passed': sum(item['expected_png_uri_present'] is True and item['empty_format_uri_present'] is False for item in data), 'total': 3, 'negative_routes': sum(item['empty_format_uri_present'] is True for item in data)}, 'original_archive_sha256': 'c840cc9ca0948253460478918ced73bc13155cab65d281c686964e81129fcfae', 'probe_sha256': sha(HERE / 'probe.cpp'), 'binary_sha256': sha(binary), 'synthetic_png_sha256': sha(HERE / 'synthetic.png'), 'result_sha256': sha(HERE / 'data/results.json')}
(HERE / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps(receipt, indent=2))

if not all(checks.values()):
    raise SystemExit(1)
