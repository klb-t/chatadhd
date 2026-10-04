#!/usr/bin/env python3
"""Offline generator guards in copied temporary layouts; production data stay untouched."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[4]
LOOM = REPO / 'loom'
GENERATOR = LOOM / 'src/extract/gen_prompt_methods.py'
SOURCE = LOOM / 'data/prompts'
INCLUDE = LOOM / 'src/extract/prompt_method_data.inc'

def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence-dir', type=Path)
    args = parser.parse_args()
    if args.evidence_dir:
        args.evidence_dir.mkdir(parents=True, exist_ok=True)
        if any(args.evidence_dir.iterdir()):
            parser.error('--evidence-dir must be empty; preserve earlier evidence')
    protected = [INCLUDE, SOURCE / 'analysis_methods.pack', *sorted(SOURCE.glob('*.recipe')),
                 LOOM / 'src/extract/prompt_method_graph.cpp', LOOM / 'src/extract/prompt_method_graph.h']
    before = {str(path.relative_to(REPO)): sha(path) for path in protected}
    sources = {str(path.relative_to(REPO)): sha(path) for path in [GENERATOR, Path(__file__).resolve(), *protected]}
    cases = []
    logs = []
    minimal_pack = '{"schema":"loom.analysis_methods/1"}\n'
    specifications = [
        ('pack_nan', '{"schema":"loom.analysis_methods/1","value":NaN}\n', {}, 'nonfinite JSON constant: NaN'),
        ('recipe_infinity', minimal_pack, {'a.recipe': '{"id":"tiny.recipe","value":Infinity}\n'}, 'nonfinite JSON constant: Infinity'),
        ('recipe_missing_id', minimal_pack, {'a.recipe': '{"schema":"loom.analysis_recipe/1"}\n'}, 'missing or duplicate recipe id'),
        ('recipe_duplicate_id', minimal_pack, {'a.recipe': '{"id":"tiny.recipe"}\n', 'b.recipe': '{"id":"tiny.recipe"}\n'}, 'missing or duplicate recipe id'),
        ('literal_delimiter_collision', json.dumps({'schema': 'loom.analysis_methods/1', 'value': ')LOOM_METHOD_DATA'}) + '\n', {}, 'raw-literal delimiter collision'),
        ('valid_data_matches_frozen_include', None, None, None),
    ]
    with tempfile.TemporaryDirectory(prefix='loom-method-generator-guards-') as scratch:
        for name, pack, recipes, expected_error in specifications:
            root = Path(scratch) / name / 'loom'
            source = root / 'data/prompts'
            target = root / 'src/extract'
            source.mkdir(parents=True)
            target.mkdir(parents=True)
            generator = target / GENERATOR.name
            include = target / INCLUDE.name
            shutil.copyfile(GENERATOR, generator)
            if pack is None:
                shutil.copyfile(SOURCE / 'analysis_methods.pack', source / 'analysis_methods.pack')
                for path in sorted(SOURCE.glob('*.recipe')):
                    shutil.copyfile(path, source / path.name)
                shutil.copyfile(INCLUDE, include)
                expected_bytes = INCLUDE.read_bytes()
                command = [sys.executable, str(generator), '--check']
            else:
                (source / 'analysis_methods.pack').write_text(pack, encoding='utf-8')
                for filename, raw in recipes.items():
                    (source / filename).write_text(raw, encoding='utf-8')
                expected_bytes = b'KEEP-FIRST-GENERATED-OUTPUT\n'
                include.write_bytes(expected_bytes)
                command = [sys.executable, str(generator)]
            result = subprocess.run(command, capture_output=True, text=True, check=False)
            untouched = include.read_bytes() == expected_bytes and not include.with_name(include.name + '.tmp').exists()
            passed = (result.returncode == 0 if expected_error is None else result.returncode != 0 and expected_error in result.stderr) and untouched
            case = {'id': name, 'passed': passed, 'exit_code': result.returncode,
                    'expected_error': expected_error, 'target_unchanged': untouched,
                    'stdout': result.stdout, 'stderr': result.stderr,
                    'inputs': {path.name: {'sha256': sha(path), 'bytes': path.stat().st_size}
                               for path in sorted(source.iterdir())},
                    'command': [sys.executable, '<copied-temp-generator>', *command[2:]]}
            cases.append(case)
            logs.append(f'CASE {name}\nCOMMAND {case["command"]!r}\nEXIT {result.returncode}\nSTDOUT\n{result.stdout}\nSTDERR\n{result.stderr}\n')
    after = {str(path.relative_to(REPO)): sha(path) for path in protected}
    unchanged = before == after
    passed = sum(case['passed'] for case in cases)
    receipt = {'schema': 'loom.prompt_method_generator_guards/1', 'cases': len(cases), 'passed': passed,
               'production_files_unchanged': unchanged, 'paid_calls': 0, 'provider_calls': 0,
               'source_sha256': sources, 'checks': cases,
               'scope': 'strict JSON and generation guards in independent copied temporary layouts'}
    if args.evidence_dir:
        (args.evidence_dir / 'receipt.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2, allow_nan=False) + '\n', encoding='utf-8')
        (args.evidence_dir / 'raw.log').write_text('\n'.join(logs), encoding='utf-8')
    print(json.dumps({'schema': receipt['schema'], 'cases': len(cases), 'passed': passed,
                      'production_files_unchanged': unchanged, 'paid_calls': 0, 'provider_calls': 0}))
    return 0 if passed == len(cases) and unchanged else 1

if __name__ == '__main__':
    raise SystemExit(main())
