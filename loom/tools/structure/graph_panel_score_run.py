"""Integrity-first, DEV-only replay of preserved graph-panel responses; no API."""
from __future__ import annotations
import argparse
from copy import deepcopy
from decimal import Decimal
import hashlib
import json
from pathlib import Path

try:
    from . import graph_panel_live as panel, openrouter_runner as safe, jev_live_pilot as jev
except ImportError:
    import graph_panel_live as panel, openrouter_runner as safe, jev_live_pilot as jev

SNAPSHOT = panel.ROOT / 'docs/research/model_method_panel_v1/public_preflight/gpt41mini_endpoints.json'


class BillingIntegrityError(RuntimeError):
    """A ledger/provider discrepancy is fatal, never a semantic abstention."""


def read(path):
    return safe.parse_json(Path(path).read_bytes())


def validate_recipe(path):
    if path is None: return None
    recipe = read(path)
    safe._keys(recipe, {'schema', 'instrument', 'track', 'base_system_sha256', 'replacement_system', 'replacement_system_sha256', 'reason'})
    replacement = panel.JUDGE_SYSTEM.replace('Return exactly {', 'Return one JSON object exactly {', 1)
    if (recipe['schema'] != 'loom.graph_panel_recipe_override/1' or recipe['instrument'] != 'gpt' or
            recipe['track'] != 'supplied_edge_judgment' or recipe['reason'] != 'provider_json_mode_literal_requirement' or
            recipe['base_system_sha256'] != hashlib.sha256(panel.JUDGE_SYSTEM.encode()).hexdigest() or
            recipe['replacement_system'] != replacement or
            recipe['replacement_system_sha256'] != hashlib.sha256(replacement.encode()).hexdigest()):
        raise ValueError('unapproved_recipe_override')
    return replacement


def gpt_content(raw, body, snapshot):
    parsed = safe._response_result(raw)
    if parsed['state'] != 'completed':
        raise ValueError('response_not_complete')
    value = safe.parse_json(raw)
    usage = value.get('usage')
    if not isinstance(usage, dict) or 'reported_cost_usd' not in parsed or usage.get('is_byok') is not False:
        raise BillingIntegrityError('response_billing_metadata_missing_or_invalid')
    endpoints = [e for e in snapshot.get('data', {}).get('endpoints', []) if e.get('tag') == panel.PROVIDER and e.get('status') == 0]
    if len(endpoints) != 1 or snapshot.get('data', {}).get('id') != body['model']:
        raise ValueError('public_identity_unavailable')
    endpoint = endpoints[0]
    models = {body['model'], endpoint.get('model_id')}
    if ' | ' in endpoint.get('name', ''): models.add(endpoint['name'].split(' | ', 1)[1])
    providers = {panel.PROVIDER, endpoint.get('provider_name')}
    if value.get('model') not in models or value.get('provider') not in providers:
        raise ValueError('response_model_provider_mismatch')
    if parsed.get('reported_is_byok') is True:
        raise ValueError('unexpected_byok_response')
    return value['choices'][0]['message']['content']


def billing_consistent(raw_cost, attempt):
    try:
        match = 'reported_cost_usd' in attempt and safe._money(attempt['reported_cost_usd']) == safe._money(raw_cost)
    except ValueError:
        raise BillingIntegrityError('invalid_billing_amount') from None
    if not match:
        raise BillingIntegrityError('raw_response_ledger_cost_mismatch')


def audit_billing_artifacts(attempts, directory):
    """Check charges even when response semantics are refused/truncated/invalid."""
    for attempt in attempts:
        if 'response_file' not in attempt:
            if 'reported_cost_usd' in attempt:
                raise BillingIntegrityError('ledger_cost_without_provider_artifact')
            continue
        raw = (directory / attempt['response_file']).read_bytes()
        if hashlib.sha256(raw).hexdigest() != attempt['response_sha256']:
            raise BillingIntegrityError('billing_artifact_hash_drift')
        try:
            value = safe.parse_json(raw)
        except ValueError:
            if 'reported_cost_usd' in attempt:
                raise BillingIntegrityError('unverifiable_ledger_cost') from None
            continue
        usage = value.get('usage') if isinstance(value, dict) else None
        if isinstance(usage, dict) and 'cost' in usage:
            billing_consistent(usage['cost'], attempt)
        elif 'reported_cost_usd' in attempt:
            raise BillingIntegrityError('ledger_cost_without_provider_usage')


