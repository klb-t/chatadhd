"""Offline ExperimentSpec expansion and durable queue; existing runner owns billing.

This module performs no network calls and never adopts user settings. Request
templates and policies are caller data. Frozen rendered requests pass through
the existing programme manifest validator, not a second paid executor.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
from decimal import Decimal
import hashlib
import itertools
import json
import math
from pathlib import Path
import random
import re
import sqlite3

from loom.tools.seeding.method_graph import canonical, strict_json


def digest(value):
    return hashlib.sha256(canonical(value)).hexdigest()


def require(condition, code):
    if not condition:
        raise ValueError(code)


def variants(spec):
    """Yield list or Cartesian dimensions without materializing the matrix."""
    design = spec['variants']
    require(design['mode'] in ('list', 'matrix'), 'unsupported_variant_mode')
    if design['mode'] == 'list':
        rows = design['values']
        require(isinstance(rows, list) and bool(rows), 'empty_variants')
        require(all(isinstance(x, dict) for x in rows), 'invalid_variant')
        require(len({digest(x) for x in rows}) == len(rows), 'duplicate_variants')
        yield from deepcopy(rows)
    else:
        axes = design['axes']
        require(isinstance(axes, list) and bool(axes), 'empty_axes')
        names = [x['name'] for x in axes]
        require(len(set(names)) == len(names), 'duplicate_axis')
        for axis in axes:
            values = axis['values']
            require(isinstance(values, list) and bool(values), 'empty_axis')
            require(len({digest(x) for x in values}) == len(values), 'duplicate_axis_values')
        # itertools.product stores axis values, not their combinatorial product.
        for values in itertools.product(*(x['values'] for x in axes)):
            yield dict(zip(names, deepcopy(values)))


def render(template, values):
    """Typed whole-value references preserve caller text and field order."""
    if isinstance(template, dict):
        if set(template) == {'$variant'}:
            return deepcopy(values[template['$variant']])
        if set(template) == {'$select'}:
            selection = template['$select']
            return deepcopy(values[selection['table']][values[selection['key']]])
        if set(template) == {'$match'}:
            selection = template['$match']
            rows = values[selection['table']]
            matches = [r for r in rows if all(r[k] == values[k] for k in selection['fields'])]
            require(len(matches) == 1, 'request_selection_ambiguous')
            return deepcopy(matches[0][selection['value_field']])
        return {k: render(v, values) for k, v in template.items()}
    if isinstance(template, list):
        return [render(v, values) for v in template]
    return template


def validate_spec(spec):
    require(spec['schema'] == 'loom.experiment_spec/1', 'unsupported_spec')
    require(bool(spec['id']) and bool(spec['campaign_id']), 'missing_identity')
    require(type(spec['repetitions']) is int and spec['repetitions'] > 0, 'invalid_repetitions')
    require(spec['response_cache'] is False, 'independent_trials_require_response_cache_disabled')
    require(spec['adoption_policy'] in ('manual', 'proposal', 'automatic'), 'invalid_adoption_policy')
    require(spec['ordering']['mode'] in ('balanced_blocks', 'prefix_grouped'), 'unsupported_ordering')
    require(type(spec['ordering']['seed']) is int, 'ordering_seed_required')
    families = {}
    exact_sources = {}
    require(bool(spec['scope']), 'empty_scope')
    require(len({x['id'] for x in spec['scope']}) == len(spec['scope']), 'duplicate_scope_id')
    for case in spec['scope']:
        require(isinstance(case['source_sha256'], str) and re.fullmatch(r'[0-9a-f]{64}',case['source_sha256']) is not None,
                'invalid_source_hash')
        require(case['split'] in ('development', 'validation', 'used_blind'), 'invalid_split')
        require(case['split'] != 'used_blind', 'used_blind_not_independent')
        for mapping, key in ((families, case['family']), (exact_sources, case['source_sha256'])):
            require(key not in mapping or mapping[key] == case['split'], 'family_or_duplicate_split_leakage')
            mapping[key] = case['split']
    require(bool(spec['metrics']) and bool(spec['evaluators']), 'evaluation_plan_required')
    require(spec['budget']['campaign_reset'] is False, 'campaign_budget_cannot_reset')
    require(spec['triggers']['frequency_seconds'] >= 0, 'invalid_frequency')
    return spec


def jobs(spec):
    """Exact requests, independent occurrence IDs, deterministic order metadata.

    Balanced blocks rotate variant order across source cases and repetitions.
    Prefix grouping is an explicit alternative; both preserve prompt semantics.
    No ordering seed promises reproducible model inference.
    """
    validate_spec(spec)
    for case_index, case in enumerate(spec['scope']):
        for repetition in range(spec['repetitions']):
            for ordinal, variant in enumerate(variants(spec)):
                body = render(spec['request_template'], {**variant, 'case': case['input']})
                raw = canonical(body)
                method_hash = digest(variant)
                occurrence = {'spec': digest(spec), 'case': case['id'], 'variant': method_hash,
                              'repetition': repetition}
                prefix = body.get('messages', [])[:spec['cache_prefix_messages']]
                model, provider = body.get('model'), body.get('provider', {})
                group = digest({'model': model, 'provider': provider, 'route': spec['route_id'],
                                'endpoint': spec['endpoint_identity'], 'prefix': prefix})
                yield {'operation_id': 'experiment:' + digest(occurrence),
                       'case_id': case['id'], 'family': case['family'], 'split': case['split'],
                       'variant': variant, 'variant_index': ordinal,
                       'repetition': repetition, 'case_index': case_index,
                       'request_sha256': hashlib.sha256(raw).hexdigest(), 'request': body,
                       'headers': {'X-OpenRouter-Cache': 'false'}, 'cache_group': group,
                       'requested_model': model, 'requested_provider': deepcopy(provider),
                       'route_id': spec['route_id'], 'endpoint_identity': spec['endpoint_identity'],
                       'prompt_cache_policy': spec['prompt_cache_policy'],
                       'independent_inference_requested': True, 'producer': 'experiment_workflow_v1'}


def ordered_jobs(spec):
    """Balanced blocks stream; explicit global prefix sort materializes its scope."""
    validate_spec(spec)
    seed = spec['ordering']['seed']
    if spec['ordering']['mode'] == 'prefix_grouped':
        rows = sorted(jobs(spec), key=lambda r: (r['cache_group'], r['repetition'], r['case_index'], r['variant_index']))
    else:
        def stream():
            design = spec['variants']
            axes = design.get('axes', [])
            n = len(design['values']) if design['mode'] == 'list' else math.prod(len(a['values']) for a in axes)
            # Validate definitions but never enumerate the matrix for validation.
            next(variants(spec))
            rng = random.Random(seed)
            multiplier = rng.randrange(1, n + 1)
            while math.gcd(multiplier, n) != 1:
                multiplier = multiplier % n + 1
            def at(index):
                if design['mode'] == 'list':
                    return deepcopy(design['values'][index])
                value = {}
                for axis in reversed(axes):
                    index, remainder = divmod(index, len(axis['values']))
                    value[axis['name']] = deepcopy(axis['values'][remainder])
                return value
            for repetition in range(spec['repetitions']):
                for ci, case in enumerate(spec['scope']):
                    for position in range(n):
                        vi = (multiplier * position + ci + repetition + seed) % n
                        variant = at(vi)
                        one = deepcopy(spec)
                        one['scope'] = [case]; one['repetitions'] = 1
                        one['variants'] = {'mode': 'list', 'values': [variant]}
                        row = next(jobs(one))
                        occurrence = {'spec': digest(spec), 'case': case['id'], 'variant': digest(variant), 'repetition': repetition}
                        row.update(operation_id='experiment:' + digest(occurrence), variant_index=vi,
                                   repetition=repetition, case_index=ci)
                        yield row
        rows = stream()
    for index, row in enumerate(rows):
        yield {**row, 'queue_ordinal': index, 'ordering_seed': seed,
               'ordering_mode': spec['ordering']['mode']}


class Queue:
    """Research scheduling journal. This is not the authoritative billing ledger."""
    def __init__(self, path):
        path = Path(path).resolve()
        require(not any((x / '.git').exists() for x in (path.parent, *path.parents)), 'private_queue_inside_git')
        path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.db = sqlite3.connect(path, isolation_level=None)
        path.chmod(0o600)
        self.db.executescript('''
            CREATE TABLE IF NOT EXISTS jobs(id TEXT PRIMARY KEY, payload TEXT NOT NULL,
              state TEXT NOT NULL, result TEXT, payer_reference TEXT);
            CREATE TABLE IF NOT EXISTS events(id TEXT PRIMARY KEY, spec TEXT NOT NULL, at REAL NOT NULL);
            CREATE TABLE IF NOT EXISTS specs(id TEXT PRIMARY KEY, hash TEXT NOT NULL);
        ''')

    def add(self, rows):
        self.db.execute('BEGIN IMMEDIATE')
        try:
            count = 0
            for row in rows:
                text = canonical(row).decode()
                previous = self.db.execute('SELECT payload FROM jobs WHERE id=?', (row['operation_id'],)).fetchone()
                require(previous is None or previous[0] == text, 'operation_identity_reused')
                if previous is None:
                    self.db.execute('INSERT INTO jobs VALUES(?,?,?,NULL,NULL)', (row['operation_id'], text, 'prepared'))
                    count += 1
            self.db.execute('COMMIT')
            return count
        except BaseException:
            self.db.execute('ROLLBACK'); raise

    def trigger(self, spec, event, now):
        validate_spec(spec)
        require(type(now) in (int, float) and math.isfinite(now), 'invalid_event_time')
        policy = spec['triggers']
        if (policy['opt_in'] is not True or event['kind'] not in policy['events']
                or event.get('origin_experiment_id') is not None):
            return False
        self.db.execute('BEGIN IMMEDIATE')
        try:
            bound = self.db.execute('SELECT hash FROM specs WHERE id=?', (spec['id'],)).fetchone()
            require(bound is None or bound[0] == digest(spec), 'trigger_spec_identity_reused')
            last = self.db.execute('SELECT MAX(at) FROM events WHERE spec=?', (spec['id'],)).fetchone()[0]
            seen = self.db.execute('SELECT 1 FROM events WHERE id=?', (event['id'],)).fetchone()
            accepted = not seen and (last is None or now - last >= policy['frequency_seconds'])
            if accepted:
                self.db.execute('INSERT OR IGNORE INTO specs VALUES(?,?)', (spec['id'], digest(spec)))
                self.db.execute('INSERT INTO events VALUES(?,?,?)', (event['id'], spec['id'], now))
            self.db.execute('COMMIT'); return accepted
        except BaseException:
            self.db.execute('ROLLBACK'); raise

    def mark_dispatched(self, operation_id, payer_reference):
        require(bool(payer_reference), 'central_payer_reservation_reference_required')
        changed = self.db.execute("UPDATE jobs SET state='pending',payer_reference=? WHERE id=? AND state='prepared'", (payer_reference, operation_id)).rowcount
        require(changed == 1, 'already_dispatched_or_unknown')
        # A pending operation is never requeued here, even after a timeout.

    def capture_first(self, operation_id, result):
        require(result.get('request_sha256') is not None, 'response_request_binding_required')
        self.db.execute('BEGIN IMMEDIATE')
        try:
            row = self.db.execute('SELECT payload,state,result FROM jobs WHERE id=?', (operation_id,)).fetchone()
            require(row is not None, 'unknown_operation')
            job = strict_json(row[0])
            require(result['request_sha256'] == job['request_sha256'], 'response_request_mismatch')
            if row[2] is not None:
                require(row[2] == canonical(result).decode(), 'first_response_immutable')
            else:
                require(row[1] == 'pending', 'response_without_dispatch')
                require(result.get('response_cache_status') != 'HIT', 'cached_response_not_independent')
                self.db.execute("UPDATE jobs SET state='captured',result=? WHERE id=?", (canonical(result).decode(), operation_id))
            self.db.execute('COMMIT')
        except BaseException:
            self.db.execute('ROLLBACK'); raise

    def snapshot(self):
        return [{'job': strict_json(p), 'state': s, 'result': strict_json(r) if r else None,
                 'payer_reference': ref}
                for p, s, r, ref in self.db.execute('SELECT payload,state,result,payer_reference FROM jobs ORDER BY id')]


def available_budget(record):
    """Offline gate only. Unknown reservations and identity prevent dispatch."""
    if (record.get('campaign_verified') is not True or record.get('key_bound') is not True
            or record.get('current_usage_usd') is None or record.get('reservations_usd') is None
            or record.get('limit_reset') != 'nonrenewing'):
        return None
    values = [Decimal(str(record[k])) for k in ('cap_usd', 'current_usage_usd', 'reservations_usd')]
    require(all(x.is_finite() and x >= 0 for x in values), 'invalid_budget')
    cap, usage, reservations = values
    return max(Decimal(0), cap - usage - reservations)


def result_record(job, raw, metadata):
    """Captured first-envelope hash plus explicit unknown observed instrumentation.

    Raw bytes must be retained by the existing payer. This projection alone is
    not independently authenticated billing or proof of model identity.
    """
    allowed = {'observed_model', 'observed_provider', 'supported_parameters', 'omitted_parameters',
               'started_at', 'completed_at', 'latency_seconds', 'usage', 'prompt_cache',
               'response_cache_status', 'actual_cost_usd', 'generation_id', 'http_status',
               'payer_response_reference', 'billing_verified'}
    require(set(metadata) <= allowed, 'unsupported_result_metadata')
    return {'operation_id':job['operation_id'], 'request_sha256':job['request_sha256'],
            'response_sha256':hashlib.sha256(raw).hexdigest(),
            'requested_model':job['requested_model'], 'requested_provider':job['requested_provider'],
            'route_id':job['route_id'], 'endpoint_identity':job['endpoint_identity'],
            'queue_ordinal':job.get('queue_ordinal'), 'ordering_mode':job.get('ordering_mode'),
            'measurements':{key:None for key in ('native_execution','source_agreement','goal_preservation','preference_preservation')},
            **{key:metadata.get(key) for key in sorted(allowed)}}


def summary(spec, rows):
    """Candidate data retains unknown quality; never edits preferences."""
    states = [x['state'] for x in rows]
    return {'experiment_spec_sha256': digest(spec), 'candidate_status': 'unvalidated',
            'planned': len(rows), 'captured': states.count('captured'),
            'quality': {metric: None for metric in spec['metrics']},
            'adoption_policy': spec['adoption_policy'], 'settings_changed': False,
            'new_provider_calls': 0, 'new_spend_usd': '0',
            'evidence_boundary': 'Offline preparation and captured-byte replay only.'}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--spec', type=Path, required=True)
    p.add_argument('--queue', type=Path, required=True)
    p.add_argument('--summary', type=Path, required=True)
    args = p.parse_args()
    spec = strict_json(args.spec.read_bytes())
    queue = Queue(args.queue)
    queue.add(ordered_jobs(spec))
    result = summary(spec, queue.snapshot())
    with args.summary.open('x') as f:
        json.dump(result, f, indent=2); f.write('\n')
    print(json.dumps({'prepared': result['planned'], 'provider_calls': 0}))


if __name__ == '__main__':
    main()
