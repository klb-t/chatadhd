"""Separate preregistered source-only semantic contrast recipe, not yet inferred.

V2 grammar, source packet, model, caps, compiler and scoring are unchanged;
the sole added model-input factor is generic semantic contrast explanation.
"""
from __future__ import annotations
import argparse
from decimal import Decimal
import hashlib
from pathlib import Path

try:
    from . import graph_free_schema_hint_v2 as grammar
except ImportError:
    import graph_free_schema_hint_v2 as grammar

free, panel, replay, safe = grammar.free, grammar.panel, grammar.integrity, grammar.safe
ROOT = free.ROOT
HERE = ROOT / 'docs/research/recipe_experiments_2026-09-30/semantic_axes_v3'
GRAMMAR_CODE_SHA256 = '2579c90ace2a49e60bf975f212b66dbea3b9e5c7a8940d8e359e96b8acd76da5'
VARIANT = 'free_semantic_axes_v3'
SEMANTIC_SUFFIX = '''

Semantic contrast examples, using symbolic P and Q rather than source content:
Operand negation and whole-relation polarity are independent. If a speaker says
"If not P, then not Q", that speaker AFFIRMS the directed conditional: preserve
the negated operands in node text and use positive relation polarity. If the
speaker explicitly says "I deny that P implies Q", the speaker DENIES that
conditional: preserve P and Q as operands and use negative relation polarity.
In both cases use the same allowed relation predicate; polarity encodes denial,
not a new predicate. Copy actual source operand wording, never symbolic P/Q.
A speaker saying "I report somebody's conditional but do not endorse it" does
not assert or deny that conditional as their own commitment. Attribute the
quoted assertion to its quoted speaker; do not invent a negative assertion for
the reporter. Saying "this rule says nothing about Q" states missing scope,
not a denial of a relation involving Q. Silence is also neither affirmation
nor denial. These examples clarify the original inclusion rules, not source
facts or a requirement to emit records. All original instructions still apply.'''
SYSTEM = grammar.SYSTEM + SEMANTIC_SUFFIX


def validate_base():
    grammar.validate_base()
    if panel.digest_file(grammar.__file__) != GRAMMAR_CODE_SHA256:
        raise ValueError('original_grammar_arm_drift')


def sha(text):
    return hashlib.sha256(text.encode()).hexdigest()


def metadata():
    return {'split': 'dev', 'track': free.TRACK, 'instrument': 'gpt',
        'batch_cap_usd': '.10', 'session_budget_reset': False,
        'gold_read_during_preparation': False, 'no_graph_promotion': True,
        'variant': VARIANT, 'original_free_sha256': grammar.ORIGINAL_FREE_SHA256,
        'grammar_v2_code_sha256': GRAMMAR_CODE_SHA256,
        'grammar_v2_system_sha256': sha(grammar.SYSTEM),
        'semantic_suffix_sha256': sha(SEMANTIC_SUFFIX), 'recipe_sha256': sha(SYSTEM)}


def requests(cases):
    validate_base(); rows = grammar.requests(cases)
    for row in rows:
        row['body']['messages'][0]['content'] = SYSTEM
        row['reservation_usd'] = safe.estimate_reservation(row['body'])['minimum_reservation_usd']
    return rows


def prepare(output):
    rows = requests(panel.load_dev_inputs())
    pricing = replay.read(ROOT / 'docs/research/graph_method_panel_v1/extraction/prepared/manifest.json')['pricing_evidence']
    folder = Path(output); folder.mkdir(parents=True, exist_ok=False)
    panel.write_new(folder / 'requests.json', rows)
    index = []
    for number, batch in enumerate(free.batches(rows), 1):
        manifest = {'schema': safe.MANIFEST_SCHEMA, 'experiment_id': f'graph-dev-free-semantic-axes-v3-{number:02d}',
            'budget_usd': '2', 'max_requests': len(batch), 'requests': batch,
            'pricing_evidence': pricing, 'metadata': metadata()}
        plan = safe.plan_manifest(manifest)
        if safe._money(plan['total_reservation_usd']) > Decimal('.10'):
            raise ValueError('semantic_axes_batch_cap_exceeded')
        destination = folder / f'batch{number:02d}' / 'prepared'; destination.mkdir(parents=True)
        path = destination / 'manifest.json'; panel.write_new(path, manifest)
        index.append({'manifest': str(path.relative_to(folder)), 'manifest_sha256': panel.digest_file(path),
            'request_ids': [r['id'] for r in batch], 'reservation_usd': plan['total_reservation_usd']})
    result = {'variant': VARIANT, 'cases': len(rows), 'batches': index,
        'total_reservation_usd': str(sum((safe._money(b['reservation_usd']) for b in index), Decimal(0))),
        'paid_calls': 0, 'gold_read': False, 'validation_read': False,
        'session_budget_reset': False, 'same_existing_shared_session_cap_usd': '2'}
    panel.write_new(folder / 'batch_index.json', result)
    return result


