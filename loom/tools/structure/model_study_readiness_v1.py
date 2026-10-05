"""Offline successor preparation, campaign resume audit and cost-aware scoring.

No credential loading, account lookup or live inference entrypoint. Existing
frozen instruments remain unchanged; a successor records request equality and
the original source drift instead of rewriting their freeze receipts.
"""
from __future__ import annotations

import argparse
from collections import Counter
from decimal import Decimal
import hashlib
import math
from pathlib import Path
import tempfile
import time

from . import analysis_optimization_v1 as study

safe, jev, w3 = study.safe, study.jev, study.w3


def digest_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def frozen_status(prepared, *, recipe=False):
    """Validate immutable payload bytes; enumerate source drift without hiding it."""
    prepared = Path(prepared)
    freeze = study.read(prepared / 'freeze.json')
    source_key = 'source_files_sha256' if recipe else 'source_sha256'
    files_key = 'plan_files_sha256' if recipe else 'files_sha256'
    expected_schema = ('loom.w3_directed_commitment.freeze/1' if recipe
                       else 'loom.analysis_optimization.freeze/1')
    if freeze.get('schema') != expected_schema:
        raise ValueError('freeze_schema_invalid')
    drift = []
    for root, key in ((study.ROOT, source_key), (prepared, files_key)):
        for name, expected in freeze[key].items():
            path = (root / name).resolve()
            if not path.is_relative_to(root.resolve()) or not path.is_file():
                raise ValueError('frozen_path_invalid_or_missing')
            actual = digest_file(path)
            if actual != expected:
                if key == files_key:
                    raise ValueError('frozen_payload_drift')
                drift.append({'path': name, 'expected_sha256': expected,
                              'current_sha256': actual})
    return {'source_matches_freeze': not drift, 'source_drift': drift,
            'payload_files_verified': len(freeze[files_key]),
            'freeze_sha256': digest_file(prepared / 'freeze.json')}


def successor_prepare(original, snapshots, output, *, recipe_original=None):
    """Use saved snapshots only; never turn old prices into new live evidence."""
    original, output = Path(original), Path(output)
    first_status = frozen_status(original)
    original_plan = study.read(original / 'plan.json')
    if output.exists():
        raise ValueError('use_new_successor_directory')
    # The existing builder's transport is an injected snapshot-only function.
    # All current dependencies are independently frozen in this new preparation.
    successor = output / 'prepared'
    plan = study.prepare(snapshots, successor)
    study.verify(successor)
    unchanged = 0
    pricing_unchanged = 0
    if plan['arms'] != original_plan['arms']:
        raise ValueError('successor_arm_or_reservation_drift')
    for arm in plan['arms']:
        name = arm['arm']
        before = study.read(original / name / 'manifest.json')
        after = study.read(successor / name / 'manifest.json')
        if before['requests'] != after['requests']:
            raise ValueError('successor_request_drift')
        unchanged += len(before['requests'])
        before_pricing = (before['created_at'] if arm['kind'] == 'jev'
                          else before['pricing_evidence'])
        after_pricing = (after['created_at'] if arm['kind'] == 'jev'
                         else after['pricing_evidence'])
        if before_pricing != after_pricing:
            raise ValueError('successor_pricing_timestamp_drift')
        pricing_unchanged += 1
    receipt = {'schema': 'loom.model_study.successor/1', 'original': first_status,
               'requests_equal': unchanged, 'requests_planned': plan['total_requests'],
               'pricing_arms_equal': pricing_unchanged,
               'new_model_calls': 0, 'gold_read_for_preparation': False,
               'old_snapshots_are_not_live_price_evidence': True,
               'readiness_tool_sha256': digest_file(__file__)}
    old_freeze = study.read(original / 'freeze.json')
    new_freeze = study.read(successor / 'freeze.json')
    gold_paths = {name for name in old_freeze['source_sha256'] if name.endswith('gold_dev.json')}
    receipt['gold_byte_hashes_equal'] = {
        name: old_freeze['source_sha256'][name] == new_freeze['source_sha256'].get(name)
        for name in sorted(gold_paths)}
    if not all(receipt['gold_byte_hashes_equal'].values()):
        raise ValueError('successor_gold_bytes_drift')
    if recipe_original is not None:
        recipe_original = Path(recipe_original)
        recipe_status = frozen_status(recipe_original, recipe=True)
        destination = output / 'recipes'
        recipe_plan = w3.prepare(destination)
        w3.verify_plan(destination)
        if recipe_plan != study.read(recipe_original / 'plan.json'):
            raise ValueError('successor_recipe_plan_drift')
        equal_queries = 0
        for batch in recipe_plan['batches']:
            for filename in ('inputs.json', 'request.json'):
                relative = Path(batch['directory']) / filename
                if study.read(destination / relative) != study.read(recipe_original / relative):
                    raise ValueError('successor_recipe_inputs_drift')
            equal_queries += batch['queries']
        receipt['recipes'] = {'original': recipe_status, 'queries_equal': equal_queries,
                              'gold_read_for_preparation': False}
    study.write(output / 'successor_receipt.json', receipt)
    return receipt


