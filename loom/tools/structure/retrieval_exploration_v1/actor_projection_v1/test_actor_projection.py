import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parent))
import actor_projection as actor


def toy():
    document = {'metadata': {'loom_source_id': 'source:alpha'}, 'mapping': {'turn-a': {'message': {
        'id': 'turn-a', 'author': {'role': 'user', 'name': 'Aria'}, 'create_time': 1789030800,
        'content': {'parts': ['Żółć przygoda.']}, 'metadata': {'loom_source_speaker': 'Aria',
            'loom_source_id': 'source:alpha', 'loom_source_turn_id': 'turn-a',
            'loom_source_known_at': '2026-09-10T09:00:00Z'}}}}}
    observation = {'id': 'ob-toy', 'date': '2026-09-10T09:00:00Z', 'speaker': 'user', 'text': 'Żółć przygoda.',
        'locator': {'json_pointer': '/mapping/turn-a/message/content/parts/0', 'byte_start': 0,
            'byte_len': len('Żółć przygoda.'.encode())}}
    return document, observation


class ActorProjectionTests(unittest.TestCase):
    def setUp(self):
        self.document, self.observation = toy()
        self.message = self.document['mapping']['turn-a']['message']
        self.policy = json.loads((actor.HERE / 'actor_projection_policy.json').read_text())

    def result(self):
        raw = json.dumps(self.document, ensure_ascii=False).encode()
        observation = copy.deepcopy(self.observation)
        if isinstance(observation.get('locator'), dict):
            observation['locator']['source'] = 'sha256:' + hashlib.sha256(raw).hexdigest()
        return actor.project(observation, raw, self.policy)

    def test_same_recorded_actor_and_role_remain_separate(self):
        out = self.result()
        self.assertEqual(out['fields']['source_actor_label']['value'], 'Aria')
        self.assertEqual(out['fields']['transport_role']['value'], 'user')
        self.assertEqual(out['actor_epistemic_status'], 'source_recorded')
        self.assertFalse(out['native_graph_modified'])
        self.assertTrue(all(v['state'] == 'resolved' for v in out['fields'].values()))

    def test_actor_name_metadata_conflict_abstains(self):
        self.message['metadata']['loom_source_speaker'] = 'Bela'
        self.assertEqual(self.result()['fields']['source_actor_label'], {'state':'conflicting_source_fields','value':None})

    def test_missing_actor_does_not_fall_back_to_user_role(self):
        del self.message['author']['name']
        del self.message['metadata']['loom_source_speaker']
        self.assertEqual(self.result()['fields']['source_actor_label']['state'], 'unknown')

    def test_one_raw_actor_source_can_resolve_without_second(self):
        del self.message['metadata']['loom_source_speaker']
        self.assertEqual(self.result()['fields']['source_actor_label']['value'], 'Aria')

    def test_mixed_source_ids_explicit_abstention(self):
        self.document['metadata']['loom_source_id'] = 'source:beta'
        self.assertEqual(self.result()['fields']['source_id']['state'], 'conflicting_source_fields')

    def test_colliding_turn_ids_remain_bound_to_distinct_source_hashes(self):
        first = self.result()
        self.message['metadata']['loom_source_id'] = 'source:beta'
        self.document['metadata']['loom_source_id'] = 'source:beta'
        second = self.result()
        self.assertEqual(first['fields']['source_turn_id'], second['fields']['source_turn_id'])
        self.assertNotEqual(first['source_sha256'], second['source_sha256'])
        self.assertNotEqual(first['fields']['source_id'], second['fields']['source_id'])

    def test_turn_id_source_fields_conflict(self):
        self.message['metadata']['loom_source_turn_id'] = 'turn-b'
        self.assertEqual(self.result()['fields']['source_turn_id']['state'], 'conflicting_source_fields')

    def test_known_at_none_is_unknown_without_epoch_imputation(self):
        self.message['metadata']['loom_source_known_at'] = None
        self.assertEqual(self.result()['fields']['source_known_at_statement']['state'], 'unknown')

    def test_conflicting_epoch_date_abstains(self):
        self.message['create_time'] += 60
        self.assertEqual(self.result()['fields']['source_known_at_statement']['state'], 'conflicting_source_fields')

    def test_missing_observation_date_is_explicit(self):
        self.observation['date'] = None
        self.assertEqual(self.result()['fields']['source_known_at_statement']['state'], 'invalid_date')

    def test_missing_locator_is_unavailable(self):
        del self.observation['locator']
        self.assertEqual(self.result()['reason'], 'missing_or_invalid_native_locator')

    def test_malformed_locator_is_unavailable(self):
        self.observation['locator'] = []
        self.assertEqual(self.result()['reason'], 'missing_or_invalid_native_locator')

    def test_unsupported_pointer_is_not_guessed(self):
        self.observation['locator']['json_pointer'] = '/messages/0/text'
        self.assertEqual(self.result()['reason'], 'unsupported_source_pointer_format')

    def test_malformed_pointer_scalar_is_unavailable(self):
        self.observation['locator']['json_pointer'] = None
        self.assertEqual(self.result()['reason'], 'missing_or_invalid_source_pointer')

    def test_narrow_quote_retains_message_actor(self):
        self.observation['text'] = 'przygoda'
        self.observation['locator']['byte_start'] = len('Żółć '.encode())
        self.observation['locator']['byte_len'] = len('przygoda'.encode())
        self.assertEqual(self.result()['fields']['source_actor_label']['value'], 'Aria')

    def test_inside_codepoint_is_unavailable(self):
        self.observation['locator']['byte_start'] = 1
        self.assertEqual(self.result()['state'], 'unavailable')

    def test_span_outside_source_does_not_accept_empty_quote(self):
        self.observation['text'] = ''
        self.observation['locator']['byte_start'] = 100
        self.observation['locator']['byte_len'] = 0
        self.assertEqual(self.result()['reason'], 'utf8_span_outside_source_text')

    def test_invalid_actor_type_abstains(self):
        self.message['author']['name'] = ['Aria']
        self.assertEqual(self.result()['fields']['source_actor_label']['state'], 'invalid_type_or_empty')

    def test_source_hash_mismatch_is_unavailable(self):
        raw = json.dumps(self.document).encode()
        self.observation['locator']['source'] = 'sha256:' + '0'*64
        self.assertEqual(actor.project(self.observation, raw, self.policy)['reason'], 'raw_source_hash_mismatch')

    def test_projection_never_mutates_raw_or_observation(self):
        before = copy.deepcopy((self.document, self.observation))
        self.result()
        self.assertEqual((self.document, self.observation), before)

    def test_unsupported_identity_policy_is_not_silently_ignored(self):
        self.policy['role_fallback_for_actor'] = True
        with self.assertRaisesRegex(ValueError, 'identity_policy'):
            self.result()

    def test_unsupported_resolution_policy_is_not_silently_ignored(self):
        self.policy['conflict_policy'] = 'prefer_name'
        with self.assertRaisesRegex(ValueError, 'resolution_policy'):
            self.result()


if __name__ == '__main__':
    unittest.main()
