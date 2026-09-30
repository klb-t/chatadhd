"""Synthetic free-extraction mechanism checks; no model-quality or sealed tests."""
from copy import deepcopy
from decimal import Decimal
import hashlib
from pathlib import Path
import tempfile
import unittest

try:
    from . import graph_free_extraction as free, graph_panel_live as panel
    from . import graph_panel_score_run as integrity, openrouter_runner as safe
except ImportError:
    import graph_free_extraction as free, graph_panel_live as panel
    import graph_panel_score_run as integrity, openrouter_runner as safe


def case():
    return {'id': 'dev-free-mechanism', 'source_id': 'synthetic:free-mechanism', 'language': 'pl',
        'turns': [{'id': 't1', 'speaker': 'writer', 'known_at': '2026-08-10T09:00:00Z',
                   'text': 'Jeśli lampa nie świeci, żaba czeka.'}],
        'node_inventory': [{'id': 'A', 'text': 'lampa nie świeci', 'aliases': ['lampa pozostaje ciemna']},
                           {'id': 'B', 'text': 'żaba czeka', 'aliases': []},
                           {'id': 'C', 'text': 'lis śpi', 'aliases': []}],
        'judgment_queries': [{'id': 'never-in-body', 'label': 'never-in-body'}],
        'family': 'never-in-body', 'gold': 'never-in-body'}


def output():
    return {'nodes': [{'id': 'x', 'text': 'lampa nie świeci'}, {'id': 'y', 'text': 'żaba czeka'}],
        'source_assertions': [{'id': 'edge1', 'relation': 'implies', 'source': 'x', 'target': 'y',
            'polarity': 'positive', 'attributed_to': 'writer', 'known_at': '2026-08-10T09:00:00Z',
            'evidence': [{'turn_id': 't1'}]}], 'status_events': []}


def gold():
    c = case(); text = c['turns'][0]['text']
    return {'id': c['id'], 'family': 'synthetic', 'language': 'pl',
        'source_assertions': [{'id': 'g1', 'relation': 'implies', 'source': 'A', 'target': 'B',
            'polarity': 'positive', 'attributed_to': 'writer', 'known_at': '2026-08-10T09:00:00Z',
            'evidence': [{'source_id': c['source_id'], 'turn_id': 't1', 'quote': text,
                'coordinate_space': 'turn.text', 'char_start': 0, 'char_end': len(text),
                'byte_start': 0, 'byte_end': len(text.encode())}],
            'content_truth': 'unverified', 'basis_class': 'observed_source_assertion'}],
        'status_events': []}


