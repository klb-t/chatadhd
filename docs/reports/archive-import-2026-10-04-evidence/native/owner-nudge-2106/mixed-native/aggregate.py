#!/usr/bin/env python3
"""Independently aggregate an existing verbose CTest log; never run tests."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re


def read(path):
    raw = path.read_bytes()
    return gzip.decompress(raw) if path.suffix == '.gz' else raw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--registration', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    raw, registration = read(args.log), read(args.registration)
    text = raw.decode()
    inventory = json.loads(registration)['tests']
    count = len(inventory)
    if not count or len({item['name'] for item in inventory}) != count:
        raise ValueError('empty or duplicate registered tests')
    pattern = (r'(?m)^\s*(\d+)/' + str(count)
               + r' Test\s+#(\d+):\s+(.+?)\s+\.{2,}\s*(?:\*{3})?(Passed|Failed)\s+')
    results = re.findall(pattern, text)
    if len(results) != count or {int(x[1]) for x in results} != set(range(1, count + 1)):
        raise ValueError('not every registered test has one completion record')
    if {int(x[0]) for x in results} != set(range(1, count + 1)):
        raise ValueError('CTest completion ordinals are missing or duplicated')
    states = {}
    for _, index, name, state in results:
        index = int(index)
        if inventory[index - 1]['name'] != name:
            raise ValueError('result index/name does not match registration')
        states[index] = state
    native = re.findall(r'(?m)^(\d+): \[doctest\]\s+test cases:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed', text)
    assertions = re.findall(r'(?m)^(\d+): \[doctest\]\s+assertions:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed', text)
    python = re.findall(r'(?m)^(\d+): Ran (\d+) tests? in ', text)
    skipped = re.findall(r'(?m)^(\d+): OK \(skipped=(\d+)\)', text)
    for name, groups in [('native', native), ('assertions', assertions), ('python', python)]:
        if len({int(x[0]) for x in groups}) != len(groups):
            raise ValueError('duplicate prefixed ' + name + ' denominator')
    native_by_index = {int(x[0]): x[1:] for x in native}
    assertions_by_index = {int(x[0]): x[1:] for x in assertions}
    python_by_index = {int(x[0]): int(x[1]) for x in python}
    required = ['unit.test_import_screenshot', 'unit.test_import_aux_depth',
                'unit.test_db_annotations', 'unit.test_import_resume',
                'unit.test_import_audit', 'unit.test_import_usage', 'compat.test_archive_cost']
    new_groups = {}
    for name in required:
        index = next(i + 1 for i, item in enumerate(inventory) if item['name'] == name)
        if index in native_by_index:
            values = native_by_index[index]
            entry = {'cases': int(values[0]), 'passed': int(values[1]), 'failed': int(values[2]),
                     'assertions': int(assertions_by_index[index][0])}
        else:
            entry = {'cases': python_by_index[index]}
        if entry['cases'] == 0:
            raise ValueError('new group has zero cases: ' + name)
        entry['ctest_status'] = states[index]
        new_groups[name] = entry
    failed = [inventory[index - 1]['name'] for index, state in sorted(states.items()) if state == 'Failed']
    aggregate = {
        'schema': 'loom.independent_ctest_log_aggregation/1',
        'derivation': 'Computed after the run from immutable logs and registration; not part of the original runner receipt.',
        'log_sha256': hashlib.sha256(raw).hexdigest(),
        'registration_sha256': hashlib.sha256(registration).hexdigest(),
        'ctest': {'registered': count, 'executed': len(results), 'passed': count-len(failed),
                  'failed': len(failed), 'failed_groups': failed},
        'native': {'groups': len(native), 'cases': sum(int(x[1]) for x in native),
                   'passed': sum(int(x[2]) for x in native), 'failed': sum(int(x[3]) for x in native),
                   'assertions': sum(int(x[1]) for x in assertions),
                   'assertions_passed': sum(int(x[2]) for x in assertions),
                   'assertions_failed': sum(int(x[3]) for x in assertions),
                   'zero_case_groups': [inventory[int(x[0])-1]['name'] for x in native if int(x[1]) == 0]},
        'python': {'unittest_groups': len(python), 'attempted_cases': sum(int(x[1]) for x in python),
                   'cases_in_passed_ctest_groups': sum(int(x[1]) for x in python if states[int(x[0])] == 'Passed'),
                   'skips': sum(int(x[1]) for x in skipped),
                   'failed_unittest_groups': [inventory[int(x[0])-1]['name'] for x in python if states[int(x[0])] != 'Passed']},
        'new_groups': new_groups,
        'prefixed_permission_error_groups': [inventory[int(index)-1]['name'] for index in
            re.findall(r'(?m)^(\d+): PermissionError: \[Errno 13\]', text)],
    }
    args.out.write_text(json.dumps(aggregate, indent=2) + '\n')
    print(json.dumps(aggregate, indent=2))


if __name__ == '__main__':
    main()
