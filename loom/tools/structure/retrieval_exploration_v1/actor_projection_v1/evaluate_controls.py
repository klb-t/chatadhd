"""Measure independently authored toy controls; freeze inputs before first result."""
import hashlib
import json
import resource
import time
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import actor_projection as actor


def execute(control):
    raw = (json.dumps(control['document'], ensure_ascii=False, sort_keys=True, indent=2) + '\n').encode()
    observation = json.loads(json.dumps(control['observation']))
    if isinstance(observation.get('locator'), dict):
        mode = control.get('hash_mode', 'actual')
        if mode == 'actual':
            observation['locator']['source'] = 'sha256:' + hashlib.sha256(raw).hexdigest()
        elif mode == 'wrong':
            observation['locator']['source'] = 'sha256:' + '0'*64
        elif mode != 'preserve':
            raise ValueError('unknown_toy_hash_policy')
    return raw, observation


def expected_matches(control, result):
    checks = []
    if 'expected_state' in control:
        checks.append({'field': 'state', 'pass': result['state'] == control['expected_state']})
    if 'expected_reason' in control:
        checks.append({'field': 'reason', 'pass': result.get('reason') == control['expected_reason']})
    for field, expected in control.get('expected_fields', {}).items():
        actual = result.get('fields', {}).get(field, {})
        if isinstance(expected, str):
            expected = {'state': expected}
        for key, value in expected.items():
            checks.append({'field': field + '.' + key, 'pass': actual.get(key) == value})
    return checks


def prepare(fixture):
    controls = json.loads(fixture.read_text())
    rows = []
    for control in controls['cases']:
        if not control['id'] or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for c in control['id']):
            raise ValueError('invalid_control_filename')
        raw, observation = execute(control)
        raw_path = actor.HERE / 'prepared_control_sources' / (control['id'] + '.json')
        raw_path.parent.mkdir(exist_ok=True)
        with raw_path.open('xb') as handle:
            handle.write(raw)
        rows.append({'case_id': control['id'], 'source_sha256': hashlib.sha256(raw).hexdigest(),
            'raw_source_file': str(raw_path.relative_to(actor.panel.ROOT)), 'actual_observation': observation})
    actor.panel.write_new(actor.HERE / 'prepared_control_manifest.json',
        {'fixture_sha256': actor.panel.sha(fixture), 'rows': rows, 'projection_executed': False})


def run(fixture):
    wall_start, cpu_start = time.perf_counter(), time.process_time()
    freeze = json.loads((actor.HERE / 'freeze_before_outputs.json').read_text())
    for path, expected in freeze['files_sha256'].items():
        if actor.panel.sha(actor.panel.ROOT / path) != expected:
            raise ValueError('projection_control_freeze_drift')
    policy = json.loads((actor.HERE / 'actor_projection_policy.json').read_text())
    controls = json.loads(fixture.read_text())
    prepared = json.loads((actor.HERE / 'prepared_control_manifest.json').read_text())
    if prepared['fixture_sha256'] != actor.panel.sha(fixture):
        raise ValueError('control_fixture_drift')
    inputs = {row['case_id']: row for row in prepared['rows']}
    rows, output_by_id = [], {}
    for control in controls['cases']:
        prepared_row = inputs[control['id']]
        raw_path = actor.panel.ROOT / prepared_row['raw_source_file']
        raw, observation = raw_path.read_bytes(), prepared_row['actual_observation']
        if hashlib.sha256(raw).hexdigest() != prepared_row['source_sha256']:
            raise ValueError('prepared_control_source_drift')
        result = actor.project(observation, raw, policy)
        checks = expected_matches(control, result)
        receipt = {'case_id': control['id'], 'source_sha256': hashlib.sha256(raw).hexdigest(),
            'raw_source_file': str(raw_path.relative_to(actor.panel.ROOT)), 'actual_observation': observation,
            'projection': result, 'checks': checks, 'all_expected_checks_pass': bool(checks) and all(c['pass'] for c in checks)}
        rows.append(receipt)
        output_by_id[control['id']] = result
    cross_checks = []
    for check in controls.get('cross_case_checks', []):
        if check['kind'] != 'source_namespace_distinct':
            raise ValueError('unregistered_cross_case_control')
        values = [output_by_id[ident].get('source_sha256') for ident in check['case_ids']]
        cross_checks.append(check | {'pass': None not in values and len(set(values)) == len(values)})
    result = {'schema': 'loom.research.actor_projection_controls_first/1', 'cases': rows,
        'control_cases_planned': len(controls['cases']), 'control_cases_passed': sum(r['all_expected_checks_pass'] for r in rows),
        'expected_field_checks': sum(len(r['checks']) for r in rows),
        'expected_field_checks_passed': sum(c['pass'] for r in rows for c in r['checks']),
        'cross_case_checks': cross_checks, 'fixture_sha256': actor.panel.sha(fixture),
        'independently_authored_controls': True, 'model_quality_measurement': False,
        'native_runs': 0, 'paid_calls': 0, 'gold_read': False, 'validation_read': False,
        'runtime': {'wall_seconds': time.perf_counter() - wall_start,
            'cpu_seconds': time.process_time() - cpu_start,
            'process_peak_rss_kib': resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
            'scope': 'freeze_checks_toy_serialization_projection_receipts_excluding_python_import_startup'}}
    actor.panel.write_new(actor.HERE / 'first_controls.json', result)
    print(json.dumps({k: v for k, v in result.items() if k not in ('cases',)}))


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=('prepare', 'run'))
    parser.add_argument('--fixture', type=Path, required=True)
    args = parser.parse_args()
    globals()[args.stage](args.fixture)
