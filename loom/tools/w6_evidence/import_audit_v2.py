#!/usr/bin/env python3
"""Amended exact structural oracle. Frozen V1 inputs and receipts stay unchanged."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT / 'docs/research/w6_evidence_2026-09-30'
FIX = BASE / 'import_fixtures'
FREEZE = BASE / 'import_v2_freeze.json'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def exact_tree(value):
    """Injective for JSON values: type, keys, arity, array order, scalar value."""
    if isinstance(value, dict):
        return ('object', tuple(sorted((key, exact_tree(v)) for key, v in value.items())))
    if isinstance(value, list):
        return ('array', tuple(exact_tree(v) for v in value))
    return (type(value).__name__, json.dumps(value, ensure_ascii=False, allow_nan=False))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['freeze', 'evaluate'])
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    files = [Path(__file__).resolve(), BASE / 'import_v2_protocol.md', FIX / 'manifest.json']
    if args.action == 'freeze':
        with FREEZE.open('x') as f:
            json.dump({str(p.relative_to(ROOT)): sha(p) for p in files}, f, indent=2)
            f.write('\n')
        print(sha(FREEZE))
        return
    if args.binary is None or args.out is None:
        parser.error('evaluate requires --binary and --out')
    for name, expected in json.loads(FREEZE.read_text()).items():
        if sha(ROOT / name) != expected:
            raise SystemExit('V2 frozen source changed: ' + name)
    spec = importlib.util.spec_from_file_location('frozen_import_v1', ROOT / 'loom/tools/w6_evidence/import_audit.py')
    v1 = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(v1)
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    checks = []
    def check(name, condition, detail):
        checks.append({'check': name, 'pass': bool(condition), 'detail': detail})
    controls = [
        ('extra_field', {'x': 1}, {'x': 1, 'added': None}, False),
        ('missing_field', {'x': 1, 'y': 2}, {'x': 1}, False),
        ('array_vs_numeric_object', ['a'], {'0': 'a'}, False),
        ('nested_array_vs_numeric_object', {'x': ['a']}, {'x': {'0': 'a'}}, False),
        ('array_order', ['a', 'b'], ['b', 'a'], False),
        ('array_duplicate', ['a'], ['a', 'a'], False),
        ('bool_vs_integer', False, 0, False),
        ('integer_vs_float', 1, 1.0, False),
        ('unicode_normalization', 'e\u0301', 'é', False),
        ('empty_container_type', [], {}, False),
        ('object_key_order_irrelevant', {'a': 1, 'b': []}, {'b': [], 'a': 1}, True),
        ('identical_nested', {'a': [None, {}, '🧪']}, {'a': [None, {}, '🧪']}, True)]
    for name, left, right, equal in controls:
        check('oracle:' + name, (exact_tree(left) == exact_tree(right)) == equal, {'expected_equal': equal})
    (out / 'oracle_controls_before_native.json').write_text(json.dumps(checks, indent=2) + '\n')
    if not all(c['pass'] for c in checks):
        raise SystemExit('Oracle controls failed; native run not attempted')
    v1.evaluate(args.binary.resolve(), out / 'native')
    native = json.loads((out / 'native/receipt.json').read_text())
    for obs in native['observations']:
        case = obs['case']
        provider = 'anthropic' if case == 'anthropic' else 'openai'
        source = json.loads((FIX / (provider + '.json')).read_text())[0]
        returned = json.loads((out / ('native/' + case + '.stdout.json')).read_text())
        cex = returned['conversations'][0]['metadata']['export']
        rows = obs['message_rows']
        reconstructed = dict(cex['fields'])
        if provider == 'openai':
            mapping = {r['metadata']['export']['key']: dict(r['metadata']['export']['node'], message=r['metadata']['export']['raw']) for r in rows}
            mapping.update({n['id']: n['node'] for n in cex['null_nodes']})
            reconstructed['mapping'] = mapping
        else:
            reconstructed['chat_messages'] = [r['metadata']['export']['raw'] for r in rows]
        check(case + ':exact_structural_reconstruction', exact_tree(source) == exact_tree(reconstructed),
              {'expected': source, 'actual': reconstructed})
        source_messages = ({k: n['message'] for k, n in source['mapping'].items() if n['message'] is not None} if provider == 'openai'
                           else {m['uuid']: m for m in source['chat_messages']})
        for row in rows:
            ex = row['metadata']['export']
            check(case + ':exact_raw_message:' + ex['key'], exact_tree(source_messages[ex['key']]) == exact_tree(ex['raw']), ex['key'])
    result = {'schema': 'w6.import_exact_structure.v2', 'freeze_sha256': sha(FREEZE), 'checks': checks,
              'passed': sum(c['pass'] for c in checks), 'total': len(checks),
              'binary_sha256': native['binary_sha256'], 'baseline_requested': native['baseline_requested'],
              'limitations': ['Same synthetic corpus as V1; amendment after cross-review, not blind',
                              'No aggregate claim combining V1/V2 checks', 'Exact JSON value fidelity, not lexical number spelling']}
    (out / 'receipt.json').write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n')
    print(json.dumps({k: result[k] for k in ['passed', 'total', 'freeze_sha256']}))
    print(json.dumps([c['check'] for c in checks if not c['pass']]))

if __name__ == '__main__':
    main()