def _snapshot_status(manifest, kind, now, max_age_seconds, future_tolerance_seconds):
    dates = ([manifest['created_at']] if kind == 'jev'
             else [p['retrieved_at'] for p in manifest['pricing_evidence']])
    rows = []
    for date in dates:
        age = now - safe._timestamp(date)
        rows.append({'retrieved_at': date, 'age_seconds': round(age, 3),
                     'fresh': -future_tolerance_seconds <= age <= max_age_seconds})
    return {'evidence': rows, 'fresh': all(r['fresh'] for r in rows)}


def arm_audit(prepared, runs, arm, *, now, max_age_seconds, future_tolerance_seconds):
    manifest = study.read(Path(prepared) / arm['arm'] / 'manifest.json')
    planned = (manifest['requests'] if arm['kind'] == 'jev'
               else safe.plan_manifest(manifest)['requests'])
    directory = Path(runs) / arm['arm']
    path = directory / 'ledger.json'
    ledger = study.read(path) if path.exists() else None
    blockers = []
    attempts = []
    if ledger is not None:
        attempts = (jev.validate_ledger(ledger, manifest, directory) if arm['kind'] == 'jev'
                    else safe._validate_ledger(ledger, safe.plan_manifest(manifest), directory))
        if ledger.get('stopped_reason'):
            blockers.append('stopped_ledger')
    else:
        if any(directory.glob('*.response.bin')):
            blockers.append('stranded_responses_without_ledger')
    fresh = _snapshot_status(manifest, arm['kind'], now, max_age_seconds,
                             future_tolerance_seconds)
    evidence_rows = []
    for row in attempts:
        cost = safe._money(row['reported_cost_usd']) if 'reported_cost_usd' in row else None
        if cost is None:
            blockers.append('attempt_with_unknown_cost')
        if row['state'] in ('started', 'uncertain'):
            blockers.append('attempt_with_uncertain_effect')
        raw = ((directory / row['response_file']).read_bytes()
               if 'response_file' in row else None)
        value = None
        if raw is not None:
            try:
                value = safe.parse_json(raw)
            except safe.RunnerError:
                pass
        usage = value.get('usage', {}) if isinstance(value, dict) else {}
        if not isinstance(usage, dict):
            usage = {}
        raw_cost = None
        if 'cost' in usage:
            try:
                raw_cost = safe._money(usage['cost'])
            except safe.RunnerError:
                pass
        if cost is not None and raw_cost is not None and cost != raw_cost:
            raise ValueError('billing_mismatch')
        if row['state'] == 'completed':
            if row.get('http_status') != 200 or raw is None or cost is None or raw_cost is None:
                raise ValueError('completed_attempt_missing_verified_billing_or_response')
            if arm['kind'] == 'jev':
                jev.parse_response(raw, manifest['requests'][len(evidence_rows)], manifest['model_aliases'])
            elif (not isinstance(value, dict) or value.get('model') != manifest['requests'][len(evidence_rows)]['body']['model']
                  or safe._response_result(raw).get('state') != 'completed'):
                raise ValueError('completed_response_identity_or_transport_drift')
        if cost is not None and cost > safe._money(row['reservation_usd']):
            blockers.append('reported_cost_exceeds_reservation')
        token_keys = ('input_tokens', 'output_tokens') if arm['kind'] == 'jev' else ('prompt_tokens', 'completion_tokens')
        tokens = {name: usage.get(key) if type(usage.get(key)) is int and usage[key] >= 0 else None
                  for name, key in zip(('input_tokens', 'output_tokens'), token_keys)}
        elapsed = row.get('elapsed_seconds')
        if type(elapsed) not in (int, float) or not math.isfinite(elapsed) or elapsed < 0:
            elapsed = None
        evidence_rows.append({'query_id': row['id'], 'state': row['state'],
                              'request_hash': row['request_hash'],
                              'response_sha256': row.get('response_sha256'),
                              'reported_cost_usd': str(cost) if cost is not None else None,
                              'unknown_cost_reservation_usd': row['reservation_usd'] if cost is None else '0',
                              **tokens, 'elapsed_seconds': elapsed,
                              'started_at': row.get('started_at'), 'finished_at': row.get('finished_at')})
    unattempted = planned[len(attempts):]
    return {'arm': arm['arm'], 'kind': arm['kind'], 'manifest_hash': safe.digest(manifest),
            'planned_requests': len(planned), 'attempts': len(attempts),
            'states': dict(Counter(r['state'] for r in attempts)),
            'first_response_only': True, 'retry_attempt_ids': [],
            'never_retry_attempt_ids': [r['id'] for r in attempts],
            'unattempted_ids': [r['id'] for r in unattempted],
            'unattempted_reservation_usd': str(sum((safe._money(r['reservation_usd']) for r in unattempted), Decimal(0))),
            'reported_cost_usd': str(sum((safe._money(r['reported_cost_usd']) for r in evidence_rows
                                        if r['reported_cost_usd'] is not None), Decimal(0))),
            'unknown_cost_reservation_usd': str(sum((safe._money(r['unknown_cost_reservation_usd']) for r in evidence_rows), Decimal(0))),
            'snapshot': fresh, 'blockers': sorted(set(blockers)), 'evidence_rows': evidence_rows}


