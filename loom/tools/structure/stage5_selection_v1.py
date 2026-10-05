"""Offline Pareto selection from neutral, already scored aggregate reports.

No archive, case, label, credential, provider or linked-report reads. This checks
aggregate consistency; it is not a substitute for auditing the source scorer.
"""
from __future__ import annotations

import argparse
from decimal import Decimal, InvalidOperation
from fractions import Fraction
import hashlib
import json
from pathlib import Path


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'),
                      ensure_ascii=False, allow_nan=False).encode('utf-8')


def sha256(raw):
    return hashlib.sha256(raw).hexdigest()


def _hash(value):
    return isinstance(value, str) and len(value) == 64 and all(c in '0123456789abcdef' for c in value)


def _number(value):
    if type(value) not in (int, float, str):
        raise ValueError('metric_not_numeric')
    try:
        result = Decimal(str(value))
    except InvalidOperation as exc:
        raise ValueError('metric_not_numeric') from exc
    if not result.is_finite() or result < 0:
        raise ValueError('metric_nonfinite_or_negative')
    return result


def _field(value, path):
    for key in path.split('.'):
        if not isinstance(value, dict) or key not in value:
            return None
        value = value[key]
    return value


def _policy(protocol):
    if protocol.get('schema') != 'loom.stage5.protocol/1' or not protocol.get('version'):
        raise ValueError('invalid_protocol_schema_or_version')
    policy = protocol['selection']
    if policy.get('strategy') != 'pareto' or policy.get('ties') != 'retain_all':
        raise ValueError('unsupported_selection_strategy')
    if policy.get('number_of_selected_configurations') is not None:
        raise ValueError('quota_selection_not_implemented')
    groups = policy.get('group_fields')
    if not isinstance(groups, list) or not groups or len(groups) != len(set(groups)):
        raise ValueError('invalid_group_fields')
    if any(field not in groups for field in ('task_id', 'dataset_sha256', 'planned_query_set_sha256', 'rubric_sha256')):
        raise ValueError('comparison_identity_missing')
    stages = policy.get('allowed_source_stages')
    if not isinstance(stages, list) or not stages or any(type(x) is not int or x < 1 for x in stages):
        raise ValueError('invalid_source_stages')
    criteria = policy.get('criteria')
    if not isinstance(criteria, list) or not criteria:
        raise ValueError('no_selection_criteria')
    ids = []
    for criterion in criteria:
        if (not isinstance(criterion.get('id'), str) or not criterion['id']
                or criterion.get('direction') not in ('maximize', 'minimize')
                or not isinstance(criterion.get('numerator'), str)
                or not isinstance(criterion.get('denominator'), str)):
            raise ValueError('invalid_selection_criterion')
        ids.append(criterion['id'])
    if len(ids) != len(set(ids)):
        raise ValueError('duplicate_criterion_id')
    thresholds = policy.get('eligibility_thresholds', [])
    for threshold in thresholds:
        if threshold.get('criterion') not in ids or threshold.get('operator') not in ('minimum', 'maximum'):
            raise ValueError('invalid_eligibility_threshold')
        _number(threshold.get('value'))
    billing = policy.get('billing_admission', {'require_complete_actual_billing': True})
    if (not isinstance(billing, dict) or set(billing) != {'require_complete_actual_billing'}
            or type(billing['require_complete_actual_billing']) is not bool):
        raise ValueError('invalid_billing_admission_policy')
    return policy


