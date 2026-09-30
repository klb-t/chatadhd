"""Post-outcome contract diagnostics, preserving competing duplicate field values."""
from collections import Counter
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from loom.tools.structure import graph_free_extraction as free
from loom.tools.structure import graph_panel_live as panel
from loom.tools.structure import openrouter_runner as safe


class Pairs(list):
    pass


def diagnostic_value(value, duplicates, path='$'):
    if isinstance(value, Pairs):
        grouped = {}
        for key, item in value:
            grouped.setdefault(key, []).append(item)
        result = {}
        for key, values in grouped.items():
            translated = [diagnostic_value(v, duplicates, path + '.' + key) for v in values]
            if len(values) == 1:
                result[key] = translated[0]
            else:
                duplicates.append({'path': path + '.' + key, 'competing_values': translated})
                result[key] = {'duplicate_values': translated}
        return result
    if isinstance(value, list):
        return [diagnostic_value(v, duplicates, f'{path}[{i}]') for i, v in enumerate(value)]
    return value


def inspect_content(content):
    duplicates = []
    try:
        diagnostic = diagnostic_value(json.loads(content, object_pairs_hook=Pairs), duplicates)
    except (json.JSONDecodeError, TypeError):
        return {'diagnostic_json_complete': False, 'duplicate_fields': [], 'object': None}
    try:
        safe.parse_json(content)
        strict_json = True
    except ValueError:
        strict_json = False
    rows = diagnostic.get('source_assertions', []) if isinstance(diagnostic, dict) else []
    evidence_shapes = Counter()
    for row in rows:
        if not isinstance(row, dict):
            evidence_shapes['invalid_assertion_record'] += 1; continue
        evidence = row.get('evidence')
        if not isinstance(evidence, list) or not evidence:
            evidence_shapes['missing_or_invalid_evidence_array'] += 1
        elif all(isinstance(e, dict) and set(e) == {'turn_id'} for e in evidence):
            evidence_shapes['required_turn_id_objects'] += 1
        elif all(isinstance(e, str) for e in evidence):
            evidence_shapes['string_ids_instead_of_required_objects'] += 1
        else:
            evidence_shapes['other_invalid_evidence_shape'] += 1
    return {'diagnostic_json_complete': True, 'strict_duplicate_free_json': strict_json,
            'duplicate_fields': duplicates, 'assertion_evidence_shapes': dict(evidence_shapes),
            'object': diagnostic}


def build():
    cases = panel.load_dev_inputs(); by_case = {c['id']: c for c in cases}; rows = []
    for batch in (1, 2):
        folder = Path(__file__).resolve().parent / f'prepared/batch0{batch}/run'
        ledger = safe.parse_json((folder / 'ledger.json').read_bytes())
        for attempt in ledger['attempts']:
            path = folder / attempt['response_file']; raw = path.read_bytes()
            if hashlib.sha256(raw).hexdigest() != attempt['response_sha256']:
                raise ValueError('preserved_response_hash_drift')
            response = safe.parse_json(raw); choice = response.get('choices', [{}])[0]
            content = choice.get('message', {}).get('content')
            diagnostic = inspect_content(content)
            c = by_case[attempt['id']]
            diagnostic.update(case_id=c['id'], source_payload=free.source_payload(c),
                attempt_state=attempt['state'], finish_reason=choice.get('finish_reason'),
                response_file=str(path.relative_to(ROOT)), response_sha256=attempt['response_sha256'],
                reported_cost_usd=attempt.get('reported_cost_usd'),
                post_outcome_diagnostic_only=True, no_output_repair_or_primary_rescore=True)
            rows.append(diagnostic)
    if len(rows) != 24 or len({r['case_id'] for r in rows}) != 24:
        raise ValueError('actual_24_case_inventory_mismatch')
    shapes = Counter(); duplicates = Counter(); finishes = Counter()
    for row in rows:
        shapes.update(row.get('assertion_evidence_shapes', {}))
        duplicates[row['case_id']] = len(row['duplicate_fields'])
        finishes[row['finish_reason']] += 1
    return {'schema': 'loom.free_raw_contract_diagnostics/1', 'split': 'dev',
        'primary_unchanged': True, 'validation_read': False, 'api_calls': 0,
        'finish_counts': dict(finishes), 'assertion_evidence_shapes': dict(shapes),
        'duplicate_fields_by_case': {k: v for k, v in duplicates.items() if v},
        'cases': rows}


if __name__ == '__main__':
    result = build(); path = Path(__file__).resolve().parent / 'raw_contract_diagnostics.json'
    if '--check' in sys.argv:
        assert json.loads(path.read_text()) == result
    else:
        with path.open('x') as out:
            json.dump(result, out, ensure_ascii=False, indent=2, sort_keys=True); out.write('\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'cases'}, sort_keys=True))
