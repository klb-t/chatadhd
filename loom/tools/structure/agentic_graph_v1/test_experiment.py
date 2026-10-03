"""Source-only review transport/alignment mechanics; never model quality."""
from copy import deepcopy
from decimal import Decimal
import hashlib
from pathlib import Path
import os
import tempfile
import unittest
from unittest.mock import patch

from . import experiment as exp
try:
    from ..test_graph_free_extraction import case, output, gold
except ImportError:
    from test_graph_free_extraction import case, output, gold


def config():
    return {'schema': 'loom.agentic_graph_model/1', 'id': 'gpt41mini', 'model': exp.panel.MODEL,
            'provider': exp.panel.PROVIDER, 'max_price': {'prompt': '.4', 'completion': '1.6'},
            'max_tokens': 3072, 'snapshot_path': str(exp.integrity.SNAPSHOT.relative_to(exp.ROOT)),
            'snapshot_sha256': exp.panel.digest_file(exp.integrity.SNAPSHOT),
            'retrieved_at': '2026-09-30T01:49:34.956981+00:00', 'disable_reasoning': False}


def review(graph=None):
    return {'critique': [{'record_kind': 'source_assertion', 'record_id': 'edge1', 'issue': 'none', 'action': 'keep',
                         'reason': 'The stated directed conditional is present.', 'evidence': [{'turn_id': 't1'}]}],
            'revised_graph': deepcopy(output() if graph is None else graph)}


def bundle(cases):
    rows = []
    for c in cases:
        content = exp.safe.canonical(output()).decode()
        rows.append({'case_id': c['id'], 'review_eligible': True,
                     'baseline_compiled': exp.free.compile_free(content, exp.free.source_payload(c)),
                     'proposal_content': content, 'proposal_content_sha256': exp.sha_text(content)})
    return {'schema': 'loom.agentic_graph_baseline_transfer/1', 'variant': exp.VARIANT, 'split': 'dev',
            'method_freeze_sha256': 'f' * 64, 'selection_sha256': exp.panel.digest_file(exp.SELECTION),
            'original_paid_calls_reissued': 0, 'rows': rows,
            'source_payload_sha256': {c['id']: exp.safe.digest(exp.free.source_payload(c)) for c in cases}}


