#!/usr/bin/env python3
"""Zwykły dialog przez prawdziwy serwer HTTP; dostawca zapisuje otrzymane żądania."""
import hashlib
import json
from pathlib import Path
import unittest

from test_server_guests import overlay

from server_flow_harness import HttpFlowCase


class DialogFlow(HttpFlowCase):
    def test_history_reopen_restart_provider_error_and_recovery(self):
        q1 = 'Wymyślony znacznik tej rozmowy to bursztynowy żółw. Potwierdź.'
        d1 = self.success(q1)
        self.assertEqual([(m['role'], self.text(m)) for m in self.wire_messages()], [('user', q1)])
        q2 = 'Jaki znacznik podałem wcześniej?'
        self.success(q2)
        expected = [('user', q1), ('assistant', self.provider.reply(1)), ('user', q2)]
        self.assertEqual([(m['role'], self.text(m)) for m in self.wire_messages()], expected)
        rows = self.history()
        self.assertEqual(rows[1]['id'], d1['message_id'])
        self.assertEqual([(m['role'], m['text']) for m in rows], expected + [('assistant', self.provider.reply(2))])
        self.assertEqual(self.history(), rows)
        self.restart()
        self.assertEqual(self.history(), rows)
        q3 = 'Kontynuuj po restarcie, zachowaj historię.'
        self.success(q3)
        expected += [('assistant', self.provider.reply(2)), ('user', q3)]
        self.assertEqual([(m['role'], self.text(m)) for m in self.wire_messages()], expected)
        good = self.history()
        self.provider.fail = True
        failed_text = 'Ta próba dostawcy ma zgłosić błąd.'
        events = self.send(failed_text)
        self.assertEqual([e['type'] for e in events if e.get('type') in ('done', 'error')], ['error'], events)
        error = next(e for e in events if e.get('type') == 'error')
        self.assertIn('400', error['message'])
        self.assertIn('Syntetyczny błąd dostawcy HTTP', error['message'])
        self.assertEqual(len(self.provider.calls), 4)
        self.assertEqual(self.provider.calls[-1]['status'], 400)
        expected += [('assistant', self.provider.reply(3)), ('user', failed_text)]
        self.assertEqual([(m['role'], self.text(m)) for m in self.wire_messages()], expected)
        after = self.history()
        # Kontrakt: przy błędzie dostawcy zachowany wysłany tekst użytkownika,
        # brak fałszywej odpowiedzi; ukończone wcześniejsze tury bez zmian.
        self.assertEqual(after[:len(good)], good)
        self.assertEqual(len(after), len(good) + 1)
        self.assertEqual(after[-1]['role'], 'user')
        self.assertEqual(after[-1]['text'], failed_text)
        self.assertEqual(len({m['id'] for m in after}), len(after))
        self.restart()
        self.assertEqual(self.history(), after)
        self.provider.fail = False
        recovery = 'Dostawca znowu działa. Kontynuuj.'
        self.success(recovery)
        expected.append(('user', recovery))
        self.assertEqual([(m['role'], self.text(m)) for m in self.wire_messages()], expected)
        self.assertEqual(len(self.provider.calls), 5)
        self.assertEqual(len(self.transcript.read_text(encoding='utf-8').splitlines()), 5)
        final = self.history()
        self.assertEqual(final[:len(after)], after)
        self.assertEqual(final[-1]['text'], self.provider.reply(5))


