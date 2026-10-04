"""Build and verify the current native tree, recording honest test denominators.

Run from a fresh clone with GCC/Clang, CMake >=3.24, Python3 and a C++20 toolchain.
Build/output directories must be fresh and outside the repository; their logs
are evidence of this invocation, not replacements for the saved measured runs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

FOCUSED = ('test_db_annotations', 'test_import_resume', 'test_import_audit', 'test_import_usage')


def repo_from_location() -> Path:
    for parent in Path(__file__).resolve().parents:
        if (parent / 'loom/CMakeLists.txt').is_file():
            return parent
    raise ValueError('cannot locate repository; supply --repo PATH')


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def discover_tool(requested: str | None, name: str, sibling: Path | None = None) -> str:
    if requested:
        result = shutil.which(requested)
    elif sibling and sibling.is_file() and os.access(sibling, os.X_OK):
        result = str(sibling)
    else:
        result = shutil.which(name)
    if not result:
        raise ValueError(f'{name} not found; install it or supply --{name} PATH')
    return result


def git_text(repo: Path, *args: str) -> str:
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()


def source_manifest(repo: Path) -> dict:
    # Hash the checked-out bytes, including untracked nonignored implementation
    # files. Build/evidence outputs are external; gitignored artifacts are omitted.
    names = subprocess.check_output(['git', '-C', str(repo), 'ls-files', '-z',
        '--cached', '--others', '--exclude-standard', '--', 'loom', 'core', 'engine',
        'docs/contracts'])
    files = {}
    for name in sorted(set(os.fsdecode(part) for part in names.split(b'\0') if part)):
        path = repo / name
        files[name] = sha256(path) if path.is_file() else None
    encoded = json.dumps(files, sort_keys=True, separators=(',', ':')).encode()
    return {'scope': 'git tracked/nonignored files under loom, core, engine and docs/contracts',
            'sha256': hashlib.sha256(encoded).hexdigest(), 'files': files}


def doctest_counts(text: str) -> dict:
    match = re.search(r'\[doctest\]\s+test cases:\s*(\d+)\s*\|\s*(\d+)\s+passed\s*\|\s*(\d+)\s+failed', text)
    if not match or int(match[1]) == 0:
        raise ValueError('focused group did not report any executed doctest cases')
    counts = {'cases': int(match[1]), 'passed': int(match[2]), 'failed': int(match[3])}
    assertions = re.search(r'\[doctest\]\s+assertions:\s*(\d+)\s*\|\s*(\d+)\s+passed\s*\|\s*(\d+)\s+failed', text)
    if assertions:
        counts['assertions'] = {'total': int(assertions[1]), 'passed': int(assertions[2]), 'failed': int(assertions[3])}
    if counts['failed'] or counts['passed'] != counts['cases']:
        raise ValueError(f'focused doctest failures: {counts}')
    return counts


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--out-dir', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2, help='build/CTest concurrency preset; any positive value')
    parser.add_argument('--cmake', help='cmake executable; otherwise discovered on PATH')
    parser.add_argument('--ctest', help='ctest executable; otherwise sibling of cmake or discovered on PATH')
    parser.add_argument('--generator', help='CMake generator; default Ninja if available, otherwise CMake default')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    repo = args.repo.resolve() if args.repo else repo_from_location()
    if not (repo / 'loom/CMakeLists.txt').is_file():
        parser.error('--repo must contain loom/CMakeLists.txt')
    build, out = args.build_dir.resolve(), args.out_dir.resolve()
    for name, path in (('build-dir', build), ('out-dir', out)):
        if path.is_relative_to(repo):
            parser.error(f'--{name} must be outside the repository')
        if path.exists() and not path.is_dir():
            parser.error(f'--{name} must be a directory')
        if path.exists() and any(path.iterdir()):
            parser.error(f'--{name} must be a fresh empty directory to preserve prior evidence')
    if build == out or build.is_relative_to(out) or out.is_relative_to(build):
        parser.error('build and output must be separate directories')
    try:
        cmake = discover_tool(args.cmake, 'cmake')
        ctest = discover_tool(args.ctest, 'ctest', Path(cmake).with_name('ctest'))
    except ValueError as error:
        parser.error(str(error))
    build.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    removed = [name for name in ('PYTHONPATH', 'TMPDIR') if name in env]
    for name in ('PYTHONPATH', 'TMPDIR'):
        env.pop(name, None)
    receipt = {'schema': 'loom.native_verification/1', 'status': 'running',
        'source_commit': git_text(repo, 'rev-parse', 'HEAD'),
        'source_dirty': bool(git_text(repo, 'status', '--porcelain', '--', 'loom', 'core', 'engine', 'docs/contracts')),
        'profile': {'build_type': 'Release', 'c_flags_release': '-O0 -DNDEBUG',
            'cxx_flags_release': '-O0 -DNDEBUG', 'werror': True, 'vendored_sqlite': True,
            'tests': True, 'cli': True, 'server': True, 'shared': True, 'jobs': args.jobs},
        'removed_external_environment_keys': removed, 'commands': [], 'focused': {},
        'full_ctest': None, 'source_stable': None, 'failure': None}

    def save() -> None:
        (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')

    def run(name: str, command: list[str]) -> subprocess.CompletedProcess:
        started = time.perf_counter()
        result = subprocess.run(command, cwd=repo, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log = out / (name + '.log')
        log.write_text(result.stdout)
        receipt['commands'].append({'name': name, 'argv': command, 'exit_code': result.returncode,
            'seconds': time.perf_counter() - started, 'log': log.name, 'log_sha256': sha256(log)})
        save()
        print(f'{name}: exit {result.returncode}; {log}', flush=True)
        return result

    def must_run(name: str, command: list[str]) -> str:
        result = run(name, command)
        if result.returncode:
            raise ValueError(f'{name} failed with exit {result.returncode}; see {name}.log')
        return result.stdout

    try:
        manifest = source_manifest(repo)
        (out / 'source-before.json').write_text(json.dumps(manifest, indent=2) + '\n')
        receipt['source_sha256'] = manifest['sha256']
        configure = [cmake, '-S', str(repo / 'loom'), '-B', str(build),
            '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_FLAGS_RELEASE=-O0 -DNDEBUG',
            '-DCMAKE_CXX_FLAGS_RELEASE=-O0 -DNDEBUG', '-DLOOM_WERROR=ON',
            '-DLOOM_USE_SYSTEM_SQLITE=OFF', '-DLOOM_BUILD_TESTS=ON',
            '-DLOOM_BUILD_CLI=ON', '-DLOOM_BUILD_SERVER=ON', '-DLOOM_SHARED=ON',
            '-DPython3_EXECUTABLE=' + sys.executable]
        generator = args.generator or ('Ninja' if shutil.which('ninja') else None)
        if generator:
            configure += ['-G', generator]
        must_run('configure', configure)
        must_run('build', [cmake, '--build', str(build), '--parallel', str(args.jobs)])
        receipt['cmake_cache_sha256'] = sha256(build / 'CMakeCache.txt')
        required = [build / 'loom_tests', build / 'cli/loom', build / 'server/loom-server']
        shared = [path for name in ('libloom.so', 'libloom.dylib', 'loom.dll')
                  if (path := build / name).is_file()]
        if not shared or any(not path.is_file() for path in required):
            raise ValueError('required tests/CLI/server/shared build artifacts are missing')
        binaries = required + shared + [path for name in ('loom_compat_tool', 'loom_candidate_graph_native_tool')
            if (path := build / name).is_file()]
        receipt['binary_sha256'] = {str(path.relative_to(build)): sha256(path) for path in binaries}
        listed = json.loads(must_run('ctest-registration', [ctest, '--test-dir', str(build), '--show-only=json-v1']))
        registered = listed.get('tests', [])
        if not registered:
            raise ValueError('CTest registered zero tests')
        names = {item['name'] for item in registered}
        for group in FOCUSED:
            if 'unit.' + group not in names:
                raise ValueError(f'required focused group is not registered: {group}')
        receipt['registered_ctest_entries'] = len(registered)
        full = run('ctest-full', [ctest, '--test-dir', str(build), '--output-on-failure',
            '--no-tests=error', '--parallel', str(args.jobs)])
        summary = re.search(r'(\d+)% tests passed,\s*(\d+) tests failed out of (\d+)', full.stdout)
        if not summary:
            raise ValueError('full CTest did not report an executed-test denominator')
        failed, executed = int(summary[2]), int(summary[3])
        receipt['full_ctest'] = {'executed': executed, 'failed': failed,
            'passed': executed - failed, 'exit_code': full.returncode}
        if not executed or executed != len(registered):
            raise ValueError('full CTest execution count does not match its registered tests')
        # These are existing native tests: source filtering cannot create new
        # cases. Validate actual doctest counts so an empty filter never passes.
        focused_failures = []
        for group in FOCUSED:
            result = run('focused-' + group, [str(build / 'loom_tests'),
                '--source-file=*/' + group + '.cpp', '--no-intro=true'])
            try:
                receipt['focused'][group] = doctest_counts(result.stdout)
                if result.returncode:
                    raise ValueError(f'{group} exited {result.returncode}')
            except ValueError as error:
                focused_failures.append(str(error))
                receipt['focused'][group] = {'verified': False, 'error': str(error), 'exit_code': result.returncode}
        after = source_manifest(repo)
        (out / 'source-after.json').write_text(json.dumps(after, indent=2) + '\n')
        receipt['source_stable'] = after['sha256'] == manifest['sha256']
        if not receipt['source_stable']:
            raise ValueError('source changed during verification; receipt is not a stable-source proof')
        if full.returncode or failed or focused_failures:
            reasons = ([f'full CTest failed: exit {full.returncode}, failed {failed}']
                       if full.returncode or failed else [])
            raise ValueError('; '.join([*reasons, *focused_failures]))
        receipt['focused_cases'] = sum(item['cases'] for item in receipt['focused'].values())
        receipt['status'] = 'passed'
    except KeyboardInterrupt:
        receipt['status'] = 'interrupted'
        receipt['failure'] = 'verification interrupted; no completed gate is claimed'
        print(receipt['failure'], file=sys.stderr)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        receipt['status'] = 'failed'
        receipt['failure'] = str(error)
        print(str(error), file=sys.stderr)
    finally:
        save()
    return 0 if receipt['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