def audit(prepared, runs, *, recipes=None, now=None, max_age_seconds=86400,
          future_tolerance_seconds=300):
    """Read-only resume plan. Any ambiguous arm blocks the whole campaign."""
    if any(type(value) not in (int, float) or not math.isfinite(value) or value < 0
           for value in (max_age_seconds, future_tolerance_seconds)):
        raise ValueError('invalid_price_age_policy')
    plan = study.verify(prepared)
    now = time.time() if now is None else now
    if type(now) not in (int, float) or not math.isfinite(now):
        raise ValueError('invalid_explicit_clock')
    arms = [arm_audit(prepared, runs, a, now=now, max_age_seconds=max_age_seconds,
                     future_tolerance_seconds=future_tolerance_seconds) for a in plan['arms']]
    blockers = [{'arm': a['arm'], 'reason': reason} for a in arms for reason in a['blockers']]
    if any(a['unattempted_ids'] and not a['snapshot']['fresh'] for a in arms):
        blockers.append({'arm': None, 'reason': 'stale_price_evidence'})
    overlaps = []
    if recipes is not None:
        recipe_plan = w3.verify_plan(recipes)
        fingerprints = {safe.digest(request['body']): (a['arm'], request['id'])
                        for a in plan['arms'] for request in study.read(Path(prepared) / a['arm'] / 'manifest.json')['requests']}
        for batch in recipe_plan['batches']:
            inputs = study.read(Path(recipes) / batch['directory'] / 'inputs.json')
            matches = []
            for row in inputs:
                body = {'model': jev.MODEL, 'state': row['state'], 'questions': row['questions'], 'provider': w3.provider()}
                if safe.digest(body) in fingerprints:
                    matches.append({'recipe_query_id': row['case_id'],
                                    'study_arm': fingerprints[safe.digest(body)][0],
                                    'study_query_id': fingerprints[safe.digest(body)][1]})
            if matches:
                overlaps.append({'recipe_directory': batch['directory'], 'identical_requests': matches,
                                 'do_not_execute_as_additional_work': True})
    remaining = sum((safe._money(a['unattempted_reservation_usd']) for a in arms), Decimal(0))
    unknown = sum((safe._money(a['unknown_cost_reservation_usd']) for a in arms), Decimal(0))
    historic = safe._money(plan['historical_unknown_cost_allowance_usd'])
    return {'schema': 'loom.model_study.readiness/1', 'new_model_calls': 0,
            'offline_instrument_verified': True, 'live_execution_authorized': False,
            'campaign_resume_blocked': bool(blockers), 'blockers': blockers,
            'required_live_prerequisites': ['owner_permission', 'valid_programme_credential',
                                           'fresh_key_identity_and_usage_reconciliation',
                                           'fresh_endpoint_evidence_if_unattempted',
                                           'shared_nonresetting_budget_reservation'],
            'price_age_policy_seconds': {'maximum': max_age_seconds, 'future_tolerance': future_tolerance_seconds},
            'historical_total_reservation_usd': plan['total_reservation_usd'],
            'remaining_request_reservation_usd': str(remaining),
            'unknown_attempt_reservation_usd': str(unknown),
            'historical_unknown_cost_allowance_usd': str(historic),
            'minimum_account_allowance_for_remaining_plan_usd': str(remaining + unknown + historic),
            'historical_not_current_price_estimate': True, 'recipe_request_overlap': overlaps,
            'arms': arms}


