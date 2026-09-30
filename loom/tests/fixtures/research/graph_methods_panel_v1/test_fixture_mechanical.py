"""Fixture integrity only; no model, parser, retrieval or quality measurement.

Default checks parse development labels only and hash sealed validation files.
The isolated author may opt into complete integrity checks with
GRAPH_PANEL_AUTHOR_CHECK_ALL=1; this must not be a method-development workflow.
"""
from collections import Counter
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import unittest


HERE = Path(__file__).resolve().parent
RELATIONS = {'implies', 'causes', 'supports', 'prevents', 'requires'}


def read(name):
    return json.loads((HERE / name).read_text())


def timestamp(value):
    return datetime.fromisoformat(value.replace('Z', '+00:00'))


class FixtureMechanicalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = read('manifest.json')
        cls.splits = ['dev', 'validation'] if os.environ.get('GRAPH_PANEL_AUTHOR_CHECK_ALL') == '1' else ['dev']
        cls.datasets = {split: (read(f'inputs_{split}.json')['cases'], read(f'gold_{split}.json')['cases']) for split in cls.splits}

    def rows(self):
        for split, (cases, golds) in self.datasets.items():
            self.assertEqual([c['id'] for c in cases], [g['id'] for g in golds])
            for case, gold in zip(cases, golds):
                yield split, case, gold

    def test_frozen_file_hashes(self):
        self.assertTrue(self.manifest['frozen_before_predictions'])
        for filename, digest in self.manifest['files'].items():
            self.assertEqual(hashlib.sha256((HERE / filename).read_bytes()).hexdigest(), digest)

    def test_family_split_disjoint_and_declared(self):
        assigned = self.manifest['family_assignment']
        dev = {family for family, split in assigned.items() if split == 'dev'}
        validation = {family for family, split in assigned.items() if split == 'validation'}
        self.assertEqual(len(dev), 4)
        self.assertEqual(len(validation), 4)
        self.assertFalse(dev & validation)
        for split, _, gold in self.rows():
            self.assertEqual(assigned[gold['family']], split)

    def test_split_case_language_and_family_counts(self):
        for split, (cases, golds) in self.datasets.items():
            self.assertEqual(len(cases), 24)
            self.assertEqual(Counter(c['language'] for c in cases), {'en': 12, 'pl': 12})
            counts = Counter(g['family'] for g in golds)
            self.assertEqual(len(counts), 4)
            self.assertEqual(set(counts.values()), {6})
            for family in counts:
                self.assertEqual(Counter(g['language'] for g in golds if g['family'] == family), {'en': 3, 'pl': 3})

    def test_no_gold_or_family_in_method_inputs(self):
        for _, case, _ in self.rows():
            self.assertNotIn('family', case)
            self.assertNotIn('source_assertions', case)
            self.assertNotIn('formal_paths', case)
            for query in case['judgment_queries']:
                self.assertNotIn('label', query)
                self.assertNotIn('support_assertion_ids', query)

    def test_unique_case_turn_node_assertion_and_query_ids(self):
        seen = set()
        for _, case, gold in self.rows():
            self.assertNotIn(case['id'], seen)
            seen.add(case['id'])
            for rows in [case['turns'], case['node_inventory'], case['judgment_queries'], gold['source_assertions']]:
                identifiers = [r['id'] for r in rows]
                self.assertEqual(len(set(identifiers)), len(identifiers))

    def test_source_turn_order_and_query_cutoffs(self):
        for _, case, _ in self.rows():
            dates = [timestamp(t['known_at']) for t in case['turns']]
            self.assertEqual(dates, sorted(dates))
            self.assertEqual(len(dates), len(set(dates)))
            for query in case['judgment_queries']:
                self.assertIn(timestamp(query['as_of']), dates)

    def test_all_relation_endpoints_exist(self):
        for _, case, gold in self.rows():
            nodes = {n['id'] for n in case['node_inventory']}
            for edge in gold['source_assertions'] + case['judgment_queries']:
                self.assertIn(edge['relation'], RELATIONS)
                self.assertIn(edge['source'], nodes)
                self.assertIn(edge['target'], nodes)
                self.assertNotEqual(edge['source'], edge['target'])

    def test_source_evidence_exact_unicode_utf8_known_at_and_scope(self):
        for _, case, gold in self.rows():
            turns = {turn['id']: turn for turn in case['turns']}
            records = gold['source_assertions'] + gold['status_events']
            for record in records:
                self.assertTrue(record['evidence'])
                for evidence in record['evidence']:
                    self.assertEqual(evidence['source_id'], case['source_id'])
                    self.assertEqual(evidence['coordinate_space'], 'turn.text')
                    turn = turns[evidence['turn_id']]
                    self.assertEqual(record['known_at'], turn['known_at'])
                    text = turn['text']
                    a, b, x, y = [evidence[k] for k in ['char_start', 'char_end', 'byte_start', 'byte_end']]
                    self.assertTrue(all(type(offset) is int for offset in [a, b, x, y]))
                    self.assertTrue(0 <= a < b <= len(text))
                    self.assertEqual(text[a:b], evidence['quote'])
                    self.assertEqual(text.encode()[x:y].decode(), evidence['quote'])
                    self.assertEqual((len(text[:a].encode()), len(text[:b].encode())), (x, y))

    def test_source_assertion_not_promoted_to_content_truth(self):
        for _, _, gold in self.rows():
            for edge in gold['source_assertions']:
                self.assertIn(edge['polarity'], {'positive', 'negative'})
                self.assertEqual(edge['basis_class'], 'observed_source_assertion')
                self.assertEqual(edge['content_truth'], 'unverified')
                self.assertTrue(edge['attributed_to'])
            for judgment in gold['judgments']:
                self.assertEqual(judgment['content_truth'], 'unverified')

    def test_status_events_append_only_and_references_retained(self):
        for _, _, gold in self.rows():
            edges = {edge['id']: edge for edge in gold['source_assertions']}
            for event in gold['status_events']:
                self.assertEqual(event['status'], 'superseded')
                self.assertIn(event['assertion_id'], edges)
                self.assertIn(event['superseded_by'], edges)
                self.assertNotEqual(event['assertion_id'], event['superseded_by'])
                self.assertLess(timestamp(edges[event['assertion_id']]['known_at']), timestamp(event['known_at']))
                self.assertEqual(timestamp(edges[event['superseded_by']]['known_at']), timestamp(event['known_at']))

    def test_judgment_ids_classes_and_no_future_support(self):
        for _, case, gold in self.rows():
            queries = {q['id']: q for q in case['judgment_queries']}
            assertions = {a['id']: a for a in gold['source_assertions']}
            self.assertEqual(len(queries), 4)
            self.assertEqual({g['query_id'] for g in gold['judgments']}, set(queries))
            for judgment in gold['judgments']:
                self.assertIn(judgment['label'], {'supported', 'refuted', 'unknown'})
                q = queries[judgment['query_id']]
                if judgment['label'] == 'unknown':
                    self.assertEqual(judgment['support_assertion_ids'], [])
                    self.assertEqual(judgment['basis_class'], 'none')
                else:
                    self.assertTrue(judgment['support_assertion_ids'])
                for ident in judgment['support_assertion_ids']:
                    self.assertIn(ident, assertions)
                    self.assertLessEqual(timestamp(assertions[ident]['known_at']), timestamp(q['as_of']))
                    self.assertEqual(assertions[ident]['attributed_to'], q['attributed_to'])

    def test_direct_judgment_support_matches_type_direction_polarity(self):
        for _, case, gold in self.rows():
            queries = {q['id']: q for q in case['judgment_queries']}
            assertions = {a['id']: a for a in gold['source_assertions']}
            for judgment in gold['judgments']:
                q = queries[judgment['query_id']]
                if q['scope'] != 'explicit_source' or judgment['label'] == 'unknown':
                    continue
                polarity = 'positive' if judgment['label'] == 'supported' else 'negative'
                for ident in judgment['support_assertion_ids']:
                    edge = assertions[ident]
                    self.assertEqual((edge['relation'], edge['source'], edge['target'], edge['polarity']),
                                     (q['relation'], q['source'], q['target'], polarity))
                    self.assertFalse(any(e['assertion_id'] == ident and timestamp(e['known_at']) <= timestamp(q['as_of'])
                                         for e in gold['status_events']))

    def test_formal_paths_are_composable_positive_attributed_and_inferred(self):
        for _, case, gold in self.rows():
            queries = {q['id']: q for q in case['judgment_queries']}
            assertions = {a['id']: a for a in gold['source_assertions']}
            for inferred in gold['formal_paths']:
                q = queries[inferred['query_id']]
                self.assertEqual(q['scope'], 'formal_implication')
                self.assertEqual(inferred['evidence_class'], 'inferred')
                self.assertEqual(inferred['content_truth'], 'unverified')
                paths = inferred['minimal_support_paths']
                self.assertEqual(len(paths), len({tuple(path) for path in paths}))
                self.assertGreaterEqual(len(paths), 2)
                for path in paths:
                    self.assertGreaterEqual(len(path), 2)
                    current = q['source']
                    for ident in path:
                        edge = assertions[ident]
                        self.assertEqual((edge['relation'], edge['polarity'], edge['source']), ('implies', 'positive', current))
                        self.assertEqual(edge['attributed_to'], q['attributed_to'])
                        self.assertLessEqual(timestamp(edge['known_at']), timestamp(q['as_of']))
                        current = edge['target']
                    self.assertEqual(current, q['target'])

    def test_recipes_are_unexecuted_and_have_no_spending_authorization(self):
        recipes = read('recipes.json')
        self.assertFalse(recipes['executed'])
        self.assertFalse(recipes['api_spending_authorized'])
        self.assertEqual(self.manifest['api_calls'], 0)
        identifiers = [r['id'] for r in recipes['recipes']]
        self.assertEqual(len(identifiers), len(set(identifiers)))


if __name__ == '__main__':
    unittest.main()
