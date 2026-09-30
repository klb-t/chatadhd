"""Deterministic independent fixture authoring; no models, secrets or old gold."""
from __future__ import annotations
from collections import Counter
from datetime import datetime, timedelta, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
ADAPTER = ROOT / 'loom/tools/structure/graph_panel_live.py'
# Match the adapter's supported standalone import context; importing is offline.
sys.path.insert(0, str(ADAPTER.parent))
spec = importlib.util.spec_from_file_location('lifecycle_graph_adapter', ADAPTER)
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)
sys.path.pop(0)

POLICY = {
    'schema': 'loom.research.source_view_policy/1',
    'id': 'source_view.latest_active_commitment/1',
    'key': ['relation', 'source', 'target', 'attributed_to'],
    'cutoff': 'known_at <= as_of; exact timestamps, no future backfill',
    'state_labels': {'positive': 'supported', 'negative': 'refuted',
                     'withdrawn_positive': 'refuted', 'withdrawn_negative': 'unknown',
                     'no_position': 'unknown'},
    'positive_withdrawal_is_not_negative_assertion': True,
    'negative_withdrawal_is_not_positive_assertion': True,
    'silence_changes_no_commitment': True,
    'quoted_speaker_not_reporter': True,
    'retain_all_observations_assertions_and_status_events': True,
    'content_truth': 'unverified', 'no_graph_promotion': True,
    'scope': 'diagnostic_supplied_edge_judgment',
}


def texts(language):
    if language == 'pl':
        positive = 'Jeżeli sygnał zielonej lampki jest aktywny, to brama zostaje odblokowana.'
        denial = 'Zaprzeczam relacji: jeżeli sygnał zielonej lampki jest aktywny, to brama zostaje odblokowana.'
        return {
            'nodes': ['sygnał zielonej lampki jest aktywny', 'brama zostaje odblokowana', 'notatka jest archiwizowana'],
            'positive': positive,
            'deny_replace': 'Wycofuję moją poprzednią deklarację. ' + denial,
            'reaffirm': 'Wycofuję moje poprzednie zaprzeczenie i ponownie deklaruję: ' + positive,
            'withdraw_positive': 'Wycofuję moją wcześniejszą deklarację: ' + positive + ' Nie podaję żadnej relacji zastępczej.',
            'quote_withdrawal': 'Mira napisała: „Wycofuję moją wcześniejszą deklarację: ' + positive + ' Nie podaję relacji zastępczej”. To cytat Miry, nie moje stanowisko.',
            'repeat': 'Powtarzam moją deklarację: ' + positive,
            'repeat_again': 'Jeszcze raz deklaruję tę samą relację: ' + positive,
            'other_positive': 'Mówię wyłącznie we własnym imieniu: ' + positive,
            'negative': denial,
            'withdraw_negative': 'Wycofuję moje wcześniejsze zaprzeczenie relacji: ' + positive + ' Nie twierdzę teraz ani że ta relacja zachodzi, ani że nie zachodzi.',
            'irrelevant': 'Dzisiaj uporządkowałem szufladę z papierami.',
        }
    positive = 'If the green lamp signal is active, then the gate is unlocked.'
    denial = 'I deny the relation: if the green lamp signal is active, then the gate is unlocked.'
    return {
        'nodes': ['the green lamp signal is active', 'the gate is unlocked', 'the note is archived'],
        'positive': positive,
        'deny_replace': 'I withdraw my previous assertion. ' + denial,
        'reaffirm': 'I withdraw my previous denial and reaffirm: ' + positive,
        'withdraw_positive': 'I withdraw my earlier assertion: ' + positive + ' I supply no replacement relation.',
        'quote_withdrawal': 'Mira wrote: "I withdraw my earlier assertion: ' + positive + ' I supply no replacement relation". That is Mira\'s quote, not my position.',
        'repeat': 'I repeat my assertion: ' + positive,
        'repeat_again': 'Once again I assert the same relation: ' + positive,
        'other_positive': 'I speak only for myself: ' + positive,
        'negative': denial,
        'withdraw_negative': 'I withdraw my earlier denial of the relation: ' + positive + ' I now assert neither that this relation holds nor that it does not hold.',
        'irrelevant': 'Today I organized a drawer full of papers.',
    }


