#!/usr/bin/env python3
"""Replay the preserved scope probes; requires the frozen baseline static build.

The first negative deliberately links old RuntimeProfile callers. The second
uses rebuilt actual fs/log callers but frozen19 embedding with data22. Neither
is a full-tree gate. --source-repo should be the matching W11 data22 snapshot.
"""
import argparse
import hashlib
import json
from pathlib import Path
import resource
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-repo', type=Path, required=True)
    parser.add_argument('--before-build', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    root = args.source_repo.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    snapshot = Path(__file__).resolve().parent / 'snapshot'
    stage = out / 'sources'
    shutil.copytree(snapshot, stage, dirs_exist_ok=True)
    overlay = stage / 'include/loom'
    overlay.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(stage / 'runtime_profile.h', overlay / 'runtime_profile.h')
    include = ['-I' + str(stage / 'include'), '-I' + str(root / 'loom/include'),
               '-I' + str(root / 'loom/src'), '-I' + str(root / 'loom/tests'),
               '-isystem', str(root / 'loom/third_party/nlohmann'),
               '-isystem', str(root / 'loom/third_party/doctest')]
    flags = ['-std=c++20', '-g0', '-fPIC', '-fvisibility=hidden', '-fvisibility-inlines-hidden',
             '-Wall', '-Wextra', '-Wpedantic', '-Wshadow', '-Wnon-virtual-dtor', '-Wold-style-cast',
             '-Wcast-align', '-Woverloaded-virtual', '-Wnull-dereference', '-Wimplicit-fallthrough',
             '-Wno-unused-parameter', '-Werror']
    commands = []
    def checked(command):
        commands.append(command)
        subprocess.run(command, check=True)
    for source in ['runner.cpp', 'runtime_profile.cpp', 'test_runtime_profile.cpp', 'fs.cpp', 'log.cpp', 'native_number_probe.cc']:
        defines = ['-DLOOM_TEST_FIXTURES="' + str(root / 'loom/tests/fixtures') + '"'] if source == 'test_runtime_profile.cpp' else []
        checked(['c++', *flags, *include, *defines, '-c', str(stage / source), '-o', str(out / (Path(source).stem + '.o'))])
    archive = args.before_build.resolve() / 'libloom_core.a'
    dependencies = [str(archive), '-lssl', '-lcrypto', '-lpthread', '-ldl']
    common = [str(out / (name + '.o')) for name in ('runner', 'test_runtime_profile', 'runtime_profile')]
    for name, extra in [('native-initial-abi-negative', []), ('native-19-vs-22-negative', ['fs', 'log'])]:
        checked(['c++', *common, *[str(out / (n + '.o')) for n in extra], *dependencies, '-o', str(out / name)])
    checked(['c++', str(out / 'native_number_probe.o'), *dependencies, '-o', str(out / 'native-number-parser')])
    results = {}
    for name in ['native-initial-abi-negative', 'native-19-vs-22-negative', 'native-number-parser']:
        command = [str(out / name)] + ([] if name == 'native-number-parser' else ['--no-colors'])
        commands.append(command)
        process = subprocess.run(command, capture_output=True, check=False)
        (out / (name + '.stdout')).write_bytes(process.stdout)
        (out / (name + '.stderr')).write_bytes(process.stderr)
        results[name] = {'exit': process.returncode, 'stdout_sha256': hashlib.sha256(process.stdout).hexdigest(),
                         'stderr_sha256': hashlib.sha256(process.stderr).hexdigest()}
    # The matching22 snapshot is retained separately, preserving both prior
    # negative inputs. Recompile only its profile TU with the same new ABI.
    shutil.copyfile(stage / 'runtime_profiles_embedded_22.inc', stage / 'runtime_profiles_embedded.inc')
    checked(['c++', *flags, *include, '-c', str(stage / 'runtime_profile.cpp'), '-o', str(out / 'runtime_profile-22.o')])
    checked(['c++', str(out / 'runner.o'), str(out / 'test_runtime_profile.o'), str(out / 'runtime_profile-22.o'),
             str(out / 'fs.o'), str(out / 'log.o'), *dependencies, '-o', str(out / 'native-22-positive')])
    command = [str(out / 'native-22-positive'), '--no-colors']
    commands.append(command)
    process = subprocess.run(command, capture_output=True, check=False)
    (out / 'native-22-positive.stdout').write_bytes(process.stdout)
    (out / 'native-22-positive.stderr').write_bytes(process.stderr)
    results['native-22-positive'] = {'exit': process.returncode,
                                   'stdout_sha256': hashlib.sha256(process.stdout).hexdigest(),
                                   'stderr_sha256': hashlib.sha256(process.stderr).hexdigest()}
    (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
