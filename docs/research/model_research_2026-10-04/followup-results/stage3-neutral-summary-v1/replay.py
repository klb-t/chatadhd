"""Replay explicit, hash-bound DATA projections; no scoring or selection.

Only caller-listed inputs are read. JSON pointers, comparisons, output templates
and missing-value treatment are supplied by the mapping, not task/model logic.
"""
from __future__ import annotations

import argparse
from decimal import Decimal
import hashlib
import json
from pathlib import Path


OMIT = object()


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(',', ':'), allow_nan=False).encode('utf-8')


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def load(raw):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('duplicate_json_key:' + key)
            result[key] = value
        return result

    def nonfinite(value):
        raise ValueError('nonfinite_json_number:' + value)

    return json.loads(raw, object_pairs_hook=pairs, parse_constant=nonfinite)


def pointer(value, path):
    if not path:
        return value
    if not path.startswith('/'):
        raise ValueError('invalid_json_pointer')
    for component in path[1:].split('/'):
        component = component.replace('~1', '/').replace('~0', '~')
        value = value[int(component)] if isinstance(value, list) else value[component]
    return value


def derive(mapping, inputs, input_hashes):
    cache, active = {}, set()

    def evaluate(value):
        if isinstance(value, list):
            result = [evaluate(item) for item in value]
            if any(item is OMIT for item in result):
                raise ValueError('omission_only_supported_for_object_fields')
            return result
        if not isinstance(value, dict):
            return value
        if len(value) == 1:
            if '$ref' in value:
                ref = value['$ref']
                return pointer(inputs[ref['input']], ref['pointer'])
            if '$input_sha256' in value:
                return input_hashes[value['$input_sha256']]
            if '$projection' in value:
                name = value['$projection']
                if name in active:
                    raise ValueError('cyclic_projection:' + name)
                if name not in cache:
                    active.add(name)
                    cache[name] = evaluate(mapping['projections'][name])
                    active.remove(name)
                return cache[name]
            if '$sha256_json' in value:
                return digest(canonical(evaluate(value['$sha256_json'])))
            if '$sha256_utf8' in value:
                return digest(evaluate(value['$sha256_utf8']).encode('utf-8'))
            if '$if_nonnull' in value:
                spec = value['$if_nonnull']
                return OMIT if evaluate(spec['test']) is None else evaluate(spec['value'])
            if '$concat' in value:
                return ''.join(evaluate(item) for item in value['$concat'])
            if '$length' in value:
                return len(evaluate(value['$length']))
            if '$any_nonnull' in value:
                return any(evaluate(item) is not None for item in value['$any_nonnull'])
            if '$count_equal' in value:
                spec = value['$count_equal']
                expected = canonical(evaluate(spec['expected']))
                return sum(canonical(evaluate(item)) == expected for item in spec['values'])
            if '$decimal_sum' in value:
                return str(sum((Decimal(str(evaluate(item))) for item in value['$decimal_sum']), Decimal(0)))
        result = {}
        for key, item in value.items():
            resolved = evaluate(item)
            if resolved is not OMIT:
                result[key] = resolved
        return result

    checks = []
    for assertion in mapping['assertions']:
        kind = assertion['kind']
        values = [evaluate(item) for item in assertion['values']]
        if kind == 'equal':
            passed = all(canonical(item) == canonical(values[0]) for item in values[1:])
        elif kind == 'unique':
            passed = len(values) == len({canonical(item) for item in values})
        elif kind == 'set_equal':
            passed = all({canonical(item) for item in values[0]} ==
                         {canonical(item) for item in other} for other in values[1:])
        elif kind == 'decimal_sum_equal':
            passed = sum((Decimal(str(item)) for item in values[0]), Decimal(0)) == Decimal(str(values[1]))
        else:
            raise ValueError('unsupported_assertion:' + kind)
        if not passed:
            raise ValueError('assertion_failed:' + assertion['id'])
        checks.append({'id': assertion['id'], 'kind': kind, 'passed': True})
    return {name: evaluate(template) for name, template in mapping['outputs'].items()}, checks


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mapping', required=True, type=Path)
    parser.add_argument('--repository-root', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    args = parser.parse_args(argv)
    raw_mapping = args.mapping.read_bytes()
    mapping = load(raw_mapping)
    inputs, hashes = {}, {}
    for name, binding in mapping['inputs'].items():
        path = args.repository_root / binding['path']
        raw = path.read_bytes()
        hashes[name] = digest(raw)
        if hashes[name] != binding['sha256']:
            raise ValueError('input_hash_mismatch:' + name)
        inputs[name] = load(raw)
    outputs, checks = derive(mapping, inputs, hashes)
    encoded = {name: canonical(value) + b'\n' for name, value in outputs.items()}
    receipt = {
        'schema': 'loom.data_projection_derivation_receipt/1',
        'mapping_sha256': digest(raw_mapping),
        'replay_source_sha256': digest(Path(__file__).read_bytes()),
        'input_bindings': mapping['inputs'],
        'output_sha256': {name: digest(raw) for name, raw in encoded.items()},
        'assertions': checks,
        'caller_annotations': mapping['annotations'],
        'source_scoring_performed': False, 'selection_performed': False,
        'new_model_calls': 0, 'network_calls': 0,
    }
    encoded['derivation-receipt.json'] = canonical(receipt) + b'\n'
    args.output_dir.mkdir(parents=True, exist_ok=True)
    # Validate and serialize everything before exclusively creating outputs.
    if any((args.output_dir / name).exists() for name in encoded):
        raise FileExistsError('output_already_exists')
    for name, raw in encoded.items():
        with (args.output_dir / name).open('xb') as handle:
            handle.write(raw)
    return receipt


if __name__ == '__main__':
    main()
