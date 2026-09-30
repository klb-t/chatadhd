"""Frozen DEV-only replay of existing GPT bytes. Never makes a live call."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
sys.path.insert(0, str(TOOLS))
import graph_panel_live as panel
import graph_panel_score_run as old
import openrouter_runner as safe
from binding import bind_turn_references, quote_free_copy

ROOT = panel.ROOT
RUN = ROOT / 'docs/research/graph_method_panel_v1/extraction/run'
FIRST = RUN.parent / 'first_score'
FREEZE = HERE / 'freeze_before_replay.json'
RESULTS = HERE / 'first_results'


def hash_file(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, value):
    with path.open('x', encoding='utf-8') as stream:
        json.dump(value, stream, ensure_ascii=False, sort_keys=True, indent=2)
        stream.write('\n')


def paths_for_freeze():
    paths = [HERE / f for f in ('PROTOCOL.md', 'policy.json', 'binding.py', 'replay.py', 'test_binding.py')]
    paths += [TOOLS / f for f in ('graph_panel_live.py', 'graph_panel_score_run.py',
              'openrouter_runner.py', 'jev_live_pilot.py')]
    paths += [panel.FIXTURE / 'inputs_dev.json', panel.FIXTURE / 'manifest.json',
              RUN / 'manifest.json', RUN / 'plan.json', RUN / 'ledger.json', old.SNAPSHOT,
              FIRST / 'compiled_first.json', FIRST / 'score_first.json']
    ledger = old.read(RUN / 'ledger.json')
    paths += [RUN / a['response_file'] for a in ledger['attempts'] if 'response_file' in a]
    return paths


def freeze():
    files = {str(p.relative_to(ROOT)): hash_file(p) for p in paths_for_freeze()}
    write(FREEZE, {'schema': 'loom.turn_reference_binding_freeze/1', 'split': 'dev',
                  'posthoc_development': True, 'gold_read_by_freeze': False,
                  'live_calls': False, 'files': files})
    return {'frozen_files': len(files), 'freeze_sha256': hash_file(FREEZE)}


def verify_freeze():
    frozen = old.read(FREEZE)
    if frozen['split'] != 'dev':
        raise ValueError('dev_only')
    actual_paths = {str(p.relative_to(ROOT)) for p in paths_for_freeze()}
    if actual_paths != set(frozen['files']):
        raise ValueError('freeze_inventory_drift')
    for name, expected in frozen['files'].items():
        if hash_file(ROOT / name) != expected:
            raise ValueError('freeze_hash_drift:' + name)
    return frozen


def run():
    frozen = verify_freeze()
    baseline, execution = old.load_run(RUN / 'manifest.json', RUN,
                                      instrument='gpt', track='assisted_extraction')
    if baseline != old.read(FIRST / 'compiled_first.json'):
        raise ValueError('original_compiled_baseline_drift')
    if len(baseline) != 24 or any(c['state'] != 'completed' for c in baseline):
        raise ValueError('same_24_complete_responses_required')
    cases = panel.load_dev_inputs(); by_case = {c['id']: c for c in cases}
    manifest = old.read(RUN / 'manifest.json'); ledger = old.read(RUN / 'ledger.json')
    attempts = {a['id']: a for a in ledger['attempts']}; snapshot = old.read(old.SNAPSHOT)
    original = []; derived = []; provenance = []; compiled = []
    for request in manifest['requests']:
        ident = request['id']; attempt = attempts[ident]
        raw_path = RUN / attempt['response_file']; raw = raw_path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != attempt['response_sha256']:
            raise ValueError('raw_hash_drift')
        content = old.gpt_content(raw, request['body'], snapshot)
        old.billing_consistent(safe._response_result(raw)['reported_cost_usd'], attempt)
        value = safe.parse_json(content)
        bound, records = bind_turn_references(value, by_case[ident], raw_response_sha256=attempt['response_sha256'])
        if quote_free_copy(bound) != quote_free_copy(value):
            raise ValueError('typed_field_drift')
        original.append({'case_id': ident, 'raw_response_sha256': attempt['response_sha256'], 'content': content})
        derived.append({'case_id': ident, 'value': bound}); provenance.extend(records)
        compiled.append(panel.compile_extraction(bound, by_case[ident]))
    RESULTS.mkdir(exist_ok=False)
    write(RESULTS / 'raw_model_contents_first.json', original)
    write(RESULTS / 'binding_inputs_first.json', derived)
    write(RESULTS / 'provenance_first.json', provenance)
    write(RESULTS / 'compiled_original_first.json', baseline)
    write(RESULTS / 'compiled_binding_first.json', compiled)
    before_gold = {p.name: hash_file(p) for p in sorted(RESULTS.glob('*.json'))}
    write(RESULTS / 'freeze_before_gold.json', {'schema': 'loom.turn_reference_binding_before_gold/1',
          'split': 'dev', 'freeze_sha256': hash_file(FREEZE), 'gold_loaded_by_driver': False, 'files': before_gold})
    # Only now load DEV labels. No validation path is accepted by this driver.
    gold = panel.load_dev_gold()
    original_score = panel.score_extraction(cases, gold, baseline)
    bound_score = panel.score_extraction(cases, gold, compiled)
    prior_score = old.read(FIRST / 'score_first.json')
    for metric in ('strict_edges', 'strict_status_events'):
        if original_score[metric] != prior_score[metric]:
            raise ValueError('original_primary_metric_drift')
    write(RESULTS / 'score_original_first.json', original_score)
    write(RESULTS / 'score_binding_first.json', bound_score)
    original_cases = {c['case_id']: c for c in baseline}
    lost = []
    for c in compiled:
        accepted = {a['id'] for a in c['source_assertions']}
        lost.extend(f"{c['case_id']}:{a['id']}" for a in original_cases[c['case_id']]['source_assertions'] if a['id'] not in accepted)
    summary = {'schema': 'loom.turn_reference_binding_comparison/1', 'split': 'dev',
       'posthoc_development': True, 'case_count': len(cases), 'same_immutable_raw_responses': len(original),
       'live_calls': False, 'typed_field_drift': 0, 'previously_accepted_assertions_lost': lost,
       'evidence_entries': len(provenance), 'bound_entries': sum(p['binding_state'] == 'bound' for p in provenance),
       'refused_entries': sum(p['binding_state'] == 'refused' for p in provenance),
       'quotes_changed': sum(p.get('quote_changed', False) for p in provenance),
       'model_hints_not_exact_unique': sum(p['binding_state'] == 'bound' and not p['model_hint_exact_unique_in_turn'] for p in provenance),
       'original': {k: original_score[k] for k in ('strict_edges', 'strict_status_events')},
       'binding': {k: bound_score[k] for k in ('strict_edges', 'strict_status_events')},
       'compiler_counts': {arm: {'accepted_assertions': sum(len(c['source_assertions']) for c in rows),
               'invalid_assertions': sum(c['invalid_assertions'] for c in rows),
               'accepted_events': sum(len(c['status_events']) for c in rows),
               'invalid_events': sum(c['invalid_events'] for c in rows)} for arm, rows in [('original', baseline), ('binding', compiled)]},
       'actor_filter': False, 'semantic_support_established': False, 'no_graph_promotion': True,
       'projection_scope': 'retrospective_whole_case_not_independent_past_prefix',
       'original_execution_integrity': execution,
       'source_files_verified_after_scoring': len(frozen['files'])}
    verify_freeze()  # including every original response, after scoring
    write(RESULTS / 'comparison_first.json', summary)
    return {k: summary[k] for k in ('case_count', 'bound_entries', 'quotes_changed', 'model_hints_not_exact_unique',
                                 'compiler_counts', 'original', 'binding')}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('freeze', 'run'))
    args = parser.parse_args()
    print(json.dumps(freeze() if args.action == 'freeze' else run(), ensure_ascii=False, sort_keys=True))