# Hand-authored stage, attribution, polarity/event and ternary label plan.
# None of these labels is derived from model output or an existing gold file.
FAMILIES = [
    ('reaffirmation_after_denial',
     [('Mira', 'positive'), ('Mira', 'deny_replace'), ('Mira', 'reaffirm')],
     [(0, 'positive', 'Mira'), (1, 'negative', 'Mira'), (2, 'positive', 'Mira')],
     [(0, 'superseded', 1, 1), (1, 'superseded', 2, 2)],
     [(0, 'Mira', False, 'supported', 'positive', 0, None),
      (1, 'Mira', False, 'refuted', 'negative', 1, None),
      (2, 'Mira', False, 'supported', 'positive', 2, None),
      (2, 'Mira', True, 'unknown', 'no_position', None, None)]),
    ('withdrawal_without_replacement',
     [('Mira', 'positive'), ('Mira', 'withdraw_positive'), ('Mira', 'irrelevant')],
     [(0, 'positive', 'Mira')], [(0, 'withdrawn', None, 1)],
     [(0, 'Mira', False, 'supported', 'positive', 0, None),
      (1, 'Mira', False, 'refuted', 'withdrawn_positive', None, 0),
      (2, 'Mira', False, 'refuted', 'withdrawn_positive', None, 0),
      (2, 'Mira', True, 'unknown', 'no_position', None, None)]),
    ('quoted_withdrawal_attribution',
     [('Mira', 'positive'), ('Owen', 'quote_withdrawal'), ('Owen', 'irrelevant')],
     [(0, 'positive', 'Mira')], [(0, 'withdrawn', None, 1)],
     [(0, 'Mira', False, 'supported', 'positive', 0, None),
      (1, 'Mira', False, 'refuted', 'withdrawn_positive', None, 0),
      (1, 'Owen', False, 'unknown', 'no_position', None, None),
      (2, 'Owen', False, 'unknown', 'no_position', None, None)]),
    ('repeated_positive_history',
     [('Mira', 'positive'), ('Mira', 'repeat'), ('Mira', 'repeat_again')],
     [(0, 'positive', 'Mira'), (1, 'positive', 'Mira'), (2, 'positive', 'Mira')], [],
     [(0, 'Mira', False, 'supported', 'positive', 0, None),
      (1, 'Mira', False, 'supported', 'positive', 1, None),
      (2, 'Mira', False, 'supported', 'positive', 2, None),
      (2, 'Mira', True, 'unknown', 'no_position', None, None)]),
    ('other_speaker_reaffirmation',
     [('Mira', 'positive'), ('Mira', 'deny_replace'), ('Owen', 'other_positive'), ('Mira', 'irrelevant')],
     [(0, 'positive', 'Mira'), (1, 'negative', 'Mira'), (2, 'positive', 'Owen')],
     [(0, 'superseded', 1, 1)],
     [(0, 'Mira', False, 'supported', 'positive', 0, None),
      (1, 'Mira', False, 'refuted', 'negative', 1, None),
      (2, 'Owen', False, 'supported', 'positive', 2, None),
      (3, 'Mira', False, 'refuted', 'negative', 1, None)]),
    ('withdrawn_denial_and_silence',
     [('Mira', 'negative'), ('Mira', 'withdraw_negative'), ('Mira', 'irrelevant')],
     [(0, 'negative', 'Mira')], [(0, 'withdrawn', None, 1)],
     [(0, 'Mira', False, 'refuted', 'negative', 0, None),
      (1, 'Mira', False, 'unknown', 'withdrawn_negative', None, 0),
      (2, 'Mira', False, 'unknown', 'withdrawn_negative', None, 0),
      (2, 'Owen', False, 'unknown', 'no_position', None, None)]),
]


