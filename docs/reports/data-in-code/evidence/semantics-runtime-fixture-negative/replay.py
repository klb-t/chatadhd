#!/usr/bin/env python3
"""Reproduce the original fixture failure without modifying tracked source."""
import argparse
import hashlib
import json
import pathlib
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--source-root', type=pathlib.Path, required=True)
parser.add_argument('--build', type=pathlib.Path, required=True)
parser.add_argument('--output', type=pathlib.Path, required=True)
args = parser.parse_args()
source_root = args.source_root.resolve()
build = args.build.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
evidence = pathlib.Path(__file__).resolve().parent
original = evidence / 'test_runtime_recipe_consumers.original.cpp'
exe = output / 'original-runtime-fixture'
loom = source_root / 'loom'
libraries = [build / name for name in ('libloom_core.a', 'libloom_sqlite3_amalgamation.a', 'libloom_miniz.a')]
for file in (original, loom / 'tests/main.cpp', *libraries):
    if not file.is_file():
        parser.error('missing required current-build/source file: ' + str(file))
command = ['c++', '-std=c++20', '-O0', '-g0', '-DJSON_USE_IMPLICIT_CONVERSIONS=1',
           '-DLOOM_VENDORED_SQLITE=1']
command += ['-I' + str(loom / path) for path in ('include', 'src', 'tests',
             'third_party/nlohmann', 'third_party/doctest', 'third_party/sqlite')]
command += [str(loom / 'tests/main.cpp'), str(original)]
command += [str(file) for file in libraries]
command += ['-lssl', '-lcrypto', '-lpthread', '-ldl', '-o', str(exe)]
compiled = subprocess.run(command, cwd=output, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(output / 'compile.log').write_text(compiled.stdout)
receipt = {'compile_command': command, 'compile_exit': compiled.returncode,
           'original_source_sha256': hashlib.sha256(original.read_bytes()).hexdigest(),
           'current_library_sha256': {file.name: hashlib.sha256(file.read_bytes()).hexdigest() for file in libraries}}
if compiled.returncode:
    (output / 'replay-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    raise SystemExit(compiled.returncode)
run_command = [str(exe), '--test-suite=runtime_recipe_consumers']
executed = subprocess.run(run_command, cwd=output, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(output / 'negative.log').write_text(executed.stdout)
receipt.update(run_command=run_command, runtime_exit=executed.returncode,
               expected='one recipe case fails three product-store assertions; the old fixture reuses one deterministic run')
(output / 'replay-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
print('Original fixture exit:', executed.returncode)
print('Full output:', output / 'negative.log')
# An actual nonzero test result is intentionally retained, not converted to green.
raise SystemExit(executed.returncode)
