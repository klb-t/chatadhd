"""Independent bounded checks. Fixture logic is not borrowed from author tests."""
from __future__ import annotations
import copy
import hashlib
import itertools
import json
import pathlib
import sys
import unittest

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
FIRST_REPORT = ROOT / 'docs/research/GEMINI_EXPERIMENTS_INDEPENDENT_2026-09-28.initial.json'
OBS_TEXT = 'Żółty warunek: jeśli P, to Q. P oraz nie Q; wniosek zależy od założeń.'
QUESTION = {'id': 'relevance-to-active-thread', 'text': 'Czy twierdzenie pomaga w bieżącym wątku?',
            'rubric_id': 'independent-relevance-v1',
            'rubric': 'Oceń każde twierdzenie niezależnie. Kilka może być istotnych.'}


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False,
                                    separators=(',', ':'), allow_nan=False).encode()).hexdigest()


def support():
    return {'observation': 'o-now', 'byte_start': 0,
            'byte_len': len(OBS_TEXT.encode()), 'quote': OBS_TEXT}


def make_bundle(formulas, *, context='asserted', quantifier=False):
    """Build public-contract records independently, keeping all roots explicit."""
    packet = {'schema': 'loom.source_packet/1', 'snapshot_id': 'independent-logic-v1',
              'observations': [{'id': 'o-now', 'unit': 'u-independent', 'text': OBS_TEXT,
                                'locator': {'source': 'manual-fixture'}}],
              'entities': [], 'claims': []}
    bundle = {'schema': 'loom.candidate_graph/1', 'packet_id': packet['snapshot_id'],
              'entity_drafts': [], 'claim_drafts': [], 'roots': list(formulas),
              'coverage': [], 'unknowns': []}
    def entity(handle, kind, label, attrs):
        bundle['entity_drafts'].append({'handle': handle, 'kind': kind, 'label': label,
                                       'attrs': attrs, 'support': [support()]})
    def claim(subject, predicate, obj="", value=None, scope='@scope', extra=None):
        bundle['claim_drafts'].append({
            'handle': '@c' + str(len(bundle['claim_drafts'])), 'subject': subject,
            'predicate': predicate, 'object': obj, 'value': value,
            'qualifiers': {'scope': scope, 'extra': {'polarity': 'positive',
                           'assertion_context': context, **(extra or {})}},
            'assessment': {'basis': {'support': [support()]}, 'premises': {'claims': []}}})
    entity('@scope', 'scope', 'independent scope',
           {'scope_type': 'assertion' if context == 'asserted' else 'quotation',
            'assertion_context': context})
    body_scope = '@scope'
    if quantifier:
        entity('@quantified', 'scope', 'quantifier scope',
               {'scope_type': 'quantifier', 'assertion_context': context})
        entity('@binder', 'binder', 'x', {'symbol': 'x'})
        entity('@quantifier', 'expression_occurrence', 'all x', {})
        claim('@quantified', 'scope_parent', '@scope', scope='@quantified')
        claim('@binder', 'in_scope', '@quantified', scope='@quantified')
        claim('@quantifier', 'in_scope', '@scope')
        claim('@quantifier', 'operation_type', value='quantifier')
        claim('@quantifier', 'quantifier_kind', value='forall')
        claim('@quantifier', 'introduces_scope', '@quantified')
        claim('@quantifier', 'operand', '@binder', extra={'port': 'binder', 'ordinal': 0})
        claim('@quantifier', 'operand', next(iter(formulas)), extra={'port': 'body', 'ordinal': 0})
        body_scope = '@quantified'
        bundle['roots'] = ['@quantifier']
    for handle, formula in formulas.items():
        op = formula[0]
        entity(handle, 'expression_occurrence', formula[1] if op == 'atom' else op, {})
        claim(handle, 'in_scope', body_scope, scope=body_scope)
        operation = {'atom': 'predicate_application', 'implies': 'conditional',
                     'not': 'negation', 'and': 'conjunction'}[op]
        claim(handle, 'operation_type', value=operation, scope=body_scope)
        if op == 'atom':
            predicate = '@predicate_' + handle[1:]
            entity(predicate, 'term_occurrence', formula[1],
                   {'term_type': 'predicate', 'symbol': formula[1]})
            claim(predicate, 'in_scope', body_scope, scope=body_scope)
            claim(handle, 'operand', predicate, scope=body_scope,
                  extra={'port': 'predicate', 'ordinal': 0})
        else:
            ports = {'implies': ('antecedent', 'consequent'), 'not': ('body',),
                     'and': ('member',) * (len(formula)-1)}[op]
            for index, (port, target) in enumerate(zip(ports, formula[1:])):
                claim(handle, 'operand', target, scope=body_scope,
                      extra={'port': port, 'ordinal': index if op == 'and' else 0})
    bundle['coverage'] = [{'support': [support()], 'status': 'represented',
                          'reason': 'manual supplied shape, not an extraction claim',
                          'drafts': list(bundle['roots'])}]
    return bundle, packet


