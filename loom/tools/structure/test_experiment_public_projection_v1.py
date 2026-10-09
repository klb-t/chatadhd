"""Public research gates. Controlled canaries only; no source or provider access."""
from contextlib import redirect_stderr, redirect_stdout
from copy import deepcopy
import gzip
import importlib.util
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

from loom.tools.seeding import method_graph as graph
from loom.tools.structure import experiment_analysis_v1 as analysis

ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT/'docs/research/thread7_real_2026-10-09'
PREVIOUS = BASE/'continuation_01'
CURRENT = BASE/'continuation_02'
CANARY = 'controlled-nonpublic-fixture'


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read(path):
    return graph.strict_json(path.read_bytes())


def review():
    return read(analysis.PUBLIC_INPUT_REVIEW)


class PublicProjectionTests(unittest.TestCase):
    def setUp(self):
        self.presentation = read(PREVIOUS/'handoff-example.json')
        self.protocol = read(PREVIOUS/'evaluation-protocol.json')
        self.projection = graph.load_projection(PREVIOUS/'handoff-projection.json')

    def artifact(self, **overrides):
        values = dict(presentation=self.presentation, protocol=self.protocol,
                      projection=self.projection, projected_at='2026-10-09T00:00:00Z')
        return analysis.build_handoff_artifact(**{**values, **overrides})

    def assert_private_rejection(self, callback):
        try:
            callback()
        except ValueError as exc:
            self.assertEqual(str(exc), 'public_projection_rejected')
            self.assertIsNone(exc.__cause__)
            self.assertIsNone(exc.__context__)
            rendered = io.StringIO()
            with redirect_stderr(rendered):
                sys.excepthook(type(exc), exc, exc.__traceback__)
            self.assertNotIn(CANARY, rendered.getvalue())
        else:
            self.fail('Expected public rejection')

    def test_approved_role_values_and_source_bytes_are_preserved(self):
        original = deepcopy(self.presentation)
        artifact = self.artifact()
        files = graph.recover_files(artifact)
        self.assertEqual(files['policy'], graph.canonical(self.protocol))
        self.assertEqual(files['projection-profile-bytes'], self.projection.source_bytes)
        self.assertEqual(graph.strict_json(files['results'])['records'], [original])
        self.assertEqual(self.presentation, original)

    def test_unknown_top_level_and_nested_protocol_metadata_rejected(self):
        for target in ('root', 'source_binding', 'metrics'):
            value = deepcopy(self.protocol)
            node = value if target == 'root' else value[target]
            if isinstance(node, list): node = node[0]
            node['unknown_auxiliary'] = {'private': CANARY}
            with self.subTest(target=target):
                self.assert_private_rejection(lambda: self.artifact(protocol=value))

    def test_invalid_protocol_source_digest_cannot_hide_private_object(self):
        value = deepcopy(self.protocol)
        value['source_binding']['expanded_panel_freeze_sha256'] = {'private': CANARY}
        self.assert_private_rejection(lambda: self.artifact(protocol=value))

    def test_unknown_projection_fields_rejected_at_each_depth(self):
        for target in ('root', 'method', 'support'):
            value = deepcopy(self.projection)
            node = value if target == 'root' else value[target]
            node['unknown_auxiliary'] = CANARY
            with self.subTest(target=target):
                self.assert_private_rejection(lambda: self.artifact(projection=value))

    def test_projection_source_bytes_cannot_bypass_approved_mapping(self):
        value = deepcopy(self.projection)
        hidden = dict(value)
        hidden['unknown_auxiliary'] = CANARY
        value.source_bytes = graph.canonical(hidden)
        self.assert_private_rejection(lambda: self.artifact(projection=value))

    def test_presentations_reject_unknown_and_known_field_payloads(self):
        for field in ('extra', 'sha256', 'count', 'summary'):
            value = deepcopy(self.presentation)
            if field == 'extra': value['unknown_auxiliary'] = CANARY
            elif field == 'sha256': value['evidence'][0]['sha256'] = {'private': CANARY}
            elif field == 'count': value['expert']['preparation_count'] = CANARY
            else: value['basic']['summary_pl'] = CANARY
            with self.subTest(field=field):
                self.assert_private_rejection(lambda: self.artifact(presentation=value))

    def test_role_rebinding_is_rejected(self):
        self.assert_private_rejection(lambda: analysis.approve_public_input('analysis_projection', self.protocol))

    def test_descriptor_rejects_unknown_top_level_or_nested_metadata(self):
        for target in ('root', 'role'):
            value = review()
            node = value if target == 'root' else value['roles']['analysis_protocol']
            node['unknown_auxiliary'] = CANARY
            with self.subTest(target=target):
                self.assert_private_rejection(lambda: analysis.validate_public_review(value))

    def test_descriptor_rejects_nonhash_hash_values(self):
        for invalid in (CANARY, {'private': CANARY}, None, 3, 'g'*64):
            value = review()
            value['roles']['analysis_protocol']['sha256'] = [invalid]
            self.assert_private_rejection(lambda: analysis.validate_public_review(value))

    def test_descriptor_rejects_duplicate_hashes_and_unknown_encoding(self):
        value = review()
        value['roles']['analysis_protocol']['sha256'] *= 2
        self.assert_private_rejection(lambda: analysis.validate_public_review(value))
        value = review()
        value['roles']['analysis_protocol']['encoding'] = CANARY
        self.assert_private_rejection(lambda: analysis.validate_public_review(value))

    def test_bad_timestamp_is_private_rejection(self):
        self.assert_private_rejection(lambda: self.artifact(projected_at=CANARY))

    def test_nested_exception_instance_and_context_are_not_public(self):
        @analysis.public_error_boundary
        def broken():
            raise KeyError(CANARY)
        self.assert_private_rejection(broken)

    def prepare_original(self, directory):
        for name in ('graph-projection.json', 'workflow-preparation-receipt.json', 'workflow-plan.json', 'HANDOFF_B.md'):
            shutil.copyfile(BASE/name, directory/name)
        return load('test_public_original', BASE/'build_public_artifact.py')

    def test_original_builder_legitimate_approved_bytes_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            builder = self.prepare_original(directory)
            builder.build(directory)
            artifact = graph.strict_json(gzip.decompress((directory/'public-workflow-artifact.json.gz').read_bytes()))
            files = graph.recover_files(artifact)
            self.assertEqual(files['policy'], (BASE/'workflow-plan.json').read_bytes())
            self.assertEqual(files['protocol'], (BASE/'HANDOFF_B.md').read_bytes())

    def test_original_builder_nested_receipt_hash_rejected_before_writes(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            builder = self.prepare_original(directory)
            value = read(directory/'workflow-preparation-receipt.json')
            value[0]['spec_sha256'] = {'private': CANARY}
            (directory/'workflow-preparation-receipt.json').write_bytes(graph.canonical(value))
            self.assert_private_rejection(lambda: builder.build(directory))
            self.assertFalse((directory/'public-workflow-artifact.json.gz').exists())
            self.assertFalse((directory/'contract-verification.json').exists())

    def test_final_builder_rejects_before_any_artifact_file_is_created(self):
        names = ('handoff-example.json', 'handoff-contract.json', 'context-expanded-v2-receipt.json',
                 'connection-receipt.json', 'FINAL_TESTS.json', 'evaluation-protocol.json', 'handoff-projection.json')
        builder = load('test_public_final', PREVIOUS/'build_final_handoff.py')
        for mutation in ('handoff-example.json', 'evaluation-protocol.json', 'handoff-projection.json'):
            with tempfile.TemporaryDirectory() as folder:
                directory = Path(folder)
                for name in names: shutil.copyfile(PREVIOUS/name, directory/name)
                value = read(directory/mutation)
                value['unknown_auxiliary'] = CANARY
                (directory/mutation).write_bytes(graph.canonical(value))
                with patch.object(builder, '__file__', str(directory/'build_final_handoff.py')):
                    self.assert_private_rejection(builder.main)
                self.assertFalse(list(directory.glob('handoff-final-*')))

    def test_delivery_policy_is_not_self_authorizing(self):
        builder = load('test_public_delivery', CURRENT/'delivery-build.py')
        for mutation in ('basic', 'input_hash', 'extra'):
            policy = read(CURRENT/'delivery-policy.json')
            if mutation == 'basic': policy['basic']['summary_pl'] = CANARY
            elif mutation == 'input_hash': policy['inputs'][0]['sha256'] = 'a'*64
            else: policy['unknown_auxiliary'] = CANARY
            self.assert_private_rejection(lambda: builder.build(policy))

    def test_current_delivery_approved_result_semantics_unchanged(self):
        builder = load('test_public_delivery_control', CURRENT/'delivery-build.py')
        presentation, contract, artifact = builder.build(read(CURRENT/'delivery-policy.json'))
        self.assertEqual(presentation, read(CURRENT/'delivery-example-v3.json'))
        self.assertEqual(contract, read(CURRENT/'delivery-contract-v3.json'))
        self.assertEqual(graph.strict_json(graph.recover_files(artifact)['results'])['records'], [presentation])

    def test_delivery_consumes_approved_bytes_despite_later_file_change(self):
        builder = load('test_public_delivery_race', CURRENT/'delivery-build.py')
        policy = read(CURRENT/'delivery-policy.json')
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)/'continuation_02'
            previous = Path(folder)/'continuation_01'
            for item in policy['inputs']:
                target = base/item['path']
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(CURRENT/item['path'], target)
            original_approve = analysis.approve_public_input
            changed = []
            def approve_then_change(role, value):
                result = original_approve(role, value)
                if role == 'continuation02/'+policy['inputs'][-1]['path'] and not changed:
                    target = previous/'handoff-final-contract-v2.json'
                    contract = read(target)
                    contract['$comment'] = CANARY
                    target.write_bytes(graph.canonical(contract))
                    changed.append(True)
                return result
            with patch.object(builder, 'BASE', base), patch.object(builder, 'PREVIOUS', previous), \
                 patch.object(analysis, 'approve_public_input', approve_then_change):
                presentation, contract, artifact = builder.build(policy)
            self.assertTrue(changed)
            self.assertEqual(contract, read(CURRENT/'delivery-contract-v3.json'))
            self.assertNotIn(CANARY, graph.canonical(contract).decode())
            self.assertEqual(presentation, read(CURRENT/'delivery-example-v3.json'))
            self.assertNotIn(CANARY, graph.canonical(artifact).decode())


if __name__ == '__main__':
    unittest.main()