def _record(record, policy):
    if not isinstance(record, dict) or not isinstance(record.get('configuration_id'), str) or not record['configuration_id']:
        raise ValueError('invalid_configuration_id')
    allowed = {'configuration_id', 'source_stage', 'counts', 'cost', 'evidence', *policy['group_fields']}
    if set(record) != allowed:
        raise ValueError('nonneutral_or_missing_record_fields')
    for field in policy['group_fields']:
        if not isinstance(record.get(field), str) or not record[field]:
            raise ValueError('missing_comparison_identity')
        if field.endswith('_sha256') and not _hash(record[field]):
            raise ValueError('invalid_comparison_hash')
    counts = record.get('counts', {})
    if not isinstance(counts, dict):
        raise ValueError('invalid_counts')
    for key, value in counts.items():
        if type(value) is not int or value < 0:
            raise ValueError('invalid_count')
    if ('planned' not in counts or 'available' not in counts or 'attempted_requests' not in counts
            or counts['available'] > counts['planned'] or counts['available'] > counts['attempted_requests']):
        raise ValueError('invalid_planned_or_available_count')
    for key in ('correct', 'semantically_valid', 'source_grounded'):
        if key in counts and counts[key] > counts['available']:
            raise ValueError('quality_count_exceeds_available')
    evidence = record.get('evidence', {})
    if not isinstance(evidence, dict):
        raise ValueError('invalid_evidence')
    if set(evidence) != {'model_quality_measured', 'kind', 'first_response_only',
                         'source_report_sha256', 'dataset_inspection_status'}:
        raise ValueError('nonneutral_or_missing_evidence_fields')
    if (not _hash(evidence.get('source_report_sha256'))
            or not isinstance(evidence.get('dataset_inspection_status'), str)
            or not evidence['dataset_inspection_status']):
        raise ValueError('missing_source_evidence')
    cost = record.get('cost', {})
    if not isinstance(cost, dict):
        raise ValueError('invalid_cost')
    if set(cost) != {'total_usd', 'complete', 'unknown_attempts', 'evidence_kind'}:
        raise ValueError('nonneutral_or_missing_cost_fields')
    if cost.get('total_usd') is not None:
        _number(cost['total_usd'])
    if (type(cost.get('unknown_attempts')) is not int or cost['unknown_attempts'] < 0
            or cost['unknown_attempts'] > counts['attempted_requests']):
        raise ValueError('invalid_unknown_attempt_count')
    if cost.get('complete') is True and (cost['unknown_attempts'] or cost.get('total_usd') is None):
        raise ValueError('contradictory_complete_cost')
    reasons = []
    if type(record.get('source_stage')) is not int or record['source_stage'] not in policy['allowed_source_stages']:
        reasons.append('source_stage_not_selected')
    if evidence.get('model_quality_measured') is not True:
        reasons.append('model_quality_unmeasured')
    if evidence.get('kind') not in ('saved_provider_first_response', 'historical_provider_first_response_replay'):
        reasons.append('not_provider_quality_evidence')
    if evidence.get('first_response_only') is not True:
        reasons.append('not_first_response_evidence')
    if not counts['planned']:
        reasons.append('no_planned_queries')
    if not counts['attempted_requests']:
        reasons.append('no_measured_attempts')
    # Billing admission is independent of the chosen mathematical criteria.
    # A quality-only metric or a cost denominator must not accidentally bypass
    # the default requirement for complete retained first-attempt charges.
    complete_billing = (cost.get('complete') is True and not cost['unknown_attempts']
                        and cost.get('total_usd') is not None)
    supported_billing = cost.get('evidence_kind') in ('provider_reported', 'invoice_reconciled')
    require_complete_billing = policy.get('billing_admission', {}).get('require_complete_actual_billing', True)
    if require_complete_billing:
        if not complete_billing:
            reasons.append('billing_incomplete')
        if not supported_billing:
            reasons.append('billing_evidence_kind_unsupported')
    metrics = {}
    for criterion in policy['criteria']:
        numerator = _field(record, criterion['numerator'])
        denominator = _field(record, criterion['denominator'])
        cost_based = any(criterion[field].startswith('cost.') for field in ('numerator', 'denominator'))
        if cost_based and (not complete_billing or not supported_billing):
            numerator = None
        if numerator is None or denominator is None or _number(denominator) == 0:
            metrics[criterion['id']] = None
            reasons.append('missing_dimension:' + criterion['id'])
        else:
            metrics[criterion['id']] = Fraction(_number(numerator)) / Fraction(_number(denominator))
    for threshold in policy.get('eligibility_thresholds', []):
        value = metrics[threshold['criterion']]
        boundary = _number(threshold['value'])
        if value is not None and ((threshold['operator'] == 'minimum' and value < boundary)
                                  or (threshold['operator'] == 'maximum' and value > boundary)):
            reasons.append('outside_configured_threshold:' + threshold['criterion'])
    return {'configuration_id': record['configuration_id'],
            'group': {field: record[field] for field in policy['group_fields']},
            'metrics': metrics, 'eligible': not reasons, 'reasons': sorted(set(reasons)),
            'source_stage': record['source_stage'], 'counts': counts, 'cost': cost, 'evidence': evidence}


