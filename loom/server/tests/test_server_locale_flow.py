#!/usr/bin/env python3
"""P0: rzeczywisty zapis ustawienia i profilu przez HTTP, bez deklaracji GUI."""
import json
import unittest

from server_flow_harness import HttpFlowCase


class LocaleFlow(HttpFlowCase):
    def config(self):
        response = self.client.get(self.srv.base + '/api/config', timeout=15)
        self.assertEqual(response.status_code, 200, response.text)
        return response.json()

    def onboarding(self, **body):
        response = self.client.post(self.srv.base + '/api/onboarding', json=body, timeout=15)
        self.assertEqual(response.status_code, 200, response.text)
        return response.json()

    def test_settings_action_persists_language_without_rewriting_model_settings(self):
        self.patch({'language': 'en', 'system_prompt': 'Synthetic preference: reply in English.'})
        before = self.config()
        model_keys = ('language', 'system_prompt', 'default_model', 'base_url')
        for language in ('en', 'pl', 'auto'):
            with self.subTest(language=language):
                response = self.client.post(self.srv.base + '/api/actions/run',
                    json={'action': 'settings.set', 'args': {'key': 'ui_language', 'value': language}}, timeout=15)
                self.assertEqual(response.status_code, 200, response.text)
                self.assertEqual(response.json()['status'], 'completed', response.json())
                self.assertEqual(self.config()['ui_language'], language)
                configs = list((self.root / 'guests').glob('*/config.json'))
                self.assertEqual(len(configs), 1)
                # Wyłącznie config utworzony przez ten test, żadnych ustawień hosta.
                self.assertEqual(json.loads(configs[0].read_text())['ui_language'], language)
                self.restart()
                after = self.config()
                self.assertEqual(after['ui_language'], language)
                self.assertEqual({k: after.get(k) for k in model_keys},
                                 {k: before.get(k) for k in model_keys})
        self.assertEqual(self.provider.calls, [])

    def test_profile_language_is_user_layer_and_survives_reopen_restart(self):
        identity = 'synthetic-integrator-p0-locale'
        opened = self.onboarding(operation='open', user_id=identity)
        self.onboarding(operation='apply', user_id=identity, expected_revision=opened['revision'],
                        action={'target': 'layers', 'op': 'override', 'key': 'preference.language',
                                'value': 'en', 'id': 'p0-language-choice',
                                'time': '2026-10-31T00:00:00Z', 'source_refs': []})
        def check():
            read = self.onboarding(operation='read', user_id=identity)
            preference = next(row for row in read['snapshot']['effectiveDefaults']
                              if row['key'] == 'preference.language')
            self.assertEqual(preference['value'], 'en')
            self.assertEqual(preference['layer'], 'user')
            self.assertEqual(self.config()['ui_language'], 'pl')
        check()
        self.restart()
        check()
        self.assertEqual(self.provider.calls, [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