def evaluate(formulas, root, values):
    op, *parts = formulas[root]
    if op == 'atom':
        return values[root]
    if op == 'not':
        return not evaluate(formulas, parts[0], values)
    if op == 'and':
        return all(evaluate(formulas, part, values) for part in parts)
    if op == 'implies':
        return not evaluate(formulas, parts[0], values) or evaluate(formulas, parts[1], values)
    raise AssertionError('oracle fixture has unknown operator')


def oracle(formulas, premises, conclusion):
    atoms = sorted(k for k, v in formulas.items() if v[0] == 'atom')
    satisfying = []
    countermodels = []
    for bits in itertools.product((False, True), repeat=len(atoms)):
        values = dict(zip(atoms, bits))
        if all(evaluate(formulas, p, values) for p in premises):
            satisfying.append(values)
            if not evaluate(formulas, conclusion, values):
                countermodels.append(values)
    return {'expected': 'inconsistent' if not satisfying else 'not_entailed' if countermodels else 'entailed',
            'atoms': atoms, 'satisfying': satisfying, 'countermodels': countermodels}


LOGIC_CASES = [
    ('modus_ponens', {'@p': ('atom', 'P'), '@q': ('atom', 'Q'),
                      '@i': ('implies', '@p', '@q')}, ['@i', '@p'], '@q'),
    ('invalid_converse', {'@p': ('atom', 'P'), '@q': ('atom', 'Q'),
                         '@i': ('implies', '@p', '@q')}, ['@i', '@q'], '@p'),
    ('inconsistent_premises', {'@p': ('atom', 'P'), '@q': ('atom', 'Q'),
                               '@n': ('not', '@p')}, ['@p', '@n'], '@q'),
    ('same_label_distinct_occurrences', {'@p': ('atom', 'P'), '@other': ('atom', 'P'),
                                         '@q': ('atom', 'Q'), '@i': ('implies', '@p', '@q')},
                                        ['@i', '@other'], '@q'),
    ('double_negation', {'@p': ('atom', 'P'), '@n': ('not', '@p'),
                        '@nn': ('not', '@n')}, ['@nn'], '@p'),
    ('conjunction_elimination', {'@p': ('atom', 'P'), '@q': ('atom', 'Q'),
                                '@a': ('and', '@p', '@q')}, ['@a'], '@q'),
    ('negated_conjunction_countermodel', {'@p': ('atom', 'P'), '@q': ('atom', 'Q'),
                                          '@a': ('and', '@p', '@q'), '@n': ('not', '@a')},
                                         ['@n', '@p'], '@q'),
]