class ContextFlow(HttpFlowCase):
    def api(self, path, body):
        r = self.client.post(self.srv.base + path, json=body, timeout=30)
        self.assertEqual(r.status_code, 200, r.text)
        return r.json()

    def receipt(self, done):
        run = self.api('/api/context/receipt', {'msg_id': done['message_id'], 'conversation_id': self.conv})
        self.assertEqual(run['status'], 'completed', run)
        data = run['result']['data']
        self.assertEqual(data['request_msg_id'], self.history()[-2]['id'])
        receipt = data['receipt']
        self.assertEqual(receipt['schema'], 'loom.context_receipt/1')
        actual = json.dumps(self.provider.calls[-1]['request']['messages'], ensure_ascii=False, separators=(',', ':'))
        self.assertEqual(receipt['sha256'], hashlib.sha256(actual.encode('utf-8')).hexdigest())
        return receipt, data['request_msg_id']

    def test_receipt_graph_sources_omissions_and_wider_for_one_turn(self):
        # Ten sam istniejący mechanizm profili; małe okno daje obserwowalną różnicę requestów.
        pack = json.loads((Path(__file__).resolve().parents[2] / 'data/runtime/chat_context.pack').read_text())
        profiles = pack['defaults']['selector_profiles']
        profile = dict(next(p for p in profiles if p['id'] == 'compat'))
        profile.update(id='http-window2', order='stability', representation='raw')
        profile['breadth'] = {**profile['breadth'], 'history_window': 2}
        guest = self.client.get(self.srv.base + '/api/auth/me', timeout=15).json()['guest']['id']
        overlay(self.root / 'guests' / guest, 'chat_context', {'selector_profiles': profiles + [profile]})
        options = {'selector_profile': 'http-window2'}
        for q in ('Pierwszy wymyślony fakt: bursztyn.', 'Drugi wymyślony fakt: żółw.'):
            self.success(q, **options)
        baseline = self.success('Pytanie bazowe o wymyślone fakty.', **options)
        before = self.history()
        self.assertEqual(len(self.wire_messages()), 3)
        receipt, request_id = self.receipt(baseline)
        history_section = next(s for s in receipt['sections'] if s['class'] == 'history')
        self.assertEqual([i['ref'] for i in history_section['items']], [m['id'] for m in before[2:4]])
        self.assertTrue(all(i['source_kind'] == 'message:L0' and i['why'] for i in history_section['items']))
        dropped = next(d for d in receipt['dropped'] if d['reason'] == 'history_window')
        self.assertEqual(dropped['refs'], [m['id'] for m in before[:2]])
        focus = 'msg:' + request_id.removeprefix('m_')
        graph = self.api('/api/graph/navigate', {'focus': focus, 'perspectives': ['sent_to_model'], 'limit': 0, 'lang': 'pl'})
        self.assertEqual(graph['schema'], 'loom.graph_navigate/1')
        self.assertTrue(graph['receipt']['present'])
        self.assertEqual(graph['receipt']['request_address'], focus)
        omitted_address = 'msg:' + before[0]['id'].removeprefix('m_')
        omitted = next(e for e in graph['edges'] if e['kind'] == 'left_out_of_model' and e['dst'] == omitted_address)
        self.assertEqual(omitted['metadata']['reason'], 'history_window')
        self.assertTrue(omitted['metadata']['reason_human'])
        self.assertEqual(len(self.provider.calls), 3, 'odczyt śladu/grafu nie wywołuje modelu')
        modified = self.api('/api/context/modify', {'conversation_id': self.conv, 'step': 'wider',
                                                    'pending_text': 'Pytanie z szerszym kontekstem.', 'pending_options': options})
        self.assertEqual(modified['status'], 'completed', modified)
        self.assertEqual(modified['result']['data']['waiting']['context.breadth_delta'], ['wider'])
        self.assertEqual(self.history(), before)
        self.assertEqual(len(self.provider.calls), 3)
        wider = self.success('Pytanie z szerszym kontekstem.', **options)
        wire = self.wire_messages()
        self.assertEqual([(m['role'], self.text(m)) for m in wire[:-1]], [(m['role'], m['text']) for m in before[-4:]])
        widened_receipt, _ = self.receipt(wider)
        self.assertEqual(widened_receipt['modifiers']['breadth_delta'], ['wider'])
        self.assertEqual(widened_receipt['modifiers']['scope'], 'turn')
        after_wider = self.history()
        reset = self.success('Pytanie po automatycznym resecie.', **options)
        self.assertEqual([(m['role'], self.text(m)) for m in self.wire_messages()[:-1]],
                         [(m['role'], m['text']) for m in after_wider[-2:]])
        reset_receipt, _ = self.receipt(reset)
        self.assertIsNone(reset_receipt['modifiers']['breadth_delta'])
        self.assertEqual(len(self.provider.calls), 5)
        # Dwa świadome kroki nadal są dwoma krokami, również w faktycznym requestcie.
        before_double = self.history()
        for _ in range(2):
            change = self.api('/api/context/modify', {'conversation_id': self.conv, 'step': 'wider',
                                                       'pending_options': options})
            self.assertEqual(change['status'], 'completed', change)
        double = self.success('Pytanie po dwóch świadomych krokach.', **options)
        self.assertEqual([(m['role'], self.text(m)) for m in self.wire_messages()[:-1]],
                         [(m['role'], m['text']) for m in before_double[-8:]])
        double_receipt, _ = self.receipt(double)
        self.assertEqual(double_receipt['modifiers']['breadth_delta'], ['wider', 'wider'])
        self.assertEqual(len(self.provider.calls), 6)


if __name__ == '__main__':
    unittest.main(verbosity=2)