class FreeMechanismTests(unittest.TestCase):
    def test_payload_uses_only_raw_turns_and_identity(self):
        c = case(); before = deepcopy(c)
        payload = free.source_payload(c)
        self.assertEqual(set(payload), {'id', 'source_id', 'turns'})
        row = free.requests([c])[0]
        self.assertEqual(safe.parse_json(row['body']['messages'][1]['content']), payload)
        self.assertNotIn('never-in-body', row['body']['messages'][1]['content'])
        self.assertNotIn('node_inventory', row['body']['messages'][1]['content'])
        self.assertEqual(row['body']['max_tokens'], 2048)
        self.assertEqual(row['body']['model'], panel.MODEL)
        self.assertEqual(row['body']['provider']['only'], [panel.PROVIDER])
        self.assertEqual(c, before)

    def test_compile_uses_model_nodes_and_full_exact_unicode_source(self):
        c, model = case(), output()
        compiled = free.compile_free(model, free.source_payload(c))
        edge = compiled['source_assertions'][0]
        self.assertEqual((edge['source'], edge['target']), ('x', 'y'))
        evidence = edge['evidence'][0]
        self.assertEqual(evidence['quote'], c['turns'][0]['text'])
        self.assertEqual(evidence['char_start'], 0)
        self.assertEqual(evidence['byte_end'], len(evidence['quote'].encode()))
        self.assertGreater(evidence['byte_end'], evidence['char_end'])
        self.assertEqual(edge['content_truth'], 'unverified')
        self.assertEqual(compiled['raw_model_object'], model)
        self.assertEqual(compiled['raw_model_object_sha256'], safe.digest(model))

    def test_fixed_alias_normalization_preserves_internal_negation_operators(self):
        self.assertEqual(free.normalize_alias('  ＬＡＭＰＡ\u00a0 NIE   ŚWIECI!  '), 'lampa nie świeci')
        self.assertNotEqual(free.normalize_alias('lampa świeci'), free.normalize_alias('lampa nie świeci'))
        self.assertNotEqual(free.normalize_alias('A implies B'), free.normalize_alias('B implies A'))
        self.assertEqual(free.normalize_alias('A AND NOT B.'), 'a and not b')
        self.assertEqual(free.normalize_alias('(A OR B)'), '(a or b)')

    def test_unique_alias_matches_no_semantic_best_choice(self):
        nodes = [{'id': 'x', 'text': 'lampa pozostaje ciemna.'}, {'id': 'y', 'text': 'żaba czeka'}]
        mapping, details = free.align_nodes(nodes, case()['node_inventory'])
        self.assertEqual(mapping, {'x': 'A', 'y': 'B'})
        self.assertTrue(all(d['alignment'] == 'unique' for d in details))
        inventory = deepcopy(case()['node_inventory'])
        inventory[1]['aliases'] = ['lampa pozostaje ciemna']
        mapping, details = free.align_nodes(nodes, inventory)
        self.assertIsNone(mapping['x'])
        self.assertEqual(details[0]['candidate_reference_ids'], ['A', 'B'])
        self.assertEqual(details[0]['alignment'], 'ambiguous')

    def test_unmatched_paraphrase_stays_unmatched_not_gold_selected(self):
        c, model = case(), output()
        model['nodes'][0]['text'] = 'the light remains dark'
        result = free.score_free([c], [gold()], [free.compile_free(model, free.source_payload(c))])
        self.assertEqual(result['strict_edges'], panel._metric(0, 1, 1))
        self.assertEqual(result['strict_reference_atom_alignment'], panel._metric(1, 1, 1))
        self.assertEqual(result['node_cases'][0]['unmapped_assertion_ids'], ['edge1'])
        self.assertTrue(result['semantic_alignment_review_required'])

    def test_aligned_reference_ids_enter_scoring_only_not_raw_graph(self):
        c = case(); compiled = free.compile_free(output(), free.source_payload(c)); before = deepcopy(compiled)
        result = free.score_free([c], [gold()], [compiled])
        self.assertEqual(result['strict_edges'], panel._metric(1, 0, 0))
        self.assertEqual(result['strict_reference_atom_alignment'], panel._metric(2, 0, 0))
        self.assertEqual(compiled, before)
        self.assertEqual(compiled['source_assertions'][0]['source'], 'x')
        self.assertIsNone(result['content_truth_accuracy'])

    def test_wrong_direction_polarity_actor_and_known_at_do_not_match(self):
        for change in ({'source': 'y', 'target': 'x'}, {'polarity': 'negative'},
                       {'attributed_to': 'other'}, {'known_at': '2026-08-10T09:01:00Z'}):
            c, model = case(), output(); model['source_assertions'][0].update(change)
            result = free.score_free([c], [gold()], [free.compile_free(model, free.source_payload(c))])
            self.assertEqual(result['strict_edges'], panel._metric(0, 1, 1))

    def test_unused_reference_control_nodes_do_not_inflate_gold_or_error(self):
        c, model = case(), output(); model['nodes'].append({'id': 'unused', 'text': 'lis śpi'})
        result = free.score_free([c], [gold()], [free.compile_free(model, free.source_payload(c))])
        self.assertEqual(result['strict_reference_atom_alignment'], panel._metric(2, 0, 0))
        self.assertEqual(result['node_cases'][0]['used_gold_reference_node_ids'], ['A', 'B'])
        self.assertEqual(len(result['node_cases'][0]['matched_unused_inventory_control_nodes']), 1)

    def test_duplicate_representation_is_node_diagnostic_but_duplicate_edge_fp(self):
        c, model = case(), output(); model['nodes'].append({'id': 'xx', 'text': 'lampa nie świeci.'})
        duplicate = deepcopy(model['source_assertions'][0]); duplicate.update(id='edge2', source='xx')
        model['source_assertions'].append(duplicate)
        result = free.score_free([c], [gold()], [free.compile_free(model, free.source_payload(c))])
        self.assertEqual(result['strict_reference_atom_alignment'], panel._metric(2, 0, 0))
        self.assertEqual(result['node_cases'][0]['duplicate_own_representations_of_used_reference_atoms'], 1)
        self.assertEqual(result['strict_edges'], panel._metric(1, 1, 0))

    def test_invalid_node_and_evidence_records_keep_false_positive_counts(self):
        c, model = case(), output(); model['nodes'][0]['truth'] = True
        compiled = free.compile_free(model, free.source_payload(c))
        self.assertEqual(compiled['invalid_nodes'], 1)
        self.assertEqual(compiled['invalid_assertions'], 1)
        result = free.score_free([c], [gold()], [compiled])
        self.assertEqual(result['strict_edges'], panel._metric(0, 1, 1))
        self.assertEqual(result['strict_reference_atom_alignment'], panel._metric(1, 1, 1))
        model = output(); model['source_assertions'][0]['evidence'][0]['quote'] = 'invented'
        compiled = free.compile_free(model, free.source_payload(c))
        self.assertEqual(compiled['invalid_assertions'], 1)

    def test_missing_empty_output_and_unavailable_keep_gold_denominators(self):
        for compiled in ([], [{'case_id': case()['id'], 'state': 'unavailable'}],
                         [free.compile_free({'nodes': [], 'source_assertions': [], 'status_events': []}, free.source_payload(case()))]):
            result = free.score_free([case()], [gold()], compiled)
            self.assertEqual(result['strict_edges'], panel._metric(0, 0, 1))
            self.assertEqual(result['strict_reference_atom_alignment'], panel._metric(0, 0, 2))
            self.assertIsNone(result['strict_edges']['precision'])

    def test_duplicate_unknown_cases_and_compiled_mutation_rejected(self):
        c = case(); compiled = free.compile_free(output(), free.source_payload(c))
        for values in ([compiled, compiled], [dict(compiled, case_id='other')]):
            with self.assertRaises(ValueError): free.score_free([c], [gold()], values)
        for mutate in ('edge', 'node', 'source', 'raw'):
            changed = deepcopy(compiled)
            if mutate == 'edge': changed['source_assertions'][0]['polarity'] = 'negative'
            elif mutate == 'node': changed['discovered_nodes'][0]['text'] = 'lampa świeci'
            elif mutate == 'source': changed['source_payload_sha256'] = 'wrong'
            else: changed['raw_model_object']['nodes'][0]['text'] = 'wrong'
            with self.assertRaises(ValueError): free.score_free([c], [gold()], [changed])

    def test_frozen_batch_reservations_are_exact_and_maximum_twelve(self):
        cases = [dict(case(), id=f'case{i}') for i in range(24)]
        rows = free.requests(cases); packed = free.batches(rows)
        self.assertEqual([len(b) for b in packed], [12, 12])
        self.assertEqual([r for b in packed for r in b], rows)
        for batch in packed:
            self.assertLessEqual(sum(Decimal(r['reservation_usd']) for r in batch), Decimal('.10'))
        for row in rows:
            self.assertEqual(row['reservation_usd'], safe.estimate_reservation(row['body'])['minimum_reservation_usd'])

    def replay_fixture(self, directory, raw, cost):
        d = Path(directory); c = case(); rows = free.requests([c])
        manifest = {'schema': safe.MANIFEST_SCHEMA, 'experiment_id': 'free-synthetic-test', 'budget_usd': '2',
            'max_requests': 1, 'requests': rows,
            'metadata': {'split': 'dev', 'track': free.TRACK, 'instrument': 'gpt', 'session_budget_reset': False, 'batch_cap_usd': '.10'},
            'pricing_evidence': [{'model': panel.MODEL, 'provider': panel.PROVIDER,
                'pricing': {'prompt': '.0000004', 'completion': '.0000016'},
                'source_url': safe.API_ROOT + '/models/' + panel.MODEL + '/endpoints', 'retrieved_at': '2026-09-30T00:00:00Z'}]}
        plan = safe.plan_manifest(manifest); request = plan['requests'][0]
        a = {k: request[k] for k in ('id', 'request_hash', 'reservation_usd')}
        a.update(state='completed', http_status=200, response_file=c['id'] + '.response.bin',
                 response_sha256=hashlib.sha256(raw).hexdigest(), reported_cost_usd=cost, elapsed_seconds=.5)
        (d / a['response_file']).write_bytes(raw)
        (d / 'manifest.json').write_bytes(safe.canonical(manifest))
        (d / 'ledger.json').write_bytes(safe.canonical({'schema': 'loom.openrouter_ledger/1', 'manifest_hash': plan['manifest_hash'], 'attempts': [a]}))
        return c, d / 'manifest.json'

    def test_actual_integrity_helpers_still_fail_hard_on_cost_mismatch(self):
        raw = safe.canonical({'model': panel.MODEL, 'provider': 'OpenAI', 'usage': {'cost': .002, 'is_byok': False},
            'choices': [{'finish_reason': 'stop', 'message': {'content': safe.canonical(output()).decode()}}]})
        with tempfile.TemporaryDirectory() as temporary:
            c, manifest = self.replay_fixture(temporary, raw, '.000001')
            with self.assertRaises(integrity.BillingIntegrityError): free.load_run(manifest, temporary, [c])
        with tempfile.TemporaryDirectory() as temporary:
            c, manifest = self.replay_fixture(temporary, raw, '.002')
            outputs, summary = free.load_run(manifest, temporary, [c])
            self.assertEqual(outputs[0]['state'], 'completed')
            self.assertEqual(summary['reported_known_cost_usd'], '0.002')


if __name__ == '__main__':
    unittest.main()
