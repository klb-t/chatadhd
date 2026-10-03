"""Independent native validation parity over frozen supplied candidate graphs.

No model calls and no native comparison-graph equivalence claim. Normal CTest
execution does not write reports. Use --write-report PATH for a recorded run.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import unittest

REPO = Path(__file__).resolve().parents[3]
EVAL_DIR = REPO / 'loom/tools/eval'
sys.path.insert(0, str(EVAL_DIR))
import candidate_graph_eval as independent  # noqa: E402

FIXTURES = REPO / 'loom/tests/fixtures/eval/independent_candidate_graph_v1'
VOCABULARY = REPO / 'loom/tools/structure/candidate_graph_vocabulary.json'
FROZEN_HASHES = {
    'development_cases.json': 'cb14ad33834be118def4a2305f503c58e5dac3a91cb20dcbf5223bac3b99501b',
    'validation_cases.json': '4dcb59baaccd6deb914c5504004649a5e2f926183b69b0797e43ec654a273787',
}
VOCABULARY_SHA256 = '0d02838638e6f82c4e5f1cc90e3842f152f4db0ac44d593721cddc8e6af8650d'
PYTHON_REPORT_SHA256 = '82ca75d3d3cd8679c084d452a5c253b82d499e3729d066cfe9ba701af786abeb'
PROBES = REPO / 'loom/tests/fixtures/eval/independent_candidate_graph_native_v1/negative_probes.json'
PROBES_PAYLOAD_SHA256 = '6389d020ca10911ede4ed31b36e0a416407808147fcfc794d5b7f4a8187537e7'
_REPORT = None


def find_tool():
    configured = os.environ.get('LOOM_CANDIDATE_GRAPH_NATIVE_TOOL')
    if configured:
        path = Path(configured)
        if not path.is_file():
            raise AssertionError('LOOM_CANDIDATE_GRAPH_NATIVE_TOOL is not a file')
        return path
    raise unittest.SkipTest('native candidate tool unavailable; set LOOM_CANDIDATE_GRAPH_NATIVE_TOOL')


def file_hash(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def same_json(left, right):
    # Python equality conflates false with 0; source/reference fields must not.
    return independent.canonical(left) == independent.canonical(right)


def run_bytes(tool, payload):
    return subprocess.run([str(tool)], input=payload, capture_output=True, timeout=30)


def load_gold():
    if file_hash(VOCABULARY) != VOCABULARY_SHA256:
        raise AssertionError('frozen candidate vocabulary changed')
    cases = []
    for filename, expected_hash in FROZEN_HASHES.items():
        fixture = independent.load_fixture(FIXTURES / filename)
        if fixture['frozen_sha256'] != expected_hash:
            raise AssertionError('unexpected candidate fixture version')
        cases.extend(fixture['cases'])
    if len(cases) != 32 or len({c['id'] for c in cases}) != 32:
        raise AssertionError('expected 32 distinct frozen cases')
    for case in cases:
        if not independent.source_integrity(case)['verified']:
            raise AssertionError('independent source-byte integrity failed: ' + case['id'])
    return cases, json.loads(VOCABULARY.read_text())


def native_audit(case, result, python_result, vocabulary):
    """Judge source labels and retained fields, independently of native claims."""
    expected_valid = not case['expected']['reject']
    valid = result.get('valid')
    draft = result.get('drafts', {})
    if not isinstance(draft, dict):
        draft = {}
    actual_entities, actual_claims = draft.get('entities'), draft.get('claims')
    gold = case['direct_bundle']
    retained = result.get('retained_input', {})
    if not isinstance(retained, dict):
        retained = {}
    accepted = valid is True
    expected_entities = gold['entity_drafts'] if expected_valid else []
    expected_claims = gold['claim_drafts'] if expected_valid else []
    entities_preserved = same_json(actual_entities, expected_entities)
    claims_preserved = same_json(actual_claims, expected_claims)
    # Native v1 deliberately returns original partial drafts, not enriched
    # compiled records. Exact comparison therefore also checks support bytes.
    no_entity_defaults = isinstance(actual_entities, list) and all(
        isinstance(e, dict) and set(e) == {'handle', 'kind', 'label', 'attrs', 'support'} for e in actual_entities)
    no_assessment_defaults = isinstance(actual_claims, list) and all(
        isinstance(c, dict) and set(c) == {'handle', 'subject', 'predicate', 'object', 'value', 'qualifiers', 'assessment'} and
        isinstance(c.get('assessment'), dict) and
        set(c.get('assessment', {})) == {'basis', 'premises'} and
        isinstance(c['assessment']['basis'], dict) and isinstance(c['assessment']['premises'], dict) and
        set(c['assessment']['basis']) == {'support'} and
        set(c['assessment']['premises']) == {'claims'} for c in actual_claims)
    errors = result.get('errors')
    errors_well_formed = isinstance(errors, list) and all(
        isinstance(e, dict) and set(e) == {'code', 'path', 'message'} and
        all(isinstance(e[k], str) for k in e) for e in errors)
    expected_coverage = python_result['coverage']
    coverage = result.get('coverage', {})
    if not isinstance(coverage, dict):
        coverage = {}
    # Coverage parity alone is not the source oracle: fixture source_integrity
    # and the authored represented/unknown/rejection labels are separate gates.
    if expected_valid:
        expected_status = 'complete_declared' if gold['roots'] else 'unrepresented'
        source_bytes = sum(len(o['text'].encode()) for o in case['source_packet']['observations'])
        status_bytes = coverage.get('by_status_bytes', {}).get(case['expected']['coverage_status'])
        coverage_gold = (coverage.get('representation_status') == expected_status and
            same_json(coverage.get('source_status_rows'), gold['coverage']) and
            same_json(coverage.get('located_unknowns'), gold['unknowns']) and
            coverage.get('source_bytes') == source_bytes and status_bytes == source_bytes and
            coverage.get('represented_bytes') == (source_bytes if gold['roots'] else 0) and
            coverage.get('uncovered_bytes') == 0 and coverage.get('uncovered_spans') == [] and
            coverage.get('unknown_bytes') == (source_bytes if gold['unknowns'] else 0))
    else:
        coverage_gold = coverage.get('representation_status') == 'unknown'
    accepted_representation = (accepted and bool(actual_entities) and bool(actual_claims) and
        coverage.get('representation_status') != 'unrepresented')
    return {
        'validity_matches_gold': type(valid) is bool and valid == expected_valid,
        'validity_matches_python': type(valid) is bool and valid == python_result['valid'],
        'status_matches_validity': result.get('status') == ('valid' if accepted else 'rejected'),
        'errors_well_formed': errors_well_formed,
        'error_presence_correct': errors_well_formed and (not errors if accepted else bool(errors)),
        'source_packet_retained': same_json(retained.get('source_packet'), case['source_packet']),
        'bundle_retained': same_json(retained.get('bundle'), gold),
        'partial_entities_preserved': entities_preserved,
        'partial_claims_preserved': claims_preserved,
        'no_default_entity_assessment': no_entity_defaults,
        'no_default_claim_assessment': no_assessment_defaults,
        'gates_closed': result.get('no_inference') is True and result.get('no_persistence') is True,
        'coverage_matches_python': same_json(coverage, expected_coverage),
        'coverage_matches_gold': coverage_gold,
        'semantic_accuracy_unclaimed': coverage.get('semantic_accuracy', 'missing') is None,
        'accepted_representation': accepted_representation,
        'accepted_representation_correct': accepted_representation == bool(expected_valid and gold['roots']),
        'no_graph_projection_emitted': 'graph' not in result,
        'report_shape_expected': set(result) == ({'version', 'valid', 'status', 'errors', 'coverage',
            'packet_hash', 'hash_algorithm', 'retained_input', 'drafts', 'no_inference', 'no_persistence'} |
            ({'vocabulary_hash'} if accepted else set())),
        'packet_hash_algorithm_declared': result.get('hash_algorithm') == 'loom-json-canonical-sha256',
        'packet_hash_matches_gold': result.get('packet_hash') == independent.digest(case['source_packet']) if expected_valid else result.get('packet_hash', 'missing') is None,
        'vocabulary_hash_matches_gold': result.get('vocabulary_hash') == independent.digest(vocabulary) if expected_valid else 'vocabulary_hash' not in result,
        'packet_hash_matches_python': result.get('packet_hash') == python_result.get('packet_hash') if accepted else None,
    }


def evaluate():
    global _REPORT
    if _REPORT is not None:
        return _REPORT
    tool = find_tool()
    cases, vocabulary = load_gold()
    python_report_path = FIXTURES / 'initial_report.json'
    if file_hash(python_report_path) != PYTHON_REPORT_SHA256:
        raise AssertionError('frozen Python reference report changed')
    reference = json.loads(python_report_path.read_text())
    if set(reference['fixtures'].values()) != set(FROZEN_HASHES.values()):
        raise AssertionError('Python report used different fixtures')
    python_rows = {r['id']: r['direct_result'] for r in reference['rows']}
    if len(reference['rows']) != len(python_rows) or set(python_rows) != {c['id'] for c in cases}:
        raise AssertionError('Python report case set is incomplete or duplicated')
    rows = []
    for case in cases:
        payload = {'packet': case['source_packet'], 'bundle': case['direct_bundle'], 'vocabulary': vocabulary}
        encoded = independent.canonical(payload).encode()
        started = time.perf_counter_ns()
        process = run_bytes(tool, encoded)
        elapsed = (time.perf_counter_ns() - started) / 1e6
        native = None
        transport_error = None
        try:
            native = json.loads(process.stdout)
            if not isinstance(native, dict):
                transport_error = 'native report was not an object'
        except (UnicodeDecodeError, json.JSONDecodeError):
            transport_error = 'native stdout was not JSON'
        if process.returncode != 0:
            transport_error = 'nonzero native exit: ' + str(process.returncode)
        row = {'id': case['id'], 'split': case['split'], 'family': case['family'], 'language': case['language'],
               'expected_valid': not case['expected']['reject'], 'source_integrity': independent.source_integrity(case),
               'wrapper_latency_ms': elapsed, 'transport_error': transport_error,
               'stderr': process.stderr.decode('utf-8', errors='replace')[:2000], 'native_result': native,
               'audit': native_audit(case, native, python_rows[case['id']], vocabulary) if transport_error is None else {}}
        rows.append(row)
    fields = ['validity_matches_gold', 'validity_matches_python', 'source_packet_retained', 'bundle_retained',
              'partial_entities_preserved', 'partial_claims_preserved', 'no_default_entity_assessment',
              'no_default_claim_assessment', 'gates_closed', 'coverage_matches_python', 'coverage_matches_gold',
              'accepted_representation_correct', 'no_graph_projection_emitted',
              'packet_hash_matches_gold', 'vocabulary_hash_matches_gold', 'report_shape_expected']
    summaries = {}
    for split in ('development', 'validation'):
        group = [r for r in rows if r['split'] == split]
        summaries[split] = {'cases': len(group), 'transport_successes': sum(r['transport_error'] is None for r in group),
                           **{key: sum(r['audit'].get(key) is True for r in group) for key in fields},
                           'accepted_representations': sum(r['audit'].get('accepted_representation') is True for r in group)}
    probe_fixture = json.loads(PROBES.read_text())
    if probe_fixture['frozen_sha256'] != PROBES_PAYLOAD_SHA256 or independent.digest(
        {k: v for k, v in probe_fixture.items() if k != 'frozen_sha256'}) != PROBES_PAYLOAD_SHA256:
        raise AssertionError('native mechanical probe fixture changed')
    probe_results = []
    for probe in probe_fixture['probes']:
        process = run_bytes(tool, independent.canonical({'packet': probe['packet'], 'bundle': probe['bundle'], 'vocabulary': vocabulary}).encode())
        try:
            result = json.loads(process.stdout)
        except (UnicodeDecodeError, json.JSONDecodeError):
            result = {}
        if not isinstance(result, dict):
            result = {}
        checks = {
            'transport_ok': process.returncode == 0,
            'rejected': result.get('valid') is False and result.get('status') == 'rejected' and bool(result.get('errors')),
            'inputs_retained': same_json(result.get('retained_input'), {'bundle': probe['bundle'], 'source_packet': probe['packet']}),
            'drafts_empty': same_json(result.get('drafts'), {'entities': [], 'claims': []}),
            'gates_closed': result.get('no_inference') is True and result.get('no_persistence') is True,
            'hash_unset': result.get('packet_hash', 'missing') is None,
        }
        probe_results.append({'id': probe['id'], 'checks': checks, 'passed': all(checks.values()), 'native_result': result,
                              'stderr': process.stderr.decode('utf-8', errors='replace')[:2000]})
    native_sources = [REPO / 'loom/include/loom/knowledge_candidate_graph.h', REPO / 'loom/src/extract/candidate_graph.cpp']
    _REPORT = {'schema': 'loom.independent_candidate_native_parity/1', 'fixture_hashes': FROZEN_HASHES,
        'vocabulary_sha256': VOCABULARY_SHA256, 'binary_sha256': file_hash(tool),
        'python_reference_report_sha256': PYTHON_REPORT_SHA256,
        'mechanical_probes_payload_sha256': PROBES_PAYLOAD_SHA256,
        'runner_sha256': file_hash(Path(__file__).with_name('candidate_graph_native_tool.cpp')),
        'evaluator_sha256': file_hash(__file__),
        'native_source_sha256': {str(p.relative_to(REPO)): file_hash(p) for p in native_sources if p.is_file()},
        'summaries': summaries, 'rows': rows, 'mechanical_probes': probe_results,
        'limitations': ['Pure native validation of manually supplied candidate graphs; no model calls.',
            'Partial drafts remain local-handle inputs; no canonical IDs or full native Assessments are created.',
            'No native comparison graph is generated, so graph projection or matching parity is not measured.',
            'Error diagnostics are checked for shape/presence; exact message/order parity is not claimed.']}
    return _REPORT


class CandidateGraphNativeParity(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = evaluate()

    def assert_audit(self, *fields):
        for row in self.report['rows']:
            with self.subTest(case=row['id']):
                self.assertIsNone(row['transport_error'])
                for field in fields:
                    self.assertIs(row['audit'].get(field), True, field)

    def test_frozen_expected_validity_and_python_decisions(self):
        self.assert_audit('validity_matches_gold', 'validity_matches_python', 'status_matches_validity',
                          'errors_well_formed', 'error_presence_correct')

    def test_original_sources_and_bundles_are_retained(self):
        self.assert_audit('source_packet_retained', 'bundle_retained')

    def test_partial_drafts_do_not_fabricate_native_assessments(self):
        self.assert_audit('partial_entities_preserved', 'partial_claims_preserved',
                          'no_default_entity_assessment', 'no_default_claim_assessment')

    def test_coverage_abstention_and_rejection_are_separate(self):
        self.assert_audit('coverage_matches_gold', 'coverage_matches_python', 'accepted_representation_correct',
                          'semantic_accuracy_unclaimed')

    def test_no_inference_persistence_or_graph_projection(self):
        self.assert_audit('gates_closed', 'no_graph_projection_emitted', 'report_shape_expected',
                          'packet_hash_algorithm_declared', 'packet_hash_matches_gold')
        self.assert_audit('vocabulary_hash_matches_gold')

    def test_runner_rejects_malformed_json_and_wrong_envelopes(self):
        for data in (b'{', b'[]', b'{}', b'{"packet":{},"bundle":{},"vocabulary":{},"extra":true}'):
            with self.subTest(payload=data):
                process = run_bytes(find_tool(), data)
                self.assertEqual(process.returncode, 2)
                self.assertEqual(process.stdout, b'')

    def test_runner_enforces_stdin_bound(self):
        process = run_bytes(find_tool(), b' ' * (4 * 1024 * 1024 + 1))
        self.assertEqual(process.returncode, 2)
        self.assertIn(b'input_exceeds_4MiB', process.stderr)

    def test_runner_checks_nesting_before_parsing_but_ignores_quoted_brackets(self):
        process = run_bytes(find_tool(), b'[' * 130 + b']' * 130)
        self.assertEqual(process.returncode, 2)
        self.assertIn(b'input_exceeds_depth128', process.stderr)
        # Embedded quote and backslash exercise scanner escapes; source text
        # containing brackets must not be mistaken for recursive JSON objects.
        payload = {'packet': {'noise': '"' + '[' * 130 + '\\' + ']' * 130}, 'bundle': {}, 'vocabulary': {}}
        process = run_bytes(find_tool(), independent.canonical(payload).encode())
        self.assertEqual(process.returncode, 0)
        self.assertIs(json.loads(process.stdout)['valid'], False)

    def test_separate_mechanical_rejection_probes(self):
        self.assertEqual(len(self.report['mechanical_probes']), 4)
        for probe in self.report['mechanical_probes']:
            with self.subTest(probe=probe['id']):
                self.assertTrue(probe['passed'], probe['checks'])


if __name__ == '__main__':
    if '--write-report' in sys.argv:
        index = sys.argv.index('--write-report')
        destination = Path(sys.argv[index + 1])
        del sys.argv[index:index + 2]
        report = evaluate()
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
        print(json.dumps({'path': str(destination), 'summaries': report['summaries']}))
    unittest.main()
