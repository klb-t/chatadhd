"""Authored source-commitment countercases, no model/DEV gold/API."""
from copy import deepcopy
from pathlib import Path
import tempfile
import unittest
try:
    from . import graph_source_commitment_projection as projection
except ImportError:
    import graph_source_commitment_projection as projection


def fixture(actor='writer'):
    times = ['2026-01-01T09:00:00Z', '2026-01-01T09:01:00Z', '2026-01-01T09:02:00Z']
    texts = ['Ada: if A then B – source.', actor + ': A does not imply B.', 'Ada: correction, if A then C.']
    case = {'id': 'authored', 'source_id': 'synthetic:authored',
            'node_inventory': [{'id': n, 'text': n, 'aliases': []} for n in ('A', 'B', 'C')],
            'turns': [{'id': 't' + str(i + 1), 'text': text, 'known_at': times[i], 'speaker': 'Ada' if i != 1 else actor} for i, text in enumerate(texts)]}
    def evidence(i):
        text = texts[i]
        return [{'source_id': case['source_id'], 'turn_id': 't' + str(i + 1), 'quote': text,
                 'char_start': 0, 'char_end': len(text), 'byte_start': 0, 'byte_end': len(text.encode()), 'coordinate_space': 'turn.text'}]
    edge = lambda ident, i, who, polarity, target: {'id': ident, 'relation': 'implies', 'source': 'A', 'target': target,
        'attributed_to': who, 'polarity': polarity, 'known_at': times[i], 'evidence': evidence(i),
        'basis_class': 'observed_source_assertion', 'content_truth': 'unverified'}
    graph = {'state': 'completed', 'case_id': 'authored', 'source_assertions': [edge('a1', 0, 'Ada', 'positive', 'B'), edge('a2', 1, actor, 'negative', 'B')],
             'status_events': [{'assertion_id': 'a1', 'superseded_by': 'a2', 'status': 'superseded', 'known_at': times[1], 'evidence': evidence(1)}]}
    query = {'id': 'authored_q', 'relation': 'implies', 'source': 'A', 'target': 'B', 'attributed_to': 'Ada',
             'as_of': times[2], 'scope': 'explicit_source'}
    return case, graph, query, edge('a3', 2, 'Ada', 'positive', 'C')


