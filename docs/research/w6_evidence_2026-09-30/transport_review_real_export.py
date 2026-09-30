#!/usr/bin/env python3
"""Synthetic controls for the independent export-audit helpers; no owner data."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile

target = Path(sys.argv[1])
spec = importlib.util.spec_from_file_location('audit_review', target)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
rows = []


def check(name, value):
    rows.append({'check': name, 'pass': bool(value)})


check('bool differs from int', module.atoms(True) != module.atoms(1))
check('int differs from float', module.atoms(1) != module.atoms(1.0))
check('empty dict differs from empty list', module.atoms({}) != module.atoms([]))
check('dictionary insertion order invariant', module.atoms({'a': 1, 'b': 2}) == module.atoms({'b': 2, 'a': 1}))
check('list order significant', module.atoms([1, 2]) != module.atoms([2, 1]))
check('empty children preserved', len(module.atoms({'x': [], 'y': {}, 'z': None})) == 3)
check('escaped slash tilde empty key pointer', module.pointer({'a/b': {'~': {'': [9]}}}, '/a~1b/~0//0') == 9)
check('root pointer', module.pointer({'x': 1}, '') == {'x': 1})
for pointer in ['/-1', '/01', '/+1']:
    try:
        module.pointer([10, 20], pointer)
        strict = False
    except (ValueError, IndexError, KeyError):
        strict = True
    rows.append({'check': 'reject noncanonical array index ' + pointer, 'pass': strict,
                 'classification': 'oracle strictness limitation; not observed production locator failure'})
with tempfile.TemporaryDirectory(prefix='w6-export-review-') as temporary:
    root = Path(temporary)
    source = root / 'source.json'
    source.write_text('[]')
    cli = root / 'dummy-cli'
    cli.write_bytes(b'not executable; safeguard must run before subprocess')
    out = root / 'existing-output'
    out.mkdir()
    sentinel = out / 'real-export-before.json'
    sentinel.write_bytes(b'preserved first evidence')
    try:
        module.audit(cli, source, root / 'runtime', out)
        refused = False
    except FileExistsError:
        refused = True
    check('existing output refused before runtime creation', refused and not (root / 'runtime').exists())
    check('existing evidence bytes unchanged', sentinel.read_bytes() == b'preserved first evidence')
print(json.dumps({'review_target': str(target), 'controls': rows}, indent=2))
raise SystemExit(0 if all(row['pass'] for row in rows) else 1)
