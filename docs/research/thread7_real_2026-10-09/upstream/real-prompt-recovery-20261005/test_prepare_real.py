"""Controlled mechanism fixtures only; never model experiments or source gold."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import prepare_real as r

ROOT = Path(__file__).parent
PROTOCOL = r.decode((ROOT / 'protocol.json').read_bytes())
DEPS = r.decode((ROOT / 'dependencies.json').read_bytes())
UPSTREAM = r.module(ROOT / DEPS['upstream']['path'], DEPS['upstream']['sha256'], 'fixture_upstream')
DESIGN = r.decode(r.bound(ROOT / DEPS['design']['path'], DEPS['design']['sha256']))


def fixture():
    nodes = []
    for i, (role, text) in enumerate([('user', 'Zażółć fixture: A implies B.'),
                                    ('assistant', 'Fixture reply, not user adoption.'),
                                    ('user', 'I explicitly deny A implies B.')]):
        native = {'id': 'fixture-native-' + str(i), 'author': {'role': role},
                  'content': {'content_type': 'text', 'parts': [text]}}
        nodes.append({'node_id': 'node-' + str(i), 'parent_node_id': 'node-' + str(i-1) if i else None,
                      'turn_id': 't' + str(i), 'role': role, 'source_create_time': 1770000000 + i,
                      'native_message': native, 'source_message_sha256': r.digest(r.canonical(native))})
    graph = {'schema': 'loom.real_source_intake_private/1', 'cases': [
        {'case_id': 'fixture', 'source_hashes': {'original_canonical_sha256': 'a' * 64},
         'view': {'nodes': nodes, 'current_path_node_ids': [n['node_id'] for n in nodes],
                  'topological_turn_ids': [n['turn_id'] for n in nodes]}}]}
    text = nodes[0]['native_message']['content']['parts'][0]
    quote = 'A implies B.'; start = text.index(quote); end = start + len(quote)
    evidence = {'turn_id': 't0', 'source_message_sha256': nodes[0]['source_message_sha256'],
                'quote': quote, 'start_character': start, 'end_character': end,
                'start_utf8': len(text[:start].encode()), 'end_utf8': len(text[:end].encode())}
    q = {'id': 'fixture-q0', 'case_id': 'fixture', 'speaker': 'user', 'as_of_turn_id': 't1',
         'proposition': {'subject': 'A', 'predicate': 'implies', 'object': 'B'},
         'gold': {'label': 'supported', 'evidence': [evidence], 'rationale': 'Controlled fixture reference.'}}
    annotation = {'schema': 'loom.real_source_reference_annotations/1',
                  'review_status': PROTOCOL['reference_status'], 'independent_review': False,
                  'questions': [q]}
    return graph, annotation


def no_attempt_bundle(directory):
    mp = directory / 'prepared/manifest.json'; m = r.decode(mp.read_bytes())
    requests = []
    for op in m['operations']:
        meta = op['metadata']
        requests.append({**{k: op[k] for k in ('operation_id', 'request_sha256', 'route_id', 'model_id', 'provider_id')},
                         'body': r.decode((mp.parent / op['request_file']).read_bytes()),
                         'metadata': {k: meta.get(k) for k in ('arm_id', 'prepared_request_id', 'source_manifest_sha256')}})
    return r.canonical({'schema': 'loom.programme_results/1', 'programme_id': m['programme_id'],
                        'stage_id': m['stage_id'], 'manifest_sha256': r.digest(mp.read_bytes()),
                        'planned_operation_ids': [op['operation_id'] for op in m['operations']],
                        'planned_operations': len(m['operations']), 'saved_row_count': 0,
                        'rows': [], 'responses': [], 'requests': requests})


class SourceTests(unittest.TestCase):
    def setUp(self):
        self.graph, self.annotation = fixture()

    def compile(self):
        return r.compile_sources(self.graph, self.annotation, PROTOCOL)

    def blocked(self, code):
        with self.assertRaisesRegex(r.PreparationError, '^' + code + '$'):
            self.compile()

    def test_exact_sources_preserved_without_mutating_inputs(self):
        before = r.canonical([self.graph, self.annotation]); ins, gold, payload = self.compile()
        self.assertEqual(before, r.canonical([self.graph, self.annotation]))
        self.assertEqual(len(ins['cases'][0]['turns']), 3)
        self.assertEqual([t['id'] for t in payload[0]['turns']], ['t0', 't1'])
        self.assertNotIn('gold', payload[0]['query'])

    def test_future_turn_excluded(self):
        _, _, payloads = self.compile()
        self.assertNotIn('explicitly deny', payloads[0]['turns'][-1]['text'])
        self.assertNotIn('t2', [t['id'] for t in payloads[0]['turns']])

    def test_future_evidence_rejected(self):
        self.annotation['questions'][0]['gold']['evidence'][0]['turn_id'] = 't2'
        self.blocked('future_evidence_not_visible')

    def test_attribution_mismatch_rejected(self):
        self.annotation['questions'][0]['speaker'] = 'assistant'
        self.blocked('reference_speaker_mismatch')

    def test_unknown_is_not_refutation(self):
        self.annotation['questions'][0]['gold'] = {'label': 'unknown', 'evidence': [], 'rationale': 'Fixture.'}
        self.assertEqual(self.compile()[1]['cases'][0]['judgments'][0]['label'], 'unknown')

    def test_positive_reference_requires_quote(self):
        self.annotation['questions'][0]['gold']['evidence'] = []
        self.blocked('decisive_evidence_required')

    def test_changed_quote_rejected(self):
        self.annotation['questions'][0]['gold']['evidence'][0]['quote'] = 'B implies A.'
        self.blocked('reference_quote_span_changed')

    def test_wrong_utf8_offset_rejected(self):
        self.annotation['questions'][0]['gold']['evidence'][0]['start_utf8'] -= 1
        self.blocked('reference_utf8_span_changed')

    def test_changed_quote_message_hash_rejected(self):
        self.annotation['questions'][0]['gold']['evidence'][0]['source_message_sha256'] = 'b' * 64
        self.blocked('reference_message_hash_changed')

    def test_duplicate_queries_rejected(self):
        self.annotation['questions'] *= 2
        self.blocked('duplicate_or_invalid_query_id')

    def test_unknown_question_case_rejected(self):
        self.annotation['questions'][0]['case_id'] = 'missing'
        self.blocked('unknown_question_case')

    def test_unknown_asof_rejected(self):
        self.annotation['questions'][0]['as_of_turn_id'] = 'missing'
        self.blocked('unknown_as_of_turn')

    def test_unknown_speaker_rejected(self):
        self.annotation['questions'][0]['speaker'] = 'missing'
        self.blocked('unknown_attributed_speaker')

    def test_branched_graph_not_flattened(self):
        self.graph['cases'][0]['view']['current_path_node_ids'].pop()
        self.blocked('branched_source_requires_graph_aware_adapter')

    def test_wrong_parent_order_rejected(self):
        self.graph['cases'][0]['view']['nodes'][2]['parent_node_id'] = 'node-0'
        self.blocked('source_parent_order_changed')

    def test_unknown_time_not_invented(self):
        self.graph['cases'][0]['view']['nodes'][0]['source_create_time'] = None
        self.blocked('unknown_source_time')

    def test_equal_times_not_arbitrarily_ordered(self):
        self.graph['cases'][0]['view']['nodes'][1]['source_create_time'] = 1770000000
        self.blocked('non_strict_source_chronology')

    def test_inverted_time_rejected(self):
        self.graph['cases'][0]['view']['nodes'][1]['source_create_time'] = 1769999999
        self.blocked('non_strict_source_chronology')

    def test_native_message_tamper_rejected(self):
        self.graph['cases'][0]['view']['nodes'][0]['native_message']['content']['parts'][0] = 'changed'
        self.blocked('native_hash_changed')

    def test_native_role_mismatch_rejected(self):
        self.graph['cases'][0]['view']['nodes'][0]['role'] = 'assistant'
        self.blocked('source_speaker_changed')

    def test_nontext_content_explicitly_not_interpreted(self):
        node = self.graph['cases'][0]['view']['nodes'][2]
        node['native_message']['content']['parts'].append({'image': 'fixture-image'})
        node['source_message_sha256'] = r.digest(r.canonical(node['native_message']))
        turns = self.compile()[0]['cases'][0]['turns']
        self.assertEqual(turns[2]['projection']['nontext_parts_not_interpreted'], 1)
        self.assertNotIn('fixture-image', turns[2]['text'])

    def test_credentials_require_review_not_silent_redaction(self):
        node = self.graph['cases'][0]['view']['nodes'][2]
        node['native_message']['content']['parts'] = ['sk-' + 'X' * 25]
        node['source_message_sha256'] = r.digest(r.canonical(node['native_message']))
        self.blocked('credential_like_source_requires_review')

    def test_independent_review_not_invented(self):
        self.annotation['independent_review'] = True
        self.blocked('independent_review_not_established')

    def test_invalid_reference_label_rejected(self):
        self.annotation['questions'][0]['gold']['label'] = 'probably'
        self.blocked('invalid_reference_label')

    def test_reference_rationale_required(self):
        self.annotation['questions'][0]['gold']['rationale'] = ''
        self.blocked('reference_rationale_required')

    def test_directed_relation_not_lost(self):
        self.assertEqual(self.compile()[2][0]['query']['proposition'],
                         {'subject': 'A', 'predicate': 'implies', 'object': 'B'})


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name); self.graph, self.annotation = fixture()

    def prepare(self, name='bundle', upstream=UPSTREAM):
        return r.prepare_bundle(graph=self.graph, annotation=self.annotation, protocol=PROTOCOL,
                                original_design=DESIGN, upstream=upstream, out=self.root/name,
                                provenance={k: 'a' * 64 for k in ('capsule','graph_projection','annotations','protocol','producer','intake','upstream','upstream_design')})

    def test_factorial_and_original_algorithm_preserved(self):
        self.prepare(); root = self.root/'bundle'; d = r.decode((root/'design.json').read_bytes())
        for k in ('prompt_common','prompt_strategies','temperatures','max_tokens','user_preferences','request_defaults','units_presets','scoring'):
            self.assertEqual(d[k], DESIGN[k])
        m = r.decode((root/'prepared/manifest.json').read_bytes())
        self.assertEqual(len(m['operations']), 16)
        self.assertEqual(len({o['operation_id'] for o in m['operations']}), 16)

    def test_output_refuses_overwrite(self):
        self.prepare()
        before = (self.root/'bundle/FREEZE.json').read_bytes()
        with self.assertRaisesRegex(r.PreparationError, 'output_already_exists'): self.prepare()
        self.assertEqual(before, (self.root/'bundle/FREEZE.json').read_bytes())

    def test_private_output_refused_inside_git(self):
        (self.root/'.git').mkdir()
        with self.assertRaisesRegex(r.PreparationError, 'private_output_inside_git'): self.prepare()

    def test_private_permissions(self):
        self.prepare()
        for p in (self.root/'bundle').rglob('*'):
            self.assertEqual(p.stat().st_mode & 0o077, 0)

    def test_exact_freeze_inventory(self):
        self.prepare(); root=self.root/'bundle'; f=r.decode((root/'FREEZE.json').read_bytes())
        self.assertEqual(set(f['files']), {str(p.relative_to(root)) for p in root.rglob('*') if p.is_file()}-{'FREEZE.json'})
        for name, expected in f['files'].items(): self.assertEqual(r.digest((root/name).read_bytes()), expected)

    def test_deterministic_bytes_across_output_directories(self):
        a=self.prepare('one');b=self.prepare('two');self.assertEqual(a,b)
        self.assertEqual((self.root/'one/FREEZE.json').read_bytes(),(self.root/'two/FREEZE.json').read_bytes())

    def test_source_views_cannot_be_mistaken_for_provider_operations(self):
        self.prepare();m=r.decode((self.root/'bundle/source-views/manifest.json').read_bytes())
        self.assertEqual(m['schema'],'loom.real_query_source_views/1');self.assertTrue(m['not_executable'])
        for op in m['operations']: self.assertNotIn('route_id',op)

    def test_failure_does_not_publish_partial_bundle(self):
        with patch.object(UPSTREAM,'prepare',side_effect=RuntimeError('fixture failure')):
            with self.assertRaises(RuntimeError):self.prepare()
        self.assertFalse((self.root/'bundle').exists())
        self.assertEqual(list(self.root.iterdir()), [])

    def test_public_receipt_does_not_copy_private_content(self):
        receipt=self.prepare();encoded=r.canonical(receipt).decode()
        for canary in ('Zażółć','fixture-q0','A implies B','source_' + 'a' * 64,'t0','Controlled fixture reference.'):
            self.assertNotIn(canary,encoded)
        self.assertFalse(receipt['dispatch_ready']);self.assertIsNone(receipt['model_quality'])

    def test_no_attempt_scoring_preserves_null_quality(self):
        self.prepare();root=self.root/'bundle';raw=no_attempt_bundle(root)
        score=r.score_real(UPSTREAM,root/'design.json',root/'prepared/manifest.json',raw,protocol=PROTOCOL,
                           expected_design_sha256=r.digest((root/'design.json').read_bytes()),
                           expected_manifest_sha256=r.digest((root/'prepared/manifest.json').read_bytes()),
                           expected_bundle_sha256=r.digest(raw),observed_on='2026-10-05')
        self.assertEqual(score['planned_operations'],16);self.assertEqual(score['first_rows'],0)
        self.assertNotIn('Authored visible synthetic',score['evidence_boundary'])
        metrics=r.shared_metrics_real(UPSTREAM,score,source_id='fixture-score',protocol=PROTOCOL)
        for c in score['configurations']:
            self.assertIsNone(c['label_match_all_planned']);self.assertEqual(c['unknown_cost_operations'],0)
            self.assertEqual(c['total_cost_usd'],'0')
        for m in metrics.values():self.assertNotIn('Two authored families',m['source_gold_label_match_all_planned']['note'])

    def test_private_values_cannot_enter_public_provenance(self):
        with self.assertRaisesRegex(r.PreparationError, 'invalid_public_hash_provenance'):
            r.prepare_bundle(graph=self.graph, annotation=self.annotation, protocol=PROTOCOL,
                             original_design=DESIGN, upstream=UPSTREAM, out=self.root/'bad',
                             provenance={'source_title': 'private canary'})
        self.assertFalse((self.root/'bad').exists())

    def test_hash_binding_rejects_tampered_request(self):
        self.prepare();root=self.root/'bundle';raw=no_attempt_bundle(root)
        file=next((root/'prepared/requests').iterdir());file.write_text('{}')
        with self.assertRaises(UPSTREAM.ScoringError):
            r.score_real(UPSTREAM,root/'design.json',root/'prepared/manifest.json',raw,protocol=PROTOCOL,
                         expected_design_sha256=r.digest((root/'design.json').read_bytes()),
                         expected_manifest_sha256=r.digest((root/'prepared/manifest.json').read_bytes()),
                         expected_bundle_sha256=r.digest(raw),observed_on='2026-10-05')


class ParseTests(unittest.TestCase):
    def test_duplicate_json_keys_fail(self):
        with self.assertRaisesRegex(r.PreparationError,'duplicate_json_key'):r.decode(b'{"a":1,"a":2}')
    def test_nonfinite_json_fails(self):
        for raw in (b'NaN',b'Infinity',b'1e999'):
            with self.subTest(raw=raw):
                with self.assertRaisesRegex(r.PreparationError,'nonfinite_json_number'):r.decode(raw)
    def test_invalid_expected_hash(self):
        with self.assertRaisesRegex(r.PreparationError,'invalid_expected_hash'):r.bound(ROOT/'protocol.json','not-hash')
    def test_dependency_tamper_fails(self):
        with self.assertRaisesRegex(r.PreparationError,'input_hash_changed'):r.module(ROOT/'protocol.json','0'*64,'bad')
    def test_original_dependency_exact_bytes(self):
        raw=r.bound(ROOT/DEPS['upstream']['path'],DEPS['upstream']['sha256'])
        import hashlib
        self.assertEqual(hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest(),
                         '2e4823b52df897800896fd13fde40faf0f202fac')


if __name__ == '__main__': unittest.main()
