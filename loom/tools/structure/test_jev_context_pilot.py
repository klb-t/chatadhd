"""Causal containment and candidate coverage, without corpus gold or network."""
from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

try:
    from . import jev_context_pilot as pilot
except ImportError:
    import jev_context_pilot as pilot


def development_cases():
    split = json.loads((pilot.FIXTURE / 'split.json').read_text())
    selected = {r['case_id'] for r in split['cases'] if r['split'] == 'development'}
    return [c for c in pilot.materializer().read_lines('inputs.jsonl') if c['case_id'] in selected]


class ContextProjectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cases = development_cases()
        cls.project = staticmethod(pilot.materializer().project)

    def test_all_48_requests_fit_without_pruning(self):
        for case in self.cases:
            for ordinal in range(6):
                export = self.project(case, ordinal)
                row, audit = pilot.from_export(export)
                view = json.loads(row['state']['text'])
                self.assertEqual(view['topics'], export['topics'])
                self.assertEqual(view['messages'], export['messages'])
                for key, value in export['source_packet'].items():
                    if key not in ('base_hash', 'packet_hash', 'excluded_context_counts'):
                        self.assertEqual(view['source_packet'][key], value)
                self.assertEqual({x['id'] for x in audit['questions'].values() if x['kind'] == 'membership'},
                                 {x['id'] for x in export['topics']})
                self.assertEqual({x['id'] for x in audit['questions'].values() if x['kind'] == 'selected_claim'},
                                 {x['id'] for x in export['source_packet']['claims']})
                self.assertLessEqual(audit['body_bytes'], pilot.jev.BODY_LIMIT)

    def test_future_text_and_handles_do_not_change_request(self):
        for case in self.cases:
            for ordinal in range(5):
                changed = deepcopy(case)
                for message in changed['messages'][ordinal + 1:]:
                    observation = next(o for o in changed['source_packet']['observations'] if o['id'] == message['observation'])
                    observation['text'] = 'FORBIDDEN future semantic change'
                    message['id'] = 'm_future_never_transmit_' + str(message['ordinal'])
                    message['span_id'] = 's_future_never_transmit_' + str(message['ordinal'])
                before = pilot.from_export(self.project(case, ordinal))[0]
                after = pilot.from_export(self.project(changed, ordinal))[0]
                self.assertEqual(before, after)
                self.assertNotIn('FORBIDDEN', after['state']['text'])
                self.assertNotIn('future_never_transmit', after['state']['text'])

    def test_rejected_extra_export_metadata_cannot_enter_request(self):
        export = self.project(self.cases[0], 0)
        for field in ('gold', 'family', 'expected_labels'):
            bad = deepcopy(export)
            bad[field] = {'leak': True}
            with self.assertRaisesRegex(ValueError, 'unexpected_export_shape'):
                pilot.from_export(bad)

    def test_existing_claims_are_not_filtered_by_target_words(self):
        case = next(c for c in self.cases if c['source_packet']['claims'])
        export = self.project(case, 0)
        row, audit = pilot.from_export(export)
        self.assertEqual(len([v for v in audit['questions'].values() if v['kind'] == 'selected_claim']),
                         len(export['source_packet']['claims']))
        self.assertGreater(len(export['source_packet']['claims']), 0)

    def test_future_claim_content_and_id_never_enter_body(self):
        case = next(c for c in self.cases if c['source_packet']['claims'])
        changed = deepcopy(case)
        hidden = deepcopy(changed['source_packet']['claims'][0])
        hidden.update(id='k_hidden_future_candidate', value='FORBIDDEN future value',
                      observed_at='2027-01-01T00:00:00Z')
        changed['source_packet']['claims'].append(hidden)
        for ordinal in range(6):
            before = pilot.from_export(self.project(case, ordinal))[0]
            after = pilot.from_export(self.project(changed, ordinal))[0]
            self.assertEqual(before, after)
            self.assertNotIn('k_hidden_future_candidate', after['state']['text'])

    def test_topic_and_claim_questions_remain_independent(self):
        export = self.project(next(c for c in self.cases if len(c['topics']) == 2), 0)
        row, audit = pilot.from_export(export)
        self.assertEqual(len([v for v in audit['questions'].values() if v['kind'] == 'membership']), 2)
        self.assertTrue(all(q['type'] == 'noul' for q in row['questions'].values()))
        self.assertIn('zero, one or several', json.loads(row['state']['text'])['instruction'])

    def test_oversize_or_too_many_candidates_rejected_not_truncated(self):
        export = self.project(self.cases[0], 0)
        huge = deepcopy(export)
        huge['source_packet']['observations'][0]['text'] += 'x' * 20000
        with self.assertRaises(pilot.jev.safe.RunnerError):
            pilot.from_export(huge)
        many = deepcopy(export)
        many['topics'] = [{'id': f't_{i}', 'description': 'topic'} for i in range(33)]
        with self.assertRaisesRegex(pilot.jev.safe.RunnerError, 'questions_invalid'):
            pilot.from_export(many)

    def test_no_label_files_or_network_and_manifest_replays(self):
        original = Path.open
        opened = []
        def guard(path, *args, **kwargs):
            opened.append(path.name)
            if path.name in ('gold.jsonl', 'validation.json'):
                self.fail('label/result file accessed')
            return original(path, *args, **kwargs)
        with tempfile.TemporaryDirectory() as folder, patch.object(Path, 'open', guard), \
                patch.object(pilot.jev, 'transport', side_effect=AssertionError('network call')):
            directory = Path(folder) / 'prepared'
            manifest = pilot.prepare_offline(directory)
            self.assertEqual(manifest['requests'], 48)
            self.assertFalse(manifest['labels_read'])
            self.assertFalse(manifest['semantic_accuracy_measured'])
            self.assertEqual(manifest['inputs_sha256'], pilot.digest(json.loads((directory / 'inputs.json').read_text())))
            self.assertFalse(json.loads((directory / 'request.disabled.json').read_text())['enabled'])
            with self.assertRaises(FileExistsError):
                pilot.prepare_offline(directory)
        self.assertNotIn('gold.jsonl', opened)


if __name__ == '__main__':
    unittest.main()