def make_context_packet(prefix=''):
    from context_delta import prepare_context
    old_text = 'Porównujemy zapis, kolor i czas działania.'
    old_span = {'observation': prefix + 'o-old', 'byte_start': 0,
                'byte_len': len(old_text.encode()), 'quote': old_text}
    observations = [{'id': prefix + 'o-old', 'unit': prefix + 'u-old', 'text': old_text,
                     'observed_at': '2026-09-28T09:00:00Z', 'locator': {'source': 'manual'}},
                    {'id': prefix + 'o-now', 'unit': prefix + 'u-now', 'text': 'Wróćmy do projektu.',
                     'observed_at': '2026-09-28T10:00:00Z', 'locator': {'source': 'manual'}}]
    claims = [{'id': prefix + ident, 'subject': prefix + 'entity', 'predicate': predicate,
               'object': None, 'value': label, 'qualifiers': {'scope': '', 'extra': {}},
               'observed_at': '2026-09-28T09:00:00Z',
               'assessment': {'basis': {'support': [copy.deepcopy(old_span)]},
                              'premises': {'claims': []}, 'evidence_class': 'observed',
                              'origin': 'independent fixture', 'confidence': 1.0, 'status': 'active'}}
              for ident, predicate, label in [('c-save', 'stores', 'source'),
                                               ('c-color', 'has_color', 'yellow'),
                                               ('c-time', 'takes', 'one minute')]]
    base = {'schema': 'loom.source_packet/1', 'snapshot_id': prefix + 'independent-context',
            'observations': observations, 'claims': claims,
            'entities': [{'id': prefix + 'entity', 'label': 'project', 'observed_at': '2026-09-28T08:00:00Z'}],
            'threads': [], 'assignments': []}
    current = {'id': 'span-current', 'observation': prefix + 'o-now', 'byte_start': 0,
               'byte_len': len(observations[1]['text'].encode()), 'quote': observations[1]['text']}
    ids = [c['id'] for c in claims]
    prepared = prepare_context(base, [current], [{'claim_id': cid, 'selection_reason': 'manual allowlist'}
                                                for cid in ids], time_cut='2026-09-28T11:00:00Z')
    if prepared['status'] != 'ready':
        raise AssertionError(prepared)
    return prepared['packet'], ids


def fixture_digest():
    return digest({'logic': LOGIC_CASES, 'logic_bundles': [make_bundle(case[1]) for case in LOGIC_CASES],
                   'quoted': make_bundle({'@p': ('atom', 'P')}, context='quoted'),
                   'quantified': make_bundle({'@p': ('atom', 'P')}, quantifier=True),
                   'question': QUESTION, 'context': make_context_packet(),
                   'renamed_context': make_context_packet('renamed-'),
                   'score_values': [0.9, 0.8, 0.1], 'thresholds': [0.75, 0.25]})


# Runtime API assertions are filled from the public contracts before first execution.