def diagnostic_event_convention(cases, golds, compiled):
    """Separate, explicitly labelled alternative convention; primary gold unchanged."""
    altered = deepcopy(golds); alternatives = 0
    by_case = {r['case_id']: r for r in compiled}
    for gold in altered:
        result = by_case.get(gold['id'], {}); predictions = {p['id']: p for p in result.get('source_assertions', [])}
        edges = {e['id']: e for e in gold['source_assertions']}
        for event in gold['status_events']:
            old = edges[event['assertion_id']]
            negative = next((e for e in gold['source_assertions'] if e['relation'] == old['relation'] and
                e['source'] == old['source'] and e['target'] == old['target'] and
                e['polarity'] == 'negative' and e['attributed_to'] == old['attributed_to'] and e['known_at'] == event['known_at']), None)
            if negative is None: continue
            for predicted in result.get('status_events', []):
                target = predictions.get(predicted['superseded_by'])
                if target is not None and panel._edge_key(target) == panel._edge_key(negative):
                    event['superseded_by'] = negative['id']; alternatives += 1; break
    report = panel.score_extraction(cases, altered, compiled)
    return {'diagnostic_only': True, 'gold_convention_ambiguous_cases': alternatives,
            'alternative_replacement': 'same-turn explicit negative denial of old edge',
            'status_events': report['strict_status_events'], 'primary_gold_unchanged': True}


def load_run(manifest_path, run_dir, *, instrument=None, track=None, recipe=None):
    manifest = read(manifest_path); directory = Path(run_dir); ledger = read(directory / 'ledger.json')
    if instrument is None: instrument = 'jev' if manifest.get('schema') == 'loom.jev_manifest/1' else manifest.get('metadata', {}).get('instrument')
    if track is None: track = 'supplied_edge_judgment' if instrument == 'jev' else manifest.get('metadata', {}).get('track')
    if instrument not in ('gpt', 'jev') or track not in ('assisted_extraction', 'supplied_edge_judgment'):
        raise ValueError('unknown_instrument_or_track')
    replacement = validate_recipe(recipe)
    if replacement is not None and (instrument != 'gpt' or track != 'supplied_edge_judgment'):
        raise ValueError('recipe_not_allowed_for_this_track')
    if instrument == 'jev':
        if track != 'supplied_edge_judgment': raise ValueError('jev_not_free_extractor')
        jev.validate_manifest(manifest); attempts = jev.validate_ledger(ledger, manifest, directory)
    else:
        if manifest.get('metadata', {}).get('split') != 'dev': raise ValueError('dev_only')
        plan = safe.plan_manifest(manifest); attempts = safe._validate_ledger(ledger, plan, directory)
    audit_billing_artifacts(attempts, directory)
    cases = panel.load_dev_inputs(); case_by = {c['id']: c for c in cases}
    queries = {q['id']: (c, q) for c in cases for q in c['judgment_queries']}
    if instrument == 'gpt':
        caps = manifest['requests'][0]['body']['provider']['max_price']
        expected_rows = panel.extraction_requests(cases, caps) if track == 'assisted_extraction' else panel.gpt_judgment_requests(cases, caps)
        if replacement is not None:
            for row in expected_rows: row['body']['messages'][0]['content'] = replacement
        expected = {r['id']: r for r in expected_rows}; snapshot = read(SNAPSHOT)
    else:
        expected = {r['case_id']: r for r in panel.jev_judgment_inputs(cases)}
    by_attempt = {a['id']: a for a in attempts}; outputs = []; diagnostics = []
    for request in manifest['requests']:
        ident = request['id']
        if ident not in expected: raise ValueError('unplanned_dev_request')
        if instrument == 'gpt':
            if request['body'] != expected[ident]['body'] or request.get('metadata') != expected[ident]['metadata']:
                raise ValueError('frozen_request_drift')
        elif request['body']['state'] != expected[ident]['state'] or request['body']['questions'] != expected[ident]['questions']:
            raise ValueError('frozen_jev_prefix_or_questions_drift')
        attempt = by_attempt.get(ident, {}); identity_key = 'case_id' if track == 'assisted_extraction' else 'query_id'
        result = {identity_key: ident, 'state': 'unavailable', 'reason': 'not_attempted'}
        if attempt.get('state') == 'completed' and attempt.get('http_status') == 200 and 'response_file' in attempt:
            try:
                raw = (directory / attempt['response_file']).read_bytes()
                if len(raw) > safe.MAX_RESPONSE_BYTES or hashlib.sha256(raw).hexdigest() != attempt['response_sha256']:
                    raise ValueError('response_artifact_drift')
                if instrument == 'jev':
                    parsed = jev.parse_response(raw, request, manifest['model_aliases'])
                    billing_consistent(parsed['reported_cost_usd'], attempt)
                    result = panel.compile_jev_judgment(parsed, ident)
                else:
                    content = gpt_content(raw, request['body'], snapshot)
                    billing_consistent(safe._response_result(raw)['reported_cost_usd'], attempt)
                    result = panel.compile_extraction(content, case_by[ident]) if track == 'assisted_extraction' else panel.compile_gpt_judgment(content, ident)
            except (KeyError, TypeError, ValueError, IndexError):
                result = {identity_key: ident, 'state': 'unavailable', 'reason': 'response_compile_or_identity_rejected'}
        elif attempt:
            result['reason'] = 'attempt_not_complete'; result['attempt_state'] = attempt.get('state')
        outputs.append(result)
        diagnostics.append({'request_id': ident, 'attempt_state': attempt.get('state', 'not_attempted'),
                            'compile_state': result['state'], 'reported_cost_usd': attempt.get('reported_cost_usd'),
                            'elapsed_seconds': attempt.get('elapsed_seconds')})
    costs = [safe._money(a['reported_cost_usd']) for a in attempts if 'reported_cost_usd' in a]
    summary = {'instrument': instrument, 'track': track, 'planned_requests': len(manifest['requests']),
        'attempted_requests': len(attempts), 'compiled_complete': sum(r['state'] == 'completed' for r in outputs),
        'reported_cost_usd': str(sum(costs, Decimal(0))), 'missing_cost_attempts': sum('reported_cost_usd' not in a for a in attempts),
        'elapsed_seconds_recorded_sum': sum(a.get('elapsed_seconds', 0) for a in attempts),
        'manifest_sha256': panel.digest_file(manifest_path), 'ledger_sha256': panel.digest_file(directory / 'ledger.json'),
        'no_graph_promotion': True, 'diagnostics': diagnostics}
    return outputs, summary