def _cost_summary(rows, correct):
    unknown = [r for r in rows if r['reported_cost_usd'] is None]
    reported = sum((safe._money(r['reported_cost_usd']) for r in rows
                    if r['reported_cost_usd'] is not None), Decimal(0))
    elapsed = [r['elapsed_seconds'] for r in rows if r['elapsed_seconds'] is not None]
    return {'attempted_requests': len(rows), 'correct_usable_queries': correct,
            'reported_cost_usd': str(reported), 'attempts_with_unknown_cost': len(unknown),
            'cost_accounting_complete_for_attempts': not unknown,
            'unknown_cost_reservation_usd': str(sum((safe._money(r['unknown_cost_reservation_usd']) for r in rows), Decimal(0))),
            'cost_per_correct_usable_query_usd': str(reported / correct) if correct and not unknown else None,
            'input_tokens_recorded': sum(r['input_tokens'] for r in rows if r['input_tokens'] is not None),
            'output_tokens_recorded': sum(r['output_tokens'] for r in rows if r['output_tokens'] is not None),
            'attempts_missing_token_usage': sum(r['input_tokens'] is None or r['output_tokens'] is None for r in rows),
            'elapsed_seconds_recorded': sum(elapsed), 'attempts_with_latency': len(elapsed),
            'elapsed_is_sum_of_request_latency_not_campaign_wall_time': True}


def score(prepared, runs, output, *, evidence_class):
    """Keep the original scorer and enrich it with exact first-attempt costs."""
    if evidence_class not in ('saved_provider_replay', 'scripted_mechanism'):
        raise ValueError('explicit_response_evidence_class_required')
    readiness = audit(prepared, runs)
    with tempfile.TemporaryDirectory(prefix='loom-study-score-') as folder:
        result = study.score(prepared, runs, Path(folder) / 'score.json')
    resources = {a['arm']: a['evidence_rows'] for a in readiness['arms']}
    # Split is one decision with two billed requests. Never count only one half.
    resources['j_split'] = resources.pop('j_split_q01') + resources.pop('j_split_q02')
    result['resource_accounting'] = {}
    for arm, report in result['reports'].items():
        correct = sum(report['confusion'][label][label] for label in study.LABELS)
        result['resource_accounting'][arm] = _cost_summary(resources[arm], correct)
    result.update(schema='loom.model_study.score/1', new_model_calls=0,
                  first_attempt_resources=readiness['arms'],
                  resume_blockers=readiness['blockers'],
                  evidence_class=evidence_class,
                  model_quality_measured=(result['model_quality_measured']
                                          and evidence_class == 'saved_provider_replay'),
                  new_model_quality_measured=False,
                  paid_execution_authorized=False,
                  partial_response_evidence_is_not_complete_study=True)
    study.write(output, result)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    successor = sub.add_parser('successor-prepare')
    for name in ('original', 'snapshots', 'output'):
        successor.add_argument('--' + name, type=Path, required=True)
    successor.add_argument('--recipe-original', type=Path)
    check = sub.add_parser('audit')
    check.add_argument('--prepared', type=Path, required=True)
    check.add_argument('--runs', type=Path, required=True)
    check.add_argument('--recipes', type=Path)
    check.add_argument('--max-price-age-seconds', type=float, default=86400)
    check.add_argument('--future-tolerance-seconds', type=float, default=300)
    check.add_argument('--output', type=Path, required=True)
    scoring = sub.add_parser('score')
    for name in ('prepared', 'runs', 'output'):
        scoring.add_argument('--' + name, type=Path, required=True)
    scoring.add_argument('--evidence-class', required=True,
                         choices=('saved_provider_replay', 'scripted_mechanism'))
    args = parser.parse_args(argv)
    if args.command == 'successor-prepare':
        result = successor_prepare(args.original, args.snapshots, args.output,
                                   recipe_original=args.recipe_original)
    elif args.command == 'audit':
        result = audit(args.prepared, args.runs, recipes=args.recipes,
                       max_age_seconds=args.max_price_age_seconds,
                       future_tolerance_seconds=args.future_tolerance_seconds)
        study.write(args.output, result)
    else:
        result = score(args.prepared, args.runs, args.output, evidence_class=args.evidence_class)
    print(safe.canonical({'schema': result['schema'], 'new_model_calls': 0}).decode())


if __name__ == '__main__':
    main()