def collect_first_run():
    from logic_check import check_bundle
    from context_scores import make_score_request, replay_context_scores, compare_score_runs
    records = {}
    def record(name, function, *args, **kwargs):
        before = copy.deepcopy((args, kwargs))
        try:
            output = function(*args, **kwargs)
            records[name] = {'output': output, 'mutated_input': (args, kwargs) != before}
        except Exception as failure:
            records[name] = {'exception': type(failure).__name__ + ': ' + str(failure),
                             'mutated_input': (args, kwargs) != before}
        return records[name].get('output', {})
    for name, formulas, premises, conclusion in LOGIC_CASES:
        bundle, packet = make_bundle(formulas)
        record('logic/' + name, check_bundle, bundle, packet, premises, conclusion)
    bundle, packet = make_bundle(LOGIC_CASES[0][1])
    record('logic/atom_budget', check_bundle, bundle, packet, ['@i', '@p'], '@q', limits={'max_atoms': 1})
    record('logic/valuation_budget', check_bundle, bundle, packet, ['@i', '@p'], '@q', limits={'max_assignments': 2})
    record('logic/boolean_budget', check_bundle, bundle, packet, ['@i', '@p'], '@q', limits={'max_atoms': True})
    bad = copy.deepcopy(bundle)
    bad['entity_drafts'][1]['support'][0]['quote'] = 'not the source'
    record('logic/source_mismatch', check_bundle, bad, packet, ['@i', '@p'], '@q')
    for name, options, root in [('quotation', {'context': 'quoted'}, '@p'),
                                ('quantifier', {'quantifier': True}, '@quantifier')]:
        one, one_packet = make_bundle({'@p': ('atom', 'P')}, **options)
        record('logic/' + name, check_bundle, one, one_packet, [], root)
    packet, ids = make_context_packet()
    request = record('scores/request', make_score_request, packet, ids, question=QUESTION)
    response = {'schema': 'loom.context_scores_response/1',
                **{k: request.get(k) for k in ('packet_hash', 'request_hash', 'question_hash')},
                'rows': [{'candidate_id': cid, 'probability': p} for cid, p in zip(ids, [0.9, 0.8, 0.1])]}
    options = {'question': QUESTION, 'keep_threshold': 0.75, 'drop_threshold': 0.25}
    left = record('scores/independent', replay_context_scores, packet, ids, response, **options)
    missing = copy.deepcopy(response)
    missing['rows'] = missing['rows'][:1]
    record('scores/missing', replay_context_scores, packet, ids, missing, **options)
    for name, probability in [('none', None), ('bool', True), ('string', '0.9'),
                              ('negative', -0.1), ('above_one', 1.1), ('object', {})]:
        bad = copy.deepcopy(response)
        bad['rows'][1]['probability'] = probability
        record('scores/invalid_' + name, replay_context_scores, packet, ids, bad, **options)
    for name in ('packet_hash', 'request_hash', 'question_hash'):
        bad = copy.deepcopy(response)
        bad[name] = '0' * 64
        record('scores/bad_' + name, replay_context_scores, packet, ids, bad, **options)
    duplicate = copy.deepcopy(response)
    duplicate['rows'].append(copy.deepcopy(duplicate['rows'][0]))
    record('scores/duplicate', replay_context_scores, packet, ids, duplicate, **options)
    unknown = copy.deepcopy(response)
    unknown['rows'].append({'candidate_id': 'not-allowlisted', 'probability': 0.99})
    record('scores/unknown', replay_context_scores, packet, ids, unknown, **options)
    boundary = copy.deepcopy(response)
    boundary['rows'] = [{'candidate_id': cid, 'probability': p} for cid, p in zip(ids, [0.75, 0.25, 0.5])]
    record('scores/boundaries', replay_context_scores, packet, ids, boundary, **options)
    for name, keep, drop in [('equal', 0.5, 0.5), ('reversed', 0.25, 0.75),
                             ('boolean', True, 0.25), ('above_one', 1.1, 0.25)]:
        record('scores/threshold_' + name, replay_context_scores, packet, ids, response,
               question=QUESTION, keep_threshold=keep, drop_threshold=drop)
    other_packet, other_ids = make_context_packet('renamed-')
    other_request = record('scores/other_request', make_score_request, other_packet, other_ids, question=QUESTION)
    other_response = {'schema': 'loom.context_scores_response/1',
                      **{k: other_request.get(k) for k in ('packet_hash', 'request_hash', 'question_hash')},
                      'rows': [{'candidate_id': cid, 'probability': p} for cid, p in zip(other_ids, [0.85, 0.9, 0.2])]}
    right = record('scores/renamed', replay_context_scores, other_packet, other_ids, other_response, **options)
    mapping = dict(zip(ids, other_ids))
    record('scores/compare', compare_score_runs, left, right, mapping)
    record('scores/compare_partial', compare_score_runs, left, right, dict(list(mapping.items())[:2]))
    record('scores/compare_nonbijective', compare_score_runs, left, right, {k: other_ids[0] for k in ids})
    other_question = {**QUESTION, 'text': 'Czy twierdzenie dotyczy koloru?'}
    question_request = record('scores/changed_question_request', make_score_request,
                              other_packet, other_ids, question=other_question)
    question_response = {**other_response, **{k: question_request.get(k)
                                             for k in ('packet_hash', 'request_hash', 'question_hash')}}
    changed = record('scores/changed_question', replay_context_scores, other_packet, other_ids,
                     question_response, question=other_question, keep_threshold=0.75, drop_threshold=0.25)
    record('scores/compare_changed_question', compare_score_runs, left, changed, mapping)
    # The old envelope must also reject when only the requested question changes.
    record('scores/rebound_question', replay_context_scores, packet, ids, response,
           question=other_question, keep_threshold=0.75, drop_threshold=0.25)
    return {'schema': 'loom.independent_gemini_experiments/1', 'fixture_sha256': fixture_digest(),
            'implementation_sha256': {name: hashlib.sha256((HERE / name).read_bytes()).hexdigest()
                                      for name in ('logic_check.py', 'context_scores.py', 'candidate_graph.py', 'context_delta.py')},
            'records': records,
            'oracle': {name: oracle(formulas, premises, conclusion)
                       for name, formulas, premises, conclusion in LOGIC_CASES},
            'no_live_model_quality_measurement': True}