def evaluate(manifest_path, run_dir, output_dir, *, instrument=None, track=None, recipe=None):
    outputs, summary = load_run(manifest_path, run_dir, instrument=instrument, track=track, recipe=recipe)
    summary['recipe_sha256'] = panel.digest_file(recipe) if recipe is not None else None
    # Gold is read only AFTER preserving/validating existing first-response artifacts.
    cases = panel.load_dev_inputs(); golds = panel.load_dev_gold()
    if summary['track'] == 'assisted_extraction':
        expected = {r['case_id'] for r in outputs}
        cases = [c for c in cases if c['id'] in expected]; golds = [g for g in golds if g['id'] in expected]
        report = panel.score_extraction(cases, golds, outputs)
        report['event_convention_diagnostic'] = diagnostic_event_convention(cases, golds, outputs)
    else:
        expected = {r['query_id'] for r in outputs}; relevant = []
        for gold in golds:
            item = deepcopy(gold); item['judgments'] = [q for q in gold['judgments'] if q['query_id'] in expected]
            if item['judgments']: relevant.append(item)
        report = panel.score_judgments(relevant, outputs)
    folder = Path(output_dir); folder.mkdir(parents=True, exist_ok=False)
    panel.write_new(folder / 'compiled_first.json', outputs)
    panel.write_new(folder / 'execution_summary.json', summary)
    panel.write_new(folder / 'score_first.json', report)
    return summary, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path); parser.add_argument('run_dir', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--instrument', choices=('gpt', 'jev'))
    parser.add_argument('--track', choices=('assisted_extraction', 'supplied_edge_judgment'))
    parser.add_argument('--recipe', type=Path)
    args = parser.parse_args()
    summary, report = evaluate(args.manifest, args.run_dir, args.output, instrument=args.instrument, track=args.track, recipe=args.recipe)
    print(json.dumps({'instrument': summary['instrument'], 'planned_requests': summary['planned_requests'],
        'compiled_complete': summary['compiled_complete'], 'reported_cost_usd': summary['reported_cost_usd'],
        'strict_edges': report.get('strict_edges'), 'per_class': report.get('per_class')}))


if __name__ == '__main__': main()