def author():
    cases, golds = [], []
    for fi, (family, turn_plan, assertion_plan, event_plan, query_plan) in enumerate(FAMILIES):
        for language in ('pl', 'en'):
            number = len(cases) + 1
            ident = f'svlv1_dev_{number:03d}'
            source_id = ident + '_source'
            text = texts(language)
            base = datetime(2026, 9, 2, 9, 0, tzinfo=timezone.utc) + timedelta(hours=number)
            turns = [{'id': ident + f'_t{i + 1}', 'speaker': speaker,
                      'known_at': (base + timedelta(minutes=i)).isoformat().replace('+00:00', 'Z'),
                      'text': text[which]} for i, (speaker, which) in enumerate(turn_plan)]
            nodes = [{'id': node, 'text': phrase, 'aliases': []} for node, phrase in zip(('A', 'B', 'C'), text['nodes'])]
            case = {'id': ident, 'language': language, 'source_id': source_id,
                    'turns': turns, 'node_inventory': nodes, 'judgment_queries': []}
            assertions = []
            for ai, (ti, polarity, attribution) in enumerate(assertion_plan):
                turn = turns[ti]
                assertions.append({'id': ident + f'_a{ai + 1}', 'relation': 'implies',
                    'source': 'A', 'target': 'B', 'polarity': polarity, 'attributed_to': attribution,
                    'known_at': turn['known_at'], 'content_truth': 'unverified',
                    'basis_class': 'observed_source_assertion',
                    'evidence': [adapter.locate_evidence({'turn_id': turn['id'], 'quote': turn['text']}, case)]})
            events = []
            for ei, (old, status, replacement, ti) in enumerate(event_plan):
                turn = turns[ti]
                events.append({'id': ident + f'_e{ei + 1}', 'assertion_id': assertions[old]['id'],
                    'status': status, 'superseded_by': assertions[replacement]['id'] if replacement is not None else None,
                    'known_at': turn['known_at'], 'attributed_to': assertions[old]['attributed_to'],
                    'content_truth': 'unverified',
                    'evidence': [adapter.locate_evidence({'turn_id': turn['id'], 'quote': turn['text']}, case)]})
            judgments = []
            for qi, (ti, speaker, reverse, label, stance, active, active_event) in enumerate(query_plan):
                query = {'id': ident + f'_q{qi + 1}', 'relation': 'implies',
                         'source': 'B' if reverse else 'A', 'target': 'A' if reverse else 'B',
                         'attributed_to': speaker, 'as_of': turns[ti]['known_at'], 'scope': 'explicit_source'}
                case['judgment_queries'].append(query)
                history = [] if reverse else [a['id'] for a in assertions if a['known_at'] <= query['as_of'] and a['attributed_to'] == speaker]
                event_history = [] if reverse else [e['id'] for e in events if e['known_at'] <= query['as_of'] and e['attributed_to'] == speaker]
                judgments.append({'query_id': query['id'], 'label': label,
                    'source_view': {'policy_id': POLICY['id'], 'as_of': query['as_of'], 'stance': stance,
                        'active_assertion_ids': [assertions[active]['id']] if active is not None else [],
                        'active_status_event_ids': [events[active_event]['id']] if active_event is not None else [],
                        'historical_assertion_ids': history, 'historical_status_event_ids': event_history,
                        'content_truth': 'unverified'}})
            cases.append(case)
            golds.append({'id': ident, 'family': family, 'language': language,
                          'source_assertions': assertions, 'status_events': events, 'judgments': judgments})
    return {'schema': 'loom.research.graph_methods_panel.inputs/1', 'split': 'dev', 'cases': cases}, {
        'schema': 'loom.research.source_view_lifecycle.gold/1', 'split': 'dev',
        'policy_id': POLICY['id'], 'cases': golds}


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + '\n')


def main():
    inputs, gold = author()
    for name, value in (('inputs_dev.json', inputs), ('gold_dev.json', gold), ('policy.json', POLICY)):
        write_json(HERE / name, value)
    files = ['inputs_dev.json', 'gold_dev.json', 'policy.json', 'PROTOCOL.md', 'author_fixture.py', 'test_fixture.py']
    manifest = {'schema': 'loom.research.source_view_lifecycle.manifest/1',
        'frozen_at_utc': datetime.now(timezone.utc).isoformat(), 'status': 'frozen_before_method_predictions',
        'split': 'diagnostic_dev', 'cases': 12, 'queries': 48, 'languages': ['pl', 'en'],
        'whole_bilingual_families': [f[0] for f in FAMILIES], 'policy_id': POLICY['id'],
        'judgment_label_counts': dict(Counter(q['label'] for g in gold['cases'] for q in g['judgments'])),
        'source_assertions': sum(len(g['source_assertions']) for g in gold['cases']),
        'status_events': sum(len(g['status_events']) for g in gold['cases']),
        'content_truth': 'unverified', 'live_model_calls': 0, 'method_predictions_read': False,
        'old_validation_gold_read': False, 'no_graph_promotion': True,
        'files': {name: hashlib.sha256((HERE / name).read_bytes()).hexdigest() for name in files},
        'dependencies': {str(ADAPTER.relative_to(ROOT)): hashlib.sha256(ADAPTER.read_bytes()).hexdigest()},
        'compatibility': {'query_payload': True, 'score_judgments': True,
                          'withdrawal_extraction_abi': False}}
    write_json(HERE / 'manifest.json', manifest)
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