class ExperimentTests(unittest.TestCase):
    def bundle_context(self):
        digest_file = exp.panel.digest_file
        def frozen_digest(path):
            if Path(path) == exp.HERE / 'METHOD_FREEZE.json': return 'f' * 64
            return digest_file(path)
        return patch.object(exp.panel, 'digest_file', side_effect=frozen_digest)

    def test_selection_twelve_balanced_before_new_outputs_no_gold_needed(self):
        with patch.object(exp.panel, 'load_dev_gold', side_effect=AssertionError('gold opened')):
            cases = exp.selected_cases()
        self.assertEqual(len(cases), 12)
        self.assertEqual(sum(c['language'] == 'en' for c in cases), 6)
        self.assertEqual([c['id'] for c in cases], exp.read(exp.SELECTION)['selected_case_ids'])

    def test_review_input_contains_exact_source_and_original_text_only(self):
        c = case(); before = deepcopy(c); transferred = bundle([c])
        with patch.object(exp, 'validate_method_freeze'), self.bundle_context():
            rows, omitted = exp.review_requests([c], transferred, config())
        self.assertFalse(omitted)
        payload = exp.safe.parse_json(rows[0]['body']['messages'][1]['content'])
        self.assertEqual(set(payload), {'source', 'proposal_content'})
        self.assertEqual(payload['source'], exp.free.source_payload(c))
        self.assertEqual(payload['proposal_content'], transferred['rows'][0]['proposal_content'])
        self.assertNotIn('never-in-body', rows[0]['body']['messages'][1]['content'])
        self.assertEqual(c, before); self.assertEqual(rows[0]['body']['max_tokens'], 3072)

    def test_raw_invalid_proposal_text_is_explicit_new_review_not_silent_retry(self):
        c = case(); transferred = bundle([c]); row = transferred['rows'][0]
        row.update(proposal_content='{invalid first JSON', proposal_content_sha256=exp.sha_text('{invalid first JSON'),
                   baseline_compiled={'case_id': c['id'], 'state': 'unavailable', 'reason': 'invalid_source_proposal'})
        with patch.object(exp, 'validate_method_freeze'), self.bundle_context():
            rows, _ = exp.review_requests([c], transferred, config())
        self.assertIn('{invalid first JSON', rows[0]['body']['messages'][1]['content'])
        self.assertEqual(row['baseline_compiled']['state'], 'unavailable')

    def test_unavailable_transport_is_not_reissued_or_dropped_from_cases(self):
        c = case(); transferred = bundle([c]); row = transferred['rows'][0]
        row.update(review_eligible=False, eligibility_reason='finish_length',
                   baseline_compiled={'case_id': c['id'], 'state': 'unavailable'})
        with patch.object(exp, 'validate_method_freeze'), self.bundle_context():
            rows, omitted = exp.review_requests([c], transferred, config())
        self.assertEqual(rows, []); self.assertEqual(omitted, [{'case_id': c['id'], 'reason': 'finish_length'}])

    def test_public_snapshot_model_provider_price_and_supported_parameters_pinned(self):
        exp.validate_model(config())
        for mutation in ('model', 'provider', 'hash', 'price', 'reasoning'):
            changed = config()
            if mutation == 'model': changed['model'] = 'openai/fabricated'
            elif mutation == 'provider': changed['provider'] = 'fabricated'
            elif mutation == 'hash': changed['snapshot_sha256'] = '0' * 64
            elif mutation == 'price': changed['max_price']['prompt'] = '.1'
            else: changed['disable_reasoning'] = True
            with self.assertRaises(ValueError, msg=mutation): exp.validate_model(changed)

    def test_response_exact_identity_and_billing_metadata(self):
        body = exp.model_body({'source': exp.free.source_payload(case()), 'proposal_content': '{}'}, config())
        raw = {'model': 'openai/gpt-4.1-mini-2025-04-14', 'provider': 'OpenAI',
               'usage': {'cost': .01, 'is_byok': False},
               'choices': [{'finish_reason': 'stop', 'message': {'content': exp.safe.canonical(review()).decode()}}]}
        content = exp.response_content(exp.safe.canonical(raw), body, config())
        self.assertEqual(exp.safe.parse_json(content), review())
        bad = deepcopy(raw); bad['model'] = 'openai/gpt-4.1-mini-20990101'
        with self.assertRaises(ValueError): exp.response_content(exp.safe.canonical(bad), body, config())
        bad = deepcopy(raw); bad['usage']['is_byok'] = True
        with self.assertRaises(exp.integrity.BillingIntegrityError): exp.response_content(exp.safe.canonical(bad), body, config())

    def test_criticism_never_enters_source_graph_and_invalid_critique_rejected(self):
        compiled = exp.compile_review(review(), case())
        self.assertEqual(compiled['compiled_graph'], exp.free.compile_free(output(), exp.free.source_payload(case())))
        self.assertEqual(compiled['critique'], review()['critique'])
        self.assertEqual(compiled['critique_basis'], 'unverified_model_hypothesis_not_graph_evidence')
        bad = review(); bad['critique'][0]['evidence'][0]['turn_id'] = 'unknown'
        with self.assertRaises(ValueError): exp.compile_review(bad, case())
        bad = review(); bad['consensus_is_true'] = True
        with self.assertRaises(ValueError): exp.compile_review(bad, case())

    def test_lost_added_and_renamed_records_visible_without_reference_alignment(self):
        c = case(); original = exp.free.compile_free(output(), exp.free.source_payload(c))
        renamed = output(); renamed['nodes'][0]['id'] = 'renamed'; renamed['source_assertions'][0]['source'] = 'renamed'
        result = exp.free.compile_free(renamed, exp.free.source_payload(c))
        compare = exp.compare_proposals(original, result)
        self.assertEqual(compare['records']['source_assertions']['unchanged_records'], 1)
        self.assertEqual(compare['records']['source_assertions']['added_records'], 0)
        empty = exp.free.compile_free({'nodes': [], 'source_assertions': [], 'status_events': []}, exp.free.source_payload(c))
        compare = exp.compare_proposals(original, empty)
        self.assertEqual(compare['records']['source_assertions']['removed_records'], 1)
        self.assertFalse(exp.compare_proposals({'state': 'unavailable'}, result)['available'])

    def test_same_strict_alias_scorer_preserves_unmatched_as_alignment_lower_bound(self):
        c = case(); proposed = output(); proposed['nodes'][0]['text'] = 'the light stays dark'
        reviewed = exp.compile_review(review(proposed), c)['compiled_graph']
        score = exp.free.score_free([c], [gold()], [reviewed])
        self.assertEqual(score['strict_edges']['tp'], 0)
        self.assertEqual(score['strict_edges']['fp'], 1)
        self.assertIn('lower_bound_not_world_truth', score['edge_alignment_interpretation'])

    def test_batch_caps_keep_order_and_never_reset_global_budget(self):
        rows = [{'id': str(i), 'reservation_usd': '.03'} for i in range(12)]
        batches = exp.batch_rows(rows)
        self.assertEqual([len(x) for x in batches], [3, 3, 3, 3])
        self.assertEqual([r for batch in batches for r in batch], rows)
        self.assertTrue(all(sum(Decimal(r['reservation_usd']) for r in batch) <= Decimal('.1') for batch in batches))
        with self.assertRaises(ValueError): exp.batch_rows([{'reservation_usd': '.11'}])

    def test_relative_model_config_cli_freeze_resolves_without_reading_gold(self):
        absolute = exp.ROOT / 'docs/research/agentic_graph_v1/gpt41mini.json'
        path = Path(os.path.relpath(absolute, Path.cwd()))
        recorded = str(absolute.relative_to(exp.ROOT))
        with patch.object(exp.panel, 'load_dev_gold', side_effect=AssertionError('gold read')):
            frozen = exp.freeze_method([path])
        self.assertIn(recorded, frozen['files_sha256'])
        self.assertEqual(frozen['files_sha256'][recorded], exp.panel.digest_file(path))
        self.assertFalse(frozen['validation_read'])


if __name__ == '__main__':
    unittest.main()