class SourceCommitmentTests(unittest.TestCase):
    def evaluate(self, case, graph, query):
        view, audit = projection.project(graph, case)
        return projection.direct.lookup(case, view, query), view, audit

    def test_cross_source_withheld_but_raw_event_and_assertions_preserved(self):
        case, graph, query, _ = fixture()
        row, view, audit = self.evaluate(case, graph, query)
        self.assertEqual(row['label'], 'supported')
        self.assertEqual(view['status_events'], [])
        self.assertEqual(view['source_assertions'], graph['source_assertions'])
        self.assertEqual(audit['raw_candidate_events'], graph['status_events'])
        self.assertEqual(audit['withheld_events'][0]['event'], graph['status_events'][0])
        self.assertFalse(audit['world_authority_verified'])
        self.assertFalse(audit['canonical_mutation'])

    def test_same_source_correction_remains_refuted(self):
        case, graph, query, _ = fixture('Ada')
        row, view, audit = self.evaluate(case, graph, query)
        self.assertEqual(row['label'], 'refuted')
        self.assertEqual(view['status_events'], graph['status_events'])
        self.assertEqual(audit['withheld_events'], [])

    def test_writer_denial_itself_remains_available_for_writer_query(self):
        case, graph, query, _ = fixture(); query['attributed_to'] = 'writer'
        row, _, _ = self.evaluate(case, graph, query)
        self.assertEqual(row['label'], 'refuted')
        self.assertEqual(row['latest_match_ids'], ['a2'])

    def test_alternative_same_source_replacement_need_not_match_query(self):
        case, graph, query, replacement = fixture('Ada'); graph['source_assertions'].append(replacement)
        graph['status_events'] = [{'assertion_id': 'a1', 'superseded_by': 'a3', 'status': 'superseded', 'known_at': replacement['known_at'], 'evidence': deepcopy(replacement['evidence'])}]
        # The independent negative is still an explicit, active match.
        graph['source_assertions'].pop(1)
        row, _, _ = self.evaluate(case, graph, query)
        self.assertEqual(row['label'], 'unknown')
        self.assertEqual(row['history_assertion_ids'], ['a1', 'a3'])

    def test_early_source_cutoff_keeps_old_statement_before_same_source_event(self):
        case, graph, query, _ = fixture('Ada'); query['as_of'] = graph['source_assertions'][0]['known_at']
        row, _, _ = self.evaluate(case, graph, query)
        self.assertEqual(row['label'], 'supported')
        self.assertEqual(row['eligible_status_events'], [])

    def test_no_identity_alias_is_invented(self):
        case, graph, query, _ = fixture('ada')
        row, _, audit = self.evaluate(case, graph, query)
        self.assertEqual(row['label'], 'supported')
        self.assertEqual(len(audit['withheld_events']), 1)

    def test_malformed_reference_delegated_not_hidden(self):
        case, graph, query, _ = fixture(); graph['status_events'][0]['superseded_by'] = 'missing'
        row, view, audit = self.evaluate(case, graph, query)
        self.assertEqual(row['state'], 'unavailable')
        self.assertEqual(view['status_events'], graph['status_events'])
        self.assertEqual(audit['withheld_events'], [])

    def test_malformed_time_delegated_not_hidden(self):
        case, graph, query, _ = fixture(); graph['status_events'][0]['known_at'] = graph['source_assertions'][0]['known_at']
        row, view, audit = self.evaluate(case, graph, query)
        self.assertEqual(row['state'], 'unavailable')
        self.assertEqual(view['status_events'], graph['status_events'])
        self.assertEqual(audit['withheld_events'], [])

    def test_malformed_source_binding_delegated_not_hidden(self):
        case, graph, query, _ = fixture(); graph['status_events'][0]['evidence'][0]['quote'] = 'unbound text'
        row, view, audit = self.evaluate(case, graph, query)
        self.assertEqual(row['state'], 'unavailable')
        self.assertEqual(view['status_events'], graph['status_events'])
        self.assertEqual(audit['withheld_events'], [])

    def test_duplicate_assertion_ids_remain_unavailable(self):
        case, graph, query, _ = fixture(); graph['source_assertions'][1]['id'] = 'a1'
        row, view, audit = self.evaluate(case, graph, query)
        self.assertEqual(row['state'], 'unavailable')
        self.assertEqual(view, graph)
        self.assertEqual(audit['state'], 'malformed_delegate_unchanged')

    def test_deepcopy_does_not_mutate_graph_or_source(self):
        case, graph, query, _ = fixture(); before_case, before_graph = deepcopy(case), deepcopy(graph)
        _, view, audit = self.evaluate(case, graph, query)
        view['source_assertions'][0]['evidence'][0]['quote'] = 'trace change'
        audit['raw_candidate_events'][0]['status'] = 'trace change'
        self.assertEqual(case, before_case); self.assertEqual(graph, before_graph)

    def test_no_event_does_not_remove_latest_same_source_negative(self):
        case, graph, query, _ = fixture('Ada'); graph['status_events'] = []
        row, _, _ = self.evaluate(case, graph, query)
        self.assertEqual(row['label'], 'refuted')

    def test_unknown_policy_keys_and_unsupported_world_scope_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            for field, value in [('implicit_alias', True), ('source_scope', 'world_truth')]:
                policy = projection.load_policy(); policy[field] = value
                path = Path(tmp) / (field + '.json'); projection.panel.write_new(path, policy)
                with self.assertRaisesRegex(ValueError, 'unsupported'):projection.load_policy(path)


if __name__ == '__main__':unittest.main()