def select(summary, protocol, *, protocol_sha256=None, summary_sha256=None):
    """Derive candidates within identical task/data/query/rubric groups only."""
    policy = _policy(protocol)
    if summary.get('schema') != 'loom.stage5.study_summary/1' or not isinstance(summary.get('records'), list):
        raise ValueError('neutral_summary_required')
    if set(summary) != {'schema', 'records'}:
        raise ValueError('nonneutral_summary_fields')
    rows = [_record(record, policy) for record in summary['records']]
    groups = {}
    planned_counts = {}
    identities = set()
    for row in rows:
        key = canonical(row['group']).decode()
        identity = (key, row['configuration_id'])
        if identity in identities:
            raise ValueError('duplicate_configuration_in_comparison_group')
        if key in planned_counts and planned_counts[key] != row['counts']['planned']:
            raise ValueError('conflicting_planned_denominators')
        planned_counts[key] = row['counts']['planned']
        identities.add(identity)
        groups.setdefault(key, []).append(row)
    results = []
    for key, members in sorted(groups.items()):
        eligible = [row for row in members if row['eligible']]
        for row in members:
            dominators = []
            if row['eligible']:
                for rival in eligible:
                    comparisons = [(rival['metrics'][c['id']] - row['metrics'][c['id']]) *
                                   (1 if c['direction'] == 'maximize' else -1) for c in policy['criteria']]
                    if all(value >= 0 for value in comparisons) and any(value > 0 for value in comparisons):
                        dominators.append(rival['configuration_id'])
            row['dominated_by'] = sorted(dominators)
            row['selected_for_stage5'] = row['eligible'] and not dominators
        for row in members:
            row['metrics'] = {name: str(value) if value is not None else None for name, value in row['metrics'].items()}
        results.append({'group': json.loads(key), 'selected_configuration_ids': sorted(
            row['configuration_id'] for row in members if row['selected_for_stage5']),
            'records': sorted(members, key=lambda row: row['configuration_id'])})
    result = {'schema': 'loom.stage5.selection/1', 'protocol_version': protocol['version'],
              'protocol_sha256': protocol_sha256 or sha256(canonical(protocol)),
              'summary_sha256': summary_sha256 or sha256(canonical(summary)),
              'selector_sha256': sha256(Path(__file__).read_bytes()), 'groups': results,
              'new_model_calls': 0, 'tool_output_grants_authorization': False,
              'selection_is_derived_from_supplied_aggregates_not_raw_scoring': True}
    result['selection_payload_sha256'] = sha256(canonical(result))
    return result


def read_neutral(path):
    """Read only the explicitly supplied file; never follow source references."""
    path = Path(path).resolve()
    if any('real-holdout-key' in part or 'validation_cases' in part for part in path.parts):
        raise ValueError('sealed_or_validation_path_refused')
    raw = path.read_bytes()
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('duplicate_json_key')
            result[key] = value
        return result
    def nonfinite(value):
        raise ValueError('nonfinite_json_number')
    # Preserve JSON decimal lexemes exactly; turning them into floats first
    # would irreversibly merge distinct observed costs before Fraction sees them.
    return json.loads(raw, object_pairs_hook=pairs, parse_float=str,
                      parse_constant=nonfinite), sha256(raw)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--summary', required=True, type=Path)
    parser.add_argument('--protocol', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args(argv)
    protocol, protocol_hash = read_neutral(args.protocol)
    summary, summary_hash = read_neutral(args.summary)
    result = select(summary, protocol, protocol_sha256=protocol_hash, summary_sha256=summary_hash)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('xb') as handle:
        handle.write(canonical(result) + b'\n')
    return result


if __name__ == '__main__':
    main()