REPORT = None


class IndependentGeminiExperiments(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        global REPORT
        if REPORT is None:
            REPORT = collect_first_run()
        cls.report = REPORT
        cls.records = REPORT['records']

    def out(self, name):
        self.assertNotIn('exception', self.records[name], self.records[name])
        return self.records[name]['output']

    def test_fixture_hash_frozen(self):
        self.assertEqual(self.report['fixture_sha256'],
                         'b8fd68f0befe8e24eecdb1645f6410e72ab7b59374c28091864771d06f3159a3')

    def test_logic_matches_independent_truth_tables(self):
        for name, formulas, premises, conclusion in LOGIC_CASES:
            with self.subTest(case=name):
                truth = oracle(formulas, premises, conclusion)
                if not truth['satisfying']:
                    expected = 'inconsistent_premises'
                elif not truth['countermodels']:
                    expected = 'entailed'
                elif len(truth['countermodels']) == len(truth['satisfying']):
                    expected = 'contradicted'
                else:
                    expected = 'undetermined'
                self.assertEqual(self.out('logic/' + name)['status'], expected)

    def test_logic_witnesses_satisfy_premises_and_reported_conclusion(self):
        for name, formulas, premises, conclusion in LOGIC_CASES:
            with self.subTest(case=name):
                result = self.out('logic/' + name)
                self.assertEqual(set(row['handle'] for row in result['atoms']), set(oracle(formulas,premises,conclusion)['atoms']))
                for kind, values in result['witnesses'].items():
                    if values is None:
                        continue
                    self.assertTrue(all(isinstance(v, bool) for v in values.values()))
                    self.assertTrue(all(evaluate(formulas, p, values) for p in premises))
                    if kind == 'conclusion_true':
                        self.assertTrue(evaluate(formulas, conclusion, values))
                    elif kind == 'conclusion_false':
                        self.assertFalse(evaluate(formulas, conclusion, values))

    def test_unsupported_is_not_an_entailment_result(self):
        for case in ('quotation', 'quantifier'):
            with self.subTest(case=case):
                self.assertEqual(self.out('logic/' + case)['status'], 'unsupported')

    def test_budget_stop_is_before_enumeration(self):
        for case in ('atom_budget', 'valuation_budget'):
            with self.subTest(case=case):
                result = self.out('logic/' + case)
                self.assertEqual(result['status'], 'limit')
                self.assertEqual(result['assignments_checked'], 0)
        self.assertEqual(self.out('logic/boolean_budget')['status'], 'rejected')

    def test_source_exactness_is_required(self):
        self.assertEqual(self.out('logic/source_mismatch')['status'], 'rejected')

    def test_independent_scores_are_not_normalized(self):
        result = self.out('scores/independent')
        self.assertEqual(result['status'], 'ready')
        self.assertEqual([r['probability'] for r in result['rows']], [0.9, 0.8, 0.1])
        self.assertEqual([r['suggestion'] for r in result['rows']], ['keep', 'keep', 'drop'])

    def test_missing_invalid_values_abstain_locally(self):
        result = self.out('scores/missing')
        self.assertEqual(result['status'], 'ready')
        self.assertEqual([r['suggestion'] for r in result['rows']], ['keep', 'review', 'review'])
        for name in ('none', 'bool', 'string', 'negative', 'above_one', 'object'):
            with self.subTest(case=name):
                result = self.out('scores/invalid_' + name)
                self.assertEqual(result['status'], 'ready')
                self.assertEqual([r['suggestion'] for r in result['rows']], ['keep', 'review', 'drop'])
                self.assertIsNone(result['rows'][1]['probability'])
                self.assertEqual(result['rows'][1]['score_status'], 'unknown')

    def test_bad_envelopes_fail_as_a_whole(self):
        for name in ('bad_packet_hash', 'bad_request_hash', 'bad_question_hash',
                     'duplicate', 'unknown', 'rebound_question'):
            with self.subTest(case=name):
                result = self.out('scores/' + name)
                self.assertEqual(result['status'], 'invalid')
                self.assertEqual(len(result['rows']), 3)
                self.assertTrue(all(r['suggestion'] == 'review' and r['probability'] is None for r in result['rows']))

    def test_threshold_validation_and_boundaries(self):
        for name in ('equal', 'reversed', 'boolean', 'above_one'):
            with self.subTest(case=name):
                self.assertEqual(self.out('scores/threshold_' + name)['status'], 'invalid')
        self.assertEqual([r['suggestion'] for r in self.out('scores/boundaries')['rows']],
                         ['keep', 'drop', 'review'])

    def test_correspondence_metrics_are_computed_from_paired_scores(self):
        result = self.out('scores/compare')
        self.assertEqual(result['status'], 'ready')
        self.assertEqual(result['candidate_count'], 3)
        self.assertEqual(result['measured_pairs'], 3)
        self.assertEqual(result['suggestion_agreement'], 1.0)
        self.assertAlmostEqual(result['mean_absolute_score_drift'], (0.05+0.1+0.1)/3)
        self.assertAlmostEqual(result['max_absolute_score_drift'], 0.1)
        self.assertEqual(result['correspondence'], 'caller_declared_not_verified')

    def test_comparison_requires_complete_bijection_and_same_question(self):
        for name in ('compare_partial', 'compare_nonbijective', 'compare_changed_question'):
            with self.subTest(case=name):
                self.assertEqual(self.out('scores/' + name)['status'], 'invalid')

    def test_inputs_and_claim_assessments_are_retained(self):
        for name, record in self.records.items():
            with self.subTest(case=name):
                self.assertFalse(record['mutated_input'])
        packet, ids = make_context_packet()
        result = self.out('scores/independent')
        self.assertEqual(result['source_packet'], packet)
        source = {r['claim_id']: r for r in packet['context_claims']}
        for row in result['rows']:
            self.assertEqual(row['context_claim'], source[row['candidate_id']])
        for name, formulas, premises, conclusion in LOGIC_CASES:
            result = self.out('logic/' + name)
            bundle, source_packet = make_bundle(formulas)
            self.assertEqual(result['retained_input']['bundle'], bundle)
            self.assertEqual(result['retained_input']['source_packet'], source_packet)
            self.assertTrue(result['no_persistence'])
            self.assertTrue(result['no_promotion'])
            self.assertFalse(result['source_interpretation_validated'])
            self.assertFalse(result['source_truth_validated'])
        self.assertTrue(result['no_persistence'])
        self.assertIsNone(self.out('scores/independent')['semantic_quality'])


if __name__ == '__main__':
    if '--write-first-report' in sys.argv:
        sys.argv.remove('--write-first-report')
        REPORT = collect_first_run()
        # Exclusive creation makes accidental overwrite of the first run impossible.
        with FIRST_REPORT.open('x', encoding='utf-8') as stream:
            json.dump(REPORT, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.write('\n')
        print('First execution saved before assertions:', FIRST_REPORT)
    unittest.main(verbosity=2)
