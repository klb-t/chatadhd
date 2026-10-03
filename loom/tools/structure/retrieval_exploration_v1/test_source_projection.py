"""Active boundary contract and read-only parity with historical DEV receipts."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile

from . import source_projection as active
from .actor_projection_v1 import test_actor_projection_v2 as legacy

HERE = Path(__file__).resolve().parent


class LegacyContractTests(legacy.ActorProjectionTests):
    """Run all 27 original v2 mechanism assertions against the active adapter."""
    def setUp(self):
        replacement = SimpleNamespace(project=active.project, HERE=legacy.actor.HERE)
        self.adapter_patch = patch.object(legacy, 'actor', replacement)
        self.adapter_patch.start()
        self.addCleanup(self.adapter_patch.stop)
        super().setUp()


class SourceBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.document, self.observation = legacy.toy()
        self.policy = json.loads(active.DEFAULT_POLICY.read_bytes())
        self.message = self.document['mapping']['turn-a']['message']

    def project(self, raw=None):
        raw = json.dumps(self.document, ensure_ascii=True).encode() if raw is None else raw
        observation = copy.deepcopy(self.observation)
        observation['locator']['source'] = 'sha256:' + hashlib.sha256(raw).hexdigest()
        return active.project(observation, raw, self.policy)

    def rename_turn(self, turn_id, pointer_token):
        self.document['mapping'][turn_id] = self.document['mapping'].pop('turn-a')
        self.message['id'] = turn_id
        self.message['metadata']['loom_source_turn_id'] = turn_id
        self.observation['locator']['json_pointer'] = '/mapping/' + pointer_token + '/message/content/parts/0'

    def test_invalid_pointer_escape_is_not_an_exact_binding(self):
        self.rename_turn('turn~2a', 'turn~2a')
        self.assertEqual(self.project()['reason'], 'invalid_source_pointer')

    def test_valid_escaped_slash_tilde_and_empty_token_keep_literal_identity(self):
        for turn_id in ('part/one~two', '~1', 'space here', 'zażółć', ''):
            with self.subTest(turn_id=turn_id):
                self.setUp()
                self.rename_turn(turn_id, turn_id.replace('~', '~0').replace('/', '~1'))
                out = self.project()
                self.assertEqual(out['state'], 'bound_source_projection')
                expected = 'resolved' if turn_id else 'invalid_type_or_empty'
                self.assertEqual(out['fields']['source_turn_id']['state'], expected)
                if turn_id:
                    self.assertEqual(out['fields']['source_turn_id']['value'], turn_id)

    def test_duplicate_actor_key_abstains_without_erasing_role_or_quote(self):
        raw = json.dumps(self.document).encode().replace(b'"name": "Aria"', b'"name": "Bela", "name": "Aria"')
        out = self.project(raw)
        self.assertEqual(out['state'], 'bound_source_projection')
        self.assertEqual(out['fields']['source_actor_label'], {'state': 'ambiguous_source_fields', 'value': None})
        self.assertEqual(out['fields']['transport_role']['value'], 'user')
        self.assertEqual(out['fields']['source_id']['value'], 'source:alpha')

    def test_same_duplicate_values_still_have_no_unique_pointer(self):
        raw = json.dumps(self.document).encode().replace(b'"name": "Aria"', b'"name": "Aria", "name": "Aria"')
        self.assertEqual(self.project(raw)['fields']['source_actor_label']['state'], 'ambiguous_source_fields')

    def test_escaped_duplicate_json_key_cannot_hide_ambiguity(self):
        raw = json.dumps(self.document).encode().replace(b'"name": "Aria"', b'"na\\u006de": "Bela", "name": "Aria"')
        self.assertEqual(self.project(raw)['fields']['source_actor_label']['state'], 'ambiguous_source_fields')

    def test_duplicate_message_ancestor_makes_source_binding_ambiguous(self):
        raw = json.dumps(self.document).encode().replace(b'"message": {', b'"message": null, "message": {')
        self.assertEqual(self.project(raw)['reason'], 'ambiguous_source_binding')

    def test_duplicate_text_part_container_makes_quote_ambiguous(self):
        raw = json.dumps(self.document).encode().replace(b'"parts": [', b'"parts": [], "parts": [')
        self.assertEqual(self.project(raw)['reason'], 'ambiguous_source_binding')

    def test_unrelated_duplicate_key_does_not_veto_source_evidence(self):
        raw = json.dumps(self.document).encode().replace(b'"mapping": {', b'"unused": 1, "unused": 2, "mapping": {')
        self.assertEqual(self.project(raw)['fields']['source_actor_label']['value'], 'Aria')

    def test_scalar_parent_is_invalid_field_structure_not_missing(self):
        self.message['author'] = False
        out = self.project()
        self.assertEqual(out['state'], 'bound_source_projection')
        self.assertEqual(out['fields']['source_actor_label']['state'], 'invalid_source_structure')
        self.assertEqual(out['fields']['transport_role']['state'], 'invalid_source_structure')
        self.assertEqual(out['fields']['source_turn_id']['value'], 'turn-a')

    def test_duplicate_epoch_abstains_only_for_source_known_at(self):
        raw = json.dumps(self.document).encode().replace(b'"create_time": ', b'"create_time": 0, "create_time": ')
        out = self.project(raw)
        self.assertEqual(out['fields']['source_known_at_statement']['state'], 'ambiguous_source_fields')
        self.assertEqual(out['fields']['source_actor_label']['value'], 'Aria')

    def test_bool_epoch_is_not_a_timestamp_even_if_it_matches(self):
        self.message['create_time'] = True
        self.message['metadata']['loom_source_known_at'] = '1970-01-01T00:00:01Z'
        self.observation['date'] = '1970-01-01T00:00:01Z'
        self.assertEqual(self.project()['fields']['source_known_at_statement']['state'], 'invalid_date')

    def test_huge_epoch_and_platform_failure_are_explicit(self):
        self.message['create_time'] = 10 ** 400
        self.assertEqual(self.project()['fields']['source_known_at_statement']['state'], 'invalid_date')

    def test_non_json_numeric_constants_are_not_silently_decoded(self):
        for value in (b'NaN', b'Infinity', b'-Infinity'):
            with self.subTest(value=value):
                raw = json.dumps(self.document).encode().replace(b'"mapping": {', b'"unused": ' + value + b', "mapping": {')
                self.assertEqual(self.project(raw)['reason'], 'source_binding_decode_failure')

    def test_truncated_or_non_utf_json_is_an_unavailable_projection(self):
        for raw in (b'{', b'\xff', b'null', b'[]'):
            with self.subTest(raw=raw):
                self.assertEqual(self.project(raw)['state'], 'unavailable')

    def test_lone_surrogate_text_is_an_unavailable_projection(self):
        self.message['content']['parts'][0] = '\ud800'
        self.observation['text'] = '\ud800'
        self.assertEqual(self.project()['reason'], 'source_binding_decode_failure')

    def test_lone_surrogate_actor_abstains_for_that_field(self):
        self.message['author']['name'] = '\ud800'
        self.assertEqual(self.project()['fields']['source_actor_label']['state'], 'invalid_unicode')

    def test_mapping_and_content_parts_container_types_are_verified(self):
        self.document['mapping'] = [self.document['mapping']['turn-a']]
        self.observation['locator']['json_pointer'] = '/mapping/0/message/content/parts/0'
        self.assertEqual(self.project()['reason'], 'source_binding_decode_failure')
        self.setUp()
        self.message['content']['parts'] = {'0': self.observation['text']}
        self.assertEqual(self.project()['reason'], 'source_binding_decode_failure')

    def test_boolean_span_values_are_not_integer_byte_offsets(self):
        for key in ('byte_start', 'byte_len'):
            with self.subTest(key=key):
                self.setUp()
                self.observation['locator'][key] = False
                self.assertEqual(self.project()['reason'], 'invalid_text_or_utf8_bounds')

    def test_empty_span_inside_utf8_codepoint_is_not_a_valid_binding(self):
        self.observation['text'] = ''
        self.observation['locator']['byte_start'] = 1
        self.observation['locator']['byte_len'] = 0
        self.assertEqual(self.project()['reason'], 'invalid_utf8_span_boundary')

    def test_empty_span_at_valid_boundary_remains_available(self):
        for position in (0, len('Ż'.encode()), len(self.observation['text'].encode())):
            with self.subTest(position=position):
                self.observation['text'] = ''
                self.observation['locator']['byte_start'] = position
                self.observation['locator']['byte_len'] = 0
                self.assertEqual(self.project()['state'], 'bound_source_projection')

    def test_oversized_array_index_is_an_unknown_field_not_conversion_failure(self):
        self.message['extra'] = []
        self.policy['actor_paths_relative_to_message'] = ['/extra/' + '9' * 5000]
        self.assertEqual(self.project()['fields']['source_actor_label']['state'], 'unknown')
        self.message['extra'] = ['Aria']
        self.assertEqual(self.project()['fields']['source_actor_label']['state'], 'unknown')

    def test_recipe_cannot_relabel_source_record_as_verified_identity(self):
        for key, value in (('identity_semantics', 'verified_world_identity'),
                           ('actor_epistemic_status', 'independently_verified')):
            with self.subTest(key=key):
                self.policy = json.loads(active.DEFAULT_POLICY.read_bytes())
                self.policy[key] = value
                with self.assertRaisesRegex(ValueError, 'evidence_semantics'):
                    self.project()

    def test_custom_metadata_paths_remain_recipe_data(self):
        self.message['extra'] = {'actor': 'Cora'}
        self.policy['actor_paths_relative_to_message'] = ['/extra/actor']
        self.assertEqual(self.project()['fields']['source_actor_label']['value'], 'Cora')

    def test_malformed_policy_path_and_unknown_format_fail_explicitly(self):
        self.policy['actor_paths_relative_to_message'] = ['/actor~7/name']
        with self.assertRaises(active.InvalidPointer):
            self.project()
        self.policy = json.loads(active.DEFAULT_POLICY.read_bytes())
        self.policy['supported_source_format'] = 'anthropic'
        with self.assertRaisesRegex(ValueError, 'unsupported_source_format'):
            self.project()

    def test_batch_decodes_one_source_once_and_preserves_order(self):
        raw = json.dumps(self.document).encode()
        one = copy.deepcopy(self.observation)
        one['locator']['source'] = 'sha256:' + hashlib.sha256(raw).hexdigest()
        two = copy.deepcopy(one)
        two['id'] = 'second'
        with patch.object(active.json, 'loads', wraps=json.loads) as loads:
            result = active.project_many([one, two], raw, self.policy)
        self.assertEqual(loads.call_count, 1)
        self.assertEqual([row['observation_id'] for row in result], [one['id'], 'second'])

    def test_batch_preserves_distinct_source_namespace_and_hash_mismatch(self):
        raw = json.dumps(self.document).encode()
        first = copy.deepcopy(self.observation)
        first['locator']['source'] = 'sha256:' + hashlib.sha256(raw).hexdigest()
        second = copy.deepcopy(first)
        second['locator']['source'] = 'sha256:' + '0' * 64
        rows = active.project_many([first, second], raw, self.policy)
        self.assertEqual(rows[0]['state'], 'bound_source_projection')
        self.assertEqual(rows[1]['reason'], 'raw_source_hash_mismatch')


class HistoricalParityTests(unittest.TestCase):
    def test_all_84_native_projection_objects_match_saved_v2_receipt(self):
        policy = json.loads(active.DEFAULT_POLICY.read_bytes())
        with zipfile.ZipFile(HERE / 'actor_projection_v1/first_evidence.zip') as actors:
            expected = json.loads(actors.read('second_projection.json'))['projections']
        actual = []
        prefix = 'loom/tools/structure/retrieval_exploration_v1/native_source_free_v1/'
        with zipfile.ZipFile(HERE / 'native_source_free_v1/first_evidence.zip') as sources:
            ledger = json.loads(sources.read('first_run_ledger2.json'))
            for row in ledger['rows']:
                if row['state'] != 'completed':
                    continue
                raw = sources.read(row['input_path'].removeprefix(prefix))
                self.assertEqual(hashlib.sha256(raw).hexdigest(), row['input_sha256'])
                snapshot = json.loads(sources.read(row['output_directory'].removeprefix(prefix) + '/native_snapshot.json'))
                projections = active.project_many(snapshot['bodies']['loom_kb_observations'], raw, policy)
                actual.extend({'case_id': row['case_id'], 'arm': row['arm']} | value for value in projections)
        self.assertEqual(len(actual), 84)
        self.assertEqual(actual, expected)

    def test_all_12_control_projection_objects_match_saved_v2_receipt(self):
        policy = json.loads(active.DEFAULT_POLICY.read_bytes())
        prefix = 'loom/tools/structure/retrieval_exploration_v1/actor_projection_v1/'
        with zipfile.ZipFile(HERE / 'actor_projection_v1/first_evidence.zip') as archive:
            cases = json.loads(archive.read('second_controls.json'))['cases']
            self.assertEqual(len(cases), 12)
            for case in cases:
                with self.subTest(case=case['case_id']):
                    raw = archive.read(case['raw_source_file'].removeprefix(prefix))
                    self.assertEqual(active.project(case['actual_observation'], raw, policy), case['projection'])


class CommandLineTests(unittest.TestCase):
    def test_module_cli_accepts_snapshot_and_preserves_inputs_and_existing_output(self):
        document, observation = legacy.toy()
        raw = json.dumps(document).encode()
        observation['locator']['source'] = 'sha256:' + hashlib.sha256(raw).hexdigest()
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            source, snapshot, output = [folder / name for name in ('raw.json', 'snapshot.json', 'result.json')]
            source.write_bytes(raw)
            snapshot.write_text(json.dumps({'bodies': {'loom_kb_observations': [observation]}}))
            before = snapshot.read_bytes()
            command = [sys.executable, '-m', 'loom.tools.structure.retrieval_exploration_v1.source_projection',
                       '--raw-source', str(source), '--observations', str(snapshot), '--output', str(output)]
            result = subprocess.run(command, cwd=HERE.parents[3], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            parsed = json.loads(output.read_bytes())
            self.assertEqual(parsed['observations'], 1)
            self.assertEqual(parsed['projections'][0]['fields']['source_actor_label']['value'], 'Aria')
            self.assertEqual(source.read_bytes(), raw)
            self.assertEqual(snapshot.read_bytes(), before)
            saved = output.read_bytes()
            second = subprocess.run(command, cwd=HERE.parents[3], capture_output=True, text=True)
            self.assertNotEqual(second.returncode, 0)
            self.assertEqual(output.read_bytes(), saved)


if __name__ == '__main__':
    unittest.main()
