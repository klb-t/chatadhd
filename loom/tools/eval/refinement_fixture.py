"""T2: mechanically materialize manually authored gold clauses as T1 products.

NOT a consolidator or a semantic label generator. No network or graph writes.
The source shown to a future consolidator contains neither gold nor turn labels.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import sys
from pathlib import Path

CONTRACTS = Path(__file__).resolve().parents[1] / 'contracts'
sys.path.insert(0, str(CONTRACTS))
from validate import ContractValidator, canonical_bytes

KINDS = {'goal', 'required_information', 'format', 'style', 'prohibition',
         'exception', 'alternative', 'open_issue', 'executor_context'}
STATUSES = {'active', 'contested', 'superseded', 'rejected'}
ACTS = {'new_requirement', 'change', 'exception', 'rejection',
        'scope_clarification', 'question', 'executor_only_context',
        'dissatisfaction_without_delta'}
# Synthetic chronology, NOT when these source conversations actually happened.
EPOCH = '2025-01-01T00:00:{:02d}Z'


def load_cases(path: Path) -> list[dict]:
    cases = []
    def unique(pairs):
        out = {}
        for key, value in pairs:
            if key in out:
                raise ValueError('duplicate JSON key')
            out[key] = value
        return out
    def finite(_):
        raise ValueError('non-finite JSON')
    with path.open(encoding='utf-8') as stream:
        for line in stream:
            if line.strip():
                case = json.loads(line, object_pairs_hook=unique, parse_constant=finite)
                validate_case(case)
                cases.append(case)
    if len({c['id'] for c in cases}) != len(cases):
        raise ValueError('duplicate case id')
    return cases


def validate_case(case: dict) -> None:
    """Fixture grammar, not inference about the correctness of authored labels."""
    if case.get('schema') != 'loom.refinement_fixture/1':
        raise ValueError('unknown fixture schema')
    if case.get('language') not in {'pl', 'en'} or case.get('split') not in {'dev', 'validation'}:
        raise ValueError('invalid language or split')
    if not case.get('id') or not case.get('tasks') or not 1 <= len(case.get('turns', [])) < 60:
        raise ValueError('case identity/tasks/turns required')
    tasks = {t['task_id'] for t in case['tasks']}
    if len(tasks) != len(case['tasks']):
        raise ValueError('duplicate task id')
    for turn in case['turns']:
        if turn.get('role') not in {'user', 'assistant'} or not isinstance(turn.get('text'), str) or not turn['text']:
            raise ValueError('invalid turn')
        labels = turn.get('labels', [])
        if (turn['role'] == 'user') != bool(labels):
            raise ValueError('every user turn, and no assistant turn, needs act labels')
        if len({x['task_id'] for x in labels}) != len(labels):
            raise ValueError('duplicate task label')
        for label in labels:
            if label['task_id'] not in tasks or not label['acts'] or not set(label['acts']) <= ACTS:
                raise ValueError('unknown task/act')
            if len(set(label['acts'])) != len(label['acts']):
                raise ValueError('duplicate act')
    for task in case['tasks']:
        clauses = task['gold']
        if not clauses or len({x['id'] for x in clauses}) != len(clauses):
            raise ValueError('missing/duplicate clauses')
        for c in clauses:
            if c['kind'] not in KINDS or c['status'] not in STATUSES or not c['text']:
                raise ValueError('invalid clause')
            indices = c['source_turns']
            if not indices or len(set(indices)) != len(indices):
                raise ValueError('source turns required and unique')
            if any(type(i) is not int or not 0 <= i < len(case['turns']) for i in indices):
                raise ValueError('source turn outside fixture')
            if c['kind'] == 'exception' and not c['conditions']:
                raise ValueError('exception scope must be explicit')


def clean_source(case: dict) -> dict:
    return {'id': case['id'], 'language': case['language'], 'domain': case['domain'],
            'turns': [{'role': t['role'], 'text': t['text']} for t in case['turns']]}


def render_statements(statements: list[dict]) -> dict:
    """Deterministic reference rendering of supplied clauses, not text understanding."""
    text, mapping = '', []
    for s in statements:
        if s['status'] not in {'active', 'contested'}:
            continue
        prefix = f"[{s['kind']}"
        if s['status'] == 'contested':
            prefix += '; contested — do not resolve without clarification'
        if s['kind'] == 'executor_context':
            prefix += '; executor only — not product content'
        prefix += '] '
        line = prefix + s['text']
        if s['conditions']:
            line += ' [scope: ' + ' | '.join(s['conditions']) + ']'
        if text:
            text += '\n'
        start = len(text.encode('utf-8'))
        text += line
        mapping.append({'span': {'byte_start': start, 'byte_len': len(line.encode('utf-8'))},
                        'statement_ids': [s['id']]})
    return {'text': text, 'source_map': mapping}


def materialize_case(case: dict) -> dict:
    validate_case(case)
    cid = case['id']
    source = clean_source(case)
    source_id = 'sha256:' + hashlib.sha256(canonical_bytes(source)).hexdigest()
    events = []
    for i, t in enumerate(source['turns']):
        events.append({'schema': 'loom.history_event/1', 'id': f'{cid}.t{i}',
                       'conversation_id': f'c_{cid}', 'branch_id': 'b_main', 'ordinal': i,
                       'known_at': EPOCH.format(i), 'occurred_at': None, 'kind': 'message',
                       'source': {'source': source_id, 'member': '', 'json_pointer': f'/turns/{i}'},
                       'provider_metadata': {'synthetic_fixture': True},
                       'payload': {'role': t['role'], 'content': t['text']}})
    specs = []
    for task in case['tasks']:
        statements = []
        for c in task['gold']:
            statements.append({'id': c['id'], 'kind': c['kind'], 'status': c['status'],
                               'text': c['text'], 'source_event_ids': [f'{cid}.t{i}' for i in c['source_turns']],
                               'claim_ids': [], 'conditions': list(c['conditions']),
                               'supersedes': list(c['supersedes'])})
        used = sorted({i for c in task['gold'] for i in c['source_turns']})
        specs.append({'schema': 'loom.active_task_spec/1',
                      'product_ref': {'kind': 'product', 'id': f"pd_{cid}_{task['task_id']}_gold_v1"},
                      'goal_id': f"g_{cid}_{task['task_id']}", 'knowledge_run': None,
                      'scope': {'conversation_id': f'c_{cid}', 'branch_id': 'b_main', 'task_id': task['task_id']},
                      'version': 1, 'previous_product_ref': None, 'known_at': EPOCH.format(len(events)-1),
                      'representation': 'derived_product',
                      'materializer': {'id': 'manual-gold-mechanical-expansion', 'version': '1'},
                      'history_event_ids': [e['id'] for e in events],
                      'source_refs': [{'event_id': events[i]['id'], 'locator': events[i]['source'],
                                       'known_at': events[i]['known_at'], 'quote': source['turns'][i]['text']}
                                      for i in used],
                      'statements': statements, 'compiled_instruction': render_statements(statements)})
    labels = [{'event_id': f'{cid}.t{i}', 'task_id': label['task_id'], 'acts': list(label['acts'])}
              for i, turn in enumerate(case['turns']) for label in turn['labels']]
    return {'case_id': cid, 'specs': specs, 'turn_labels': labels,
            'history_events': events, 'history_source': source}


def verify_manifest(root: Path) -> list[str]:
    manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
    failed = []
    for name, expected in manifest['files'].items():
        path = root / name
        if path.parent != root or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected['sha256']:
            failed.append(name)
    return failed


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('corpus', type=Path)
    p.add_argument('--mode', choices=['check', 'inputs', 'gold'], default='check')
    args = p.parse_args(argv)
    try:
        cases = load_cases(args.corpus)
        validator = ContractValidator()
        materialized = [materialize_case(c) for c in cases]
        errors = [{'case_id': d['case_id'], 'codes': [i.code for i in validator.validate(s)]}
                  for d in materialized for s in d['specs'] if validator.validate(s)]
        if errors:
            print(json.dumps({'valid': False, 'errors': errors})); return 1
        if args.mode == 'check':
            print(json.dumps({'valid': True, 'blocks': len(cases),
                              'specifications': sum(len(d['specs']) for d in materialized)}))
        else:
            for d in materialized:
                value = {'case_id': d['case_id'], 'source': d['history_source']} if args.mode == 'inputs' else d
                print(json.dumps(value, ensure_ascii=False, separators=(',', ':')))
        return 0
    except (ValueError, KeyError, TypeError, OSError):
        print(json.dumps({'valid': False, 'error': 'fixture_input_error'})); return 2


if __name__ == '__main__':
    raise SystemExit(main())