def freeze(prepared):
    validate_base(); prepared = Path(prepared).resolve()
    if list(prepared.rglob('ledger.json')) or list(prepared.rglob('*.response.bin')):
        raise ValueError('cannot_freeze_semantic_recipe_after_attempts')
    index = replay.read(prepared / 'batch_index.json')
    files = [Path(__file__), Path(__file__).with_name('test_graph_free_semantic_axes_v3.py'),
        HERE / 'PROTOCOL.md', HERE / 'FIRST_MECHANISM_RESULTS.json', Path(grammar.__file__),
        Path(free.__file__), Path(panel.__file__), Path(replay.__file__), Path(safe.__file__),
        replay.SNAPSHOT, panel.FIXTURE / 'manifest.json', panel.FIXTURE / 'inputs_dev.json',
        prepared / 'requests.json', prepared / 'batch_index.json']
    files.extend(prepared / b['manifest'] for b in index['batches'])
    record = {'schema': 'loom.free_semantic_axes_v3.freeze/1', 'frozen_at_utc': safe._utc(),
        'before_v3_calls_or_outputs': True, 'after_v1_diagnostic_outcomes': True,
        'v2_live_outcomes_not_observed': True, 'split': 'diagnostic_dev',
        'single_variable': 'append generic semantic contrast explanation to unchanged v2 grammar',
        'new_paid_calls': 0, 'validation_read': False, 'primary_compiler_unchanged': True,
        'session_budget_reset': False, 'shared_global_cap_usd': '2',
        'files_sha256': {str(p.relative_to(ROOT)): panel.digest_file(p) for p in files}}
    panel.write_new(HERE / 'FREEZE.json', record)
    return record


def load_run(manifest_path, run_dir, cases):
    validate_base(); manifest = replay.read(manifest_path); directory = Path(run_dir)
    if manifest.get('metadata') != metadata() or safe._money(manifest.get('budget_usd')) != Decimal('2'):
        raise ValueError('semantic_axes_manifest_contract_drift')
    expected = free.batches(requests(cases))
    matching = [i for i, batch in enumerate(expected, 1) if manifest.get('requests') == batch]
    if (len(matching) != 1 or manifest.get('experiment_id') != f'graph-dev-free-semantic-axes-v3-{matching[0]:02d}'):
        raise ValueError('semantic_axes_whole_batch_identity_drift')
    plan = safe.plan_manifest(manifest)
    if safe._money(plan['total_reservation_usd']) > Decimal('.10'):
        raise ValueError('semantic_axes_batch_cap_exceeded')
    attempts = safe._validate_ledger(replay.read(directory / 'ledger.json'), plan, directory)
    replay.audit_billing_artifacts(attempts, directory)
    snapshot = replay.read(replay.SNAPSHOT)
    by_attempt = {a['id']: a for a in attempts}; by_case = {c['id']: c for c in cases}
    outputs = []
    for request in manifest['requests']:
        ident = request['id']; attempt = by_attempt.get(ident, {})
        result = {'case_id': ident, 'state': 'unavailable', 'reason': 'not_attempted'}
        if attempt.get('state') == 'completed' and attempt.get('http_status') == 200 and 'response_file' in attempt:
            try:
                raw = (directory / attempt['response_file']).read_bytes()
                if len(raw) > safe.MAX_RESPONSE_BYTES or hashlib.sha256(raw).hexdigest() != attempt['response_sha256']:
                    raise ValueError('response_artifact_drift')
                content = replay.gpt_content(raw, request['body'], snapshot)
                replay.billing_consistent(safe._response_result(raw)['reported_cost_usd'], attempt)
                result = free.compile_free(content, free.source_payload(by_case[ident]))
            except (KeyError, TypeError, ValueError, IndexError):
                result = {'case_id': ident, 'state': 'unavailable', 'reason': 'response_compile_or_identity_rejected'}
        elif attempt:
            result.update(reason='attempt_not_complete', attempt_state=attempt.get('state'))
        outputs.append(result)
    summary = {'variant': VARIANT, 'recipe_sha256': sha(SYSTEM), 'planned_requests': len(manifest['requests']),
        'attempted_requests': len(attempts), 'compiled_complete': sum(o['state'] == 'completed' for o in outputs),
        'reported_known_cost_usd': str(sum((safe._money(a['reported_cost_usd']) for a in attempts if 'reported_cost_usd' in a), Decimal(0))),
        'unknown_cost_attempts': sum('reported_cost_usd' not in a for a in attempts),
        'unknown_reserved_usd': str(sum((safe._money(a['reservation_usd']) for a in attempts if 'reported_cost_usd' not in a), Decimal(0))),
        'manifest_sha256': panel.digest_file(manifest_path), 'ledger_sha256': panel.digest_file(directory / 'ledger.json'),
        'no_graph_promotion': True, 'session_budget_reset': False}
    return outputs, summary


def score(manifest_path, run_dir, output):
    record = replay.read(HERE / 'FREEZE.json')
    for name, expected in record['files_sha256'].items():
        if panel.digest_file(ROOT / name) != expected: raise ValueError('semantic_axes_frozen_artifact_drift')
    cases = panel.load_dev_inputs(); outputs, summary = load_run(manifest_path, run_dir, cases)
    folder = Path(output); folder.mkdir(parents=True, exist_ok=False)
    panel.write_new(folder / 'compiled_first.json', outputs)
    panel.write_new(folder / 'execution_summary.json', summary)
    ids = {r['case_id'] for r in outputs}; golds = panel.load_dev_gold()
    result = free.score_free([c for c in cases if c['id'] in ids], [g for g in golds if g['id'] in ids], outputs)
    result.update(variant=VARIANT, recipe_sha256=sha(SYSTEM))
    panel.write_new(folder / 'score_first.json', result)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__); sub = parser.add_subparsers(dest='command', required=True)
    prep = sub.add_parser('prepare'); prep.add_argument('--output', type=Path, required=True)
    freezing = sub.add_parser('freeze'); freezing.add_argument('--prepared', type=Path, required=True)
    scoring = sub.add_parser('score'); scoring.add_argument('manifest', type=Path); scoring.add_argument('run_dir', type=Path)
    scoring.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'prepare': result = prepare(args.output)
    elif args.command == 'freeze': result = freeze(args.prepared)
    else: result = score(args.manifest, args.run_dir, args.output)
    print(safe.canonical(result).decode())


if __name__ == '__main__': main()
