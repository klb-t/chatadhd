"""Separate DEV syntax-scaffold arm; original free compiler/scorer unchanged.

No API, key, index, validation or canonical writes. Root alone executes the new
frozen manifests against its existing shared nonreset budget.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
from decimal import Decimal
import hashlib
from pathlib import Path

try:
    from . import graph_free_extraction as free
except ImportError:
    import graph_free_extraction as free

panel, integrity, safe = free.panel, free.integrity, free.safe
ROOT = free.ROOT
HERE = ROOT / 'docs/research/free_schema_hint_v2'
VARIANT = 'free_schema_hint_v2'
TRACK = free.TRACK
ORIGINAL_FREE_SHA256 = '5ccb81d038d80343a0407630fc4ce806b549e9f3c1a2c5798971092f5a70d91d'
SCHEMA_HINT_OBJECT = {
    'nodes': [{'id': 'node1', 'text': 'proposition1'}, {'id': 'node2', 'text': 'proposition2'}],
    'source_assertions': [
        {'id': 'assertion1', 'relation': 'implies', 'source': 'node1', 'target': 'node2',
         'polarity': 'positive', 'attributed_to': 'speaker', 'known_at': 'time1',
         'evidence': [{'turn_id': 'turn1'}]},
        {'id': 'assertion2', 'relation': 'implies', 'source': 'node1', 'target': 'node2',
         'polarity': 'positive', 'attributed_to': 'speaker', 'known_at': 'time2',
         'evidence': [{'turn_id': 'turn2'}]}],
    'status_events': [{'assertion_id': 'assertion1', 'status': 'superseded',
        'superseded_by': 'assertion2', 'known_at': 'time2', 'evidence': [{'turn_id': 'turn2'}]}],
}
HINT_SUFFIX = ('\n\nLiteral JSON output grammar example; all values are symbolic placeholders, '
    'not source content. Replace placeholders with raw-source values. '
    'All source-dependent inclusion and interpretation rules remain as stated above:\n'
    + safe.canonical(SCHEMA_HINT_OBJECT).decode())
SYSTEM = free.SYSTEM + HINT_SUFFIX

# Exact function identities are deliberate: no modified compiler/scorer/aligner.
source_payload = free.source_payload
compile_free = free.compile_free
score_free = free.score_free
align_nodes = free.align_nodes
batches = free.batches


def _hash(text):
    return hashlib.sha256(text.encode()).hexdigest()


def validate_base():
    if panel.digest_file(free.__file__) != ORIGINAL_FREE_SHA256 or SYSTEM != free.SYSTEM + HINT_SUFFIX:
        raise ValueError('original_free_or_suffix_drift')


def manifest_metadata():
    return {'split': 'dev', 'track': TRACK, 'instrument': 'gpt',
            'batch_cap_usd': '.10', 'session_budget_reset': False,
            'gold_read_during_preparation': False, 'no_graph_promotion': True,
            'variant': VARIANT, 'original_free_sha256': ORIGINAL_FREE_SHA256,
            'base_system_sha256': _hash(free.SYSTEM), 'hint_suffix_sha256': _hash(HINT_SUFFIX),
            'system_sha256': _hash(SYSTEM), 'recipe_sha256': _hash(SYSTEM)}


def requests(cases):
    validate_base()
    rows = free.requests(cases)
    for row in rows:
        row['body']['messages'][0]['content'] = SYSTEM
        row['reservation_usd'] = safe.estimate_reservation(row['body'])['minimum_reservation_usd']
    return rows


def prepare(output_dir):
    rows = requests(panel.load_dev_inputs())
    pricing = integrity.read(ROOT / 'docs/research/graph_method_panel_v1/extraction/prepared/manifest.json')['pricing_evidence']
    directory = Path(output_dir)
    directory.mkdir(parents=True, exist_ok=False)
    panel.write_new(directory / 'requests.json', rows)
    index = []
    for i, batch in enumerate(batches(rows), 1):
        manifest = {'schema': safe.MANIFEST_SCHEMA,
                    'experiment_id': f'graph-dev-free-schema-hint-v2-{i:02d}',
                    'budget_usd': '2', 'max_requests': len(batch), 'requests': batch,
                    'pricing_evidence': pricing, 'metadata': manifest_metadata()}
        plan = safe.plan_manifest(manifest)
        if safe._money(plan['total_reservation_usd']) > Decimal('.10'):
            raise ValueError('schema_hint_batch_cap_exceeded')
        folder = directory / f'batch{i:02d}' / 'prepared'
        folder.mkdir(parents=True, exist_ok=False)
        panel.write_new(folder / 'manifest.json', manifest)
        index.append({'manifest': str((folder / 'manifest.json').relative_to(directory)),
                      'manifest_sha256': panel.digest_file(folder / 'manifest.json'),
                      'experiment_id': manifest['experiment_id'], 'request_ids': [r['id'] for r in batch],
                      'reservation_usd': plan['total_reservation_usd']})
    report = {'variant': VARIANT, 'track': TRACK, 'split': 'dev', 'cases': len(rows),
              'gold_read': False, 'api_calls': 0, 'batch_cap_usd': '.10',
              'maximum_rows_per_batch': free.ROW_LIMIT, 'session_budget_reset': False,
              'same_existing_shared_session_cap_usd': '2', 'batches': index,
              'total_reservation_usd': str(sum((safe._money(b['reservation_usd']) for b in index), Decimal(0)))}
    panel.write_new(directory / 'batch_index.json', report)
    return report


def load_run(manifest_path, run_dir, cases):
    validate_base()
    manifest = integrity.read(manifest_path)
    directory = Path(run_dir)
    if manifest.get('metadata') != manifest_metadata() or safe._money(manifest.get('budget_usd')) != Decimal('2'):
        raise ValueError('schema_hint_manifest_recipe_contract_invalid')
    expected = batches(requests(cases))
    matching = [i for i, batch in enumerate(expected, 1) if manifest.get('requests') == batch]
    if len(matching) != 1:
        raise ValueError('frozen_schema_hint_whole_batch_drift')
    if manifest.get('experiment_id') != f'graph-dev-free-schema-hint-v2-{matching[0]:02d}':
        raise ValueError('schema_hint_experiment_identity_drift')
    plan = safe.plan_manifest(manifest)
    if safe._money(plan['total_reservation_usd']) > Decimal('.10'):
        raise ValueError('schema_hint_batch_cap_exceeded')
    # All gates above run before ledger/response reads and before any gold access.
    attempts = safe._validate_ledger(integrity.read(directory / 'ledger.json'), plan, directory)
    integrity.audit_billing_artifacts(attempts, directory)
    snapshot = integrity.read(integrity.SNAPSHOT)
    by_attempt = {a['id']: a for a in attempts}
    by_case = {c['id']: c for c in cases}
    outputs, diagnostics = [], []
    for request in manifest['requests']:
        ident = request['id']
        attempt = by_attempt.get(ident, {})
        result = {'case_id': ident, 'state': 'unavailable', 'reason': 'not_attempted'}
        if attempt.get('state') == 'completed' and attempt.get('http_status') == 200 and 'response_file' in attempt:
            try:
                raw = (directory / attempt['response_file']).read_bytes()
                if len(raw) > safe.MAX_RESPONSE_BYTES or hashlib.sha256(raw).hexdigest() != attempt['response_sha256']:
                    raise ValueError('response_artifact_drift')
                content = integrity.gpt_content(raw, request['body'], snapshot)
                integrity.billing_consistent(safe._response_result(raw)['reported_cost_usd'], attempt)
                result = compile_free(content, source_payload(by_case[ident]))
            except (KeyError, TypeError, ValueError, IndexError):
                result = {'case_id': ident, 'state': 'unavailable', 'reason': 'response_compile_or_identity_rejected'}
        elif attempt:
            result.update(reason='attempt_not_complete', attempt_state=attempt.get('state'))
        outputs.append(result)
        diagnostics.append({'case_id': ident, 'attempt_state': attempt.get('state', 'not_attempted'),
            'compile_state': result['state'], 'reported_cost_usd': attempt.get('reported_cost_usd'),
            'elapsed_seconds': attempt.get('elapsed_seconds')})
    known = sum((safe._money(a['reported_cost_usd']) for a in attempts if 'reported_cost_usd' in a), Decimal(0))
    uncertain = sum((safe._money(a['reservation_usd']) for a in attempts if 'reported_cost_usd' not in a), Decimal(0))
    summary = {'variant': VARIANT, 'recipe_sha256': _hash(SYSTEM), 'track': TRACK, 'split': 'dev',
        'planned_requests': len(manifest['requests']), 'attempted_requests': len(attempts),
        'compiled_complete': sum(o['state'] == 'completed' for o in outputs),
        'reported_known_cost_usd': str(known), 'missing_cost_attempts': sum('reported_cost_usd' not in a for a in attempts),
        'unknown_attempt_reserved_usd': str(uncertain),
        'elapsed_seconds_recorded_sum': sum(a.get('elapsed_seconds', 0) for a in attempts),
        'manifest_sha256': panel.digest_file(manifest_path),
        'ledger_sha256': panel.digest_file(directory / 'ledger.json'),
        'no_graph_promotion': True, 'diagnostics': diagnostics}
    return outputs, summary


def evaluate(manifest_path, run_dir, output_dir):
    cases = panel.load_dev_inputs()
    outputs, summary = load_run(manifest_path, run_dir, cases)
    folder = Path(output_dir)
    folder.mkdir(parents=True, exist_ok=False)
    panel.write_new(folder / 'compiled_first.json', outputs)
    panel.write_new(folder / 'execution_summary.json', summary)
    # Exact existing evaluation contract, only after outputs persisted unchanged.
    golds = panel.load_dev_gold()
    ids = {o['case_id'] for o in outputs}
    report = score_free([c for c in cases if c['id'] in ids], [g for g in golds if g['id'] in ids], outputs)
    report['variant'] = VARIANT
    report['recipe_sha256'] = _hash(SYSTEM)
    panel.write_new(folder / 'score_first.json', report)
    return summary, report


def freeze_record(prepared_dir):
    validate_base()
    directory = Path(prepared_dir)
    if list(directory.rglob('ledger.json')) or list(directory.rglob('*.response.bin')):
        raise ValueError('cannot_freeze_after_v2_attempts_or_outputs')
    index = integrity.read(directory / 'batch_index.json')
    files = [Path(__file__), ROOT / 'loom/tools/structure/test_graph_free_schema_hint_v2.py',
             HERE / 'PROTOCOL.md', Path(free.__file__), Path(panel.__file__),
             Path(integrity.__file__), Path(safe.__file__), integrity.SNAPSHOT,
             panel.FIXTURE / 'manifest.json', panel.FIXTURE / 'inputs_dev.json',
             ROOT / 'docs/research/graph_free_extraction_v1/FREEZE.json',
             directory / 'requests.json', directory / 'batch_index.json']
    files.extend(directory / b['manifest'] for b in index['batches'])
    def ref(path):
        return str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)
    return {'schema': 'loom.free_schema_hint_v2.freeze/1', 'frozen_at_utc': datetime.now(timezone.utc).isoformat(),
        'before_v2_calls_or_outputs': True, 'after_v1_failures_observed': True,
        'diagnostic_dev_not_validation': True, 'api_calls_by_author': 0, 'validation_read': False,
        'original_free_unchanged': True, 'original_free_sha256': ORIGINAL_FREE_SHA256,
        'base_system_sha256': _hash(free.SYSTEM), 'hint_suffix_sha256': _hash(HINT_SUFFIX),
        'system_sha256': _hash(SYSTEM), 'single_model_input_variable': 'append_generic_literal_output_grammar',
        'gold_loaded_during_prepare': False, 'session_budget_reset': False,
        'same_existing_shared_session_cap_usd': '2', 'batch_index': index,
        'files_sha256': {ref(path): panel.digest_file(path) for path in files}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    prep = sub.add_parser('prepare'); prep.add_argument('--output', type=Path, required=True)
    freeze = sub.add_parser('freeze'); freeze.add_argument('--prepared', type=Path, required=True)
    score = sub.add_parser('score'); score.add_argument('manifest', type=Path)
    score.add_argument('run_dir', type=Path); score.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'prepare':
        report = prepare(args.output)
    elif args.command == 'freeze':
        report = freeze_record(args.prepared)
        panel.write_new(HERE / 'FREEZE.json', report)
    else:
        summary, score = evaluate(args.manifest, args.run_dir, args.output)
        report = {'summary': summary, 'strict_edges': score['strict_edges'],
                  'strict_reference_atom_alignment': score['strict_reference_atom_alignment']}
    print(safe.canonical(report).decode())


if __name__ == '__main__':
    main()
