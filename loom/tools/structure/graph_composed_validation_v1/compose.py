"""Frozen composition transport over supplied cases, never sealed-file loading.

The source compiler, citation binding, projection, lookup and formal operators
are imported unchanged. This file adds inventories, immutable first artifacts
and reference measurements; it cannot execute a model or read credentials.
"""
from __future__ import annotations

from collections import Counter
from copy import deepcopy
from datetime import datetime, timezone
import hashlib
from pathlib import Path

from .. import graph_panel_live as panel
from .. import graph_panel_validation as validation
from .. import graph_panel_score_run as integrity
from .. import new_graph_direct_lookup as direct
from .. import graph_source_commitment_projection as commitment
from .. import graph_formal_paths as formal
from ..turn_reference_binding_v1 import binding

HERE = Path(__file__).resolve().parent
ROOT = panel.ROOT
FREEZE = HERE / 'freeze_before_validation.json'
ARMS = ('original', 'citation_bound', 'individual_source_view')


def read(path):
    return panel.safe.parse_json(Path(path).read_bytes())


def dependencies():
    return [Path(__file__), HERE / 'PROTOCOL.md', HERE / 'test_compose.py',
            Path(panel.__file__), Path(validation.__file__), Path(integrity.__file__),
            Path(panel.safe.__file__), Path(validation.jev.__file__), integrity.SNAPSHOT,
            ROOT / 'docs/research/model_method_panel_v1/public_preflight/jev_endpoints.json',
            Path(binding.__file__), Path(binding.__file__).with_name('policy.json'),
            Path(commitment.binding.__file__),
            Path(direct.__file__), direct.POLICY_PATH,
            Path(commitment.__file__), commitment.POLICY,
            Path(formal.__file__), formal.POLICY_PATH]


def freeze():
    value = {'schema': 'loom.graph_composed_validation_freeze/1',
        'frozen_at_utc': datetime.now(timezone.utc).isoformat(),
        'validation_inputs_read': False, 'validation_gold_read': False,
        'semantic_methods_changed': False, 'api_calls': 0,
        'files_sha256': {str(p.relative_to(ROOT)): panel.digest_file(p) for p in dependencies()}}
    panel.write_new(FREEZE, value)
    return panel.digest_file(FREEZE)


def verify_freeze(expected_sha256):
    if panel.digest_file(FREEZE) != expected_sha256:
        raise ValueError('composition_freeze_hash_drift')
    frozen = read(FREEZE)
    expected_paths = {str(p.relative_to(ROOT)) for p in dependencies()}
    if set(frozen['files_sha256']) != expected_paths:
        raise ValueError('composition_dependency_inventory_drift')
    for name, digest in frozen['files_sha256'].items():
        if panel.digest_file(ROOT / name) != digest:
            raise ValueError('composition_dependency_hash_drift:' + name)
    return frozen


def _inventory(cases):
    by_case = {c['id']: c for c in cases}
    queries = {q['id']: (c, q) for c in cases for q in c['judgment_queries']}
    if len(by_case) != len(cases) or len(queries) != sum(len(c['judgment_queries']) for c in cases):
        raise ValueError('duplicate_case_or_query')
    if any(q['scope'] not in ('explicit_source', 'formal_implication') for _, q in queries.values()):
        raise ValueError('unregistered_query_scope')
    return by_case, queries


def extraction_records(cases, batches, release):
    """Revalidate original released runs, then expose verified GPT contents.

    Batches contain (manifest_path, run_directory). The caller must already have
    the root VerifiedRelease ticket; this helper never loads inputs or gold.
    Missing/rejected attempts remain present in the complete planned inventory.
    Billing exceptions are fatal and never converted into semantic abstention.
    """
    validation.require_verified_release(release)
    case_by, _ = _inventory(cases)
    rows, summaries, seen = [], [], set()
    for manifest_path, run_directory in batches:
        manifest = read(manifest_path)
        if (manifest.get('metadata', {}).get('instrument') != 'gpt' or
                manifest.get('metadata', {}).get('track') != 'assisted_extraction'):
            raise ValueError('only_assisted_extraction_batches')
        original, summary = validation.replay(manifest_path, run_directory, cases, release)
        summaries.append(summary)
        baseline = {r['case_id']: r for r in original}
        attempts = {a['id']: a for a in read(Path(run_directory) / 'ledger.json')['attempts']}
        snapshot = read(integrity.SNAPSHOT)
        for request in manifest['requests']:
            ident = request['id']
            if ident not in case_by or ident in seen:
                raise ValueError('duplicate_or_unknown_extraction_case')
            seen.add(ident)
            row = {'case_id': ident, 'state': 'unavailable',
                   'reason': baseline[ident].get('reason', 'response_unavailable'),
                   'compiled_original': deepcopy(baseline[ident]),
                   'manifest_sha256': summary['manifest_sha256'],
                   'ledger_sha256': summary['ledger_sha256']}
            attempt = attempts.get(ident, {})
            if (attempt.get('state') == 'completed' and attempt.get('http_status') == 200 and
                    'response_file' in attempt):
                raw = (Path(run_directory) / attempt['response_file']).read_bytes()
                if hashlib.sha256(raw).hexdigest() != attempt['response_sha256']:
                    raise ValueError('response_hash_drift')
                try:
                    content = integrity.gpt_content(raw, request['body'], snapshot)
                    integrity.billing_consistent(panel.safe._response_result(raw)['reported_cost_usd'], attempt)
                    row.update(state='completed', content=content,
                               raw_response_sha256=attempt['response_sha256'])
                except (KeyError, TypeError, ValueError, IndexError):
                    pass
            rows.append(row)
    if seen != set(case_by):
        raise ValueError('complete_assisted_case_inventory_required')
    return rows, summaries


def _unavailable(case_id, reason):
    return {'case_id': case_id, 'state': 'unavailable', 'reason': reason}


def _formal_row(case, graph, query):
    if not isinstance(graph, dict) or graph.get('state') != 'completed':
        return {'case_id': case['id'], 'query_id': query['id'], 'scope': query['scope'],
                'state': 'unavailable', 'label': None, 'paths': [],
                'reason': 'missing_or_unavailable_compiled_graph', 'basis_class': 'none',
                'content_truth': 'unverified', 'no_graph_promotion': True}
    try:
        row = formal.solve_paths(graph['source_assertions'], graph['status_events'], query,
                                 premise_mode='model_predicted_source_assertion')
        row['case_id'] = case['id']
        return row
    except (KeyError, TypeError, ValueError):
        return {'case_id': case['id'], 'query_id': query['id'], 'scope': query['scope'],
                'state': 'unavailable', 'label': None, 'paths': [],
                'reason': 'frozen_formal_operator_rejected_graph', 'basis_class': 'none',
                'content_truth': 'unverified', 'no_graph_promotion': True}


def compute_cases(cases, records, *, split, expected_freeze_sha256):
    """Pure replay over supplied inputs, with no gold or output-directory access."""
    verify_freeze(expected_freeze_sha256)
    if split not in ('dev', 'validation', 'synthetic'):
        raise ValueError('unrecognized_split')
    case_by, queries = _inventory(cases)
    record_by = {r['case_id']: r for r in records}
    if len(record_by) != len(records) or set(record_by) != set(case_by):
        raise ValueError('complete_record_inventory_required')
    compiled = {arm: [] for arm in ARMS}; derivations = []; sidecars = []; audits = []
    rows = {arm: {'explicit_source': [], 'formal_implication': []} for arm in ARMS}
    for case in cases:
        record = record_by[case['id']]
        original = deepcopy(record['compiled_original'])
        if original.get('case_id') != case['id']:
            raise ValueError('compiled_case_identity_drift')
        bound = _unavailable(case['id'], 'unavailable_raw_model_content')
        if record.get('state') == 'completed':
            try:
                value = panel.safe.parse_json(record['content'])
                recompiled = panel.compile_extraction(value, case)
                if recompiled != original:
                    raise RuntimeError('original_compiler_replay_drift')
                derived, provenance = binding.bind_turn_references(value, case,
                    raw_response_sha256=record['raw_response_sha256'])
                if binding.quote_free_copy(derived) != binding.quote_free_copy(value):
                    raise RuntimeError('binding_changed_typed_fields')
                bound = panel.compile_extraction(derived, case)
                derivations.append({'case_id': case['id'], 'raw_response_sha256': record['raw_response_sha256'],
                                    'value': derived})
                sidecars.extend(provenance)
            except (KeyError, TypeError, ValueError):
                bound = _unavailable(case['id'], 'binding_or_compiler_rejected_response')
        projected, audit = commitment.project(bound, case)
        audits.append({'case_id': case['id'], 'audit': audit})
        graphs = {'original': original, 'citation_bound': bound, 'individual_source_view': projected}
        for arm, graph in graphs.items():
            compiled[arm].append(graph)
            for query in case['judgment_queries']:
                row = (direct.lookup(case, graph, query) if query['scope'] == 'explicit_source'
                       else _formal_row(case, graph, query))
                row.update(graph_variant=arm, split=split,
                    pipeline_mode=direct.PIPELINE_MODE, causal_prefix_extraction_verified=False)
                rows[arm][query['scope']].append(row)
    result = {'schema': 'loom.graph_composed_first_predictions/1', 'split': split,
        'freeze_sha256': expected_freeze_sha256, 'case_count': len(cases), 'query_count': len(queries),
        'queries_by_scope': dict(Counter(q['scope'] for _, q in queries.values())), 'arms': rows,
        'gold_accessed_during_prediction': False, 'model_response_available_at': 'not_in_compiled_ABI',
        'pipeline_mode': direct.PIPELINE_MODE, 'causal_prefix_extraction_verified': False,
        'content_truth': 'unverified', 'no_graph_promotion': True}
    return {'supplied_cases_first.json': cases, 'raw_model_contents_first.json': records,
            'compiled_variants_first.json': compiled, 'binding_derivations_first.json': derivations,
            'binding_provenance_first.json': sidecars, 'projection_audits_first.json': audits,
            'first_predictions.json': result}


def predict_cases(cases, records, output, *, split, expected_freeze_sha256):
    """Persist all graph variants and predictions exclusively before gold.

    Caller-supplied records are from extraction_records, or explicitly labelled
    synthetic/DEV checks. No gold argument exists in this prediction function.
    """
    artifacts = compute_cases(cases, records, split=split,
                              expected_freeze_sha256=expected_freeze_sha256)
    folder = Path(output); folder.mkdir(parents=True, exist_ok=False)
    for name, value in artifacts.items():
        panel.write_new(folder / name, value)
    verify_freeze(expected_freeze_sha256)
    panel.write_new(folder / 'freeze_before_gold.json', {
        'schema': 'loom.graph_composed_before_gold/1', 'split': split,
        'freeze_sha256': expected_freeze_sha256, 'gold_read_by_wrapper': False,
        'files_sha256': {p.name: panel.digest_file(p) for p in sorted(folder.iterdir()) if p.is_file()}})
    return artifacts['first_predictions.json']


def _scope_gold(cases, golds, scope):
    ids = {q['id'] for c in cases for q in c['judgment_queries'] if q['scope'] == scope}
    result = []
    for gold in golds:
        value = deepcopy(gold)
        value['judgments'] = [j for j in gold['judgments'] if j['query_id'] in ids]
        result.append(value)
    if (len(ids) != sum(len(g['judgments']) for g in result) or
            ids != {j['query_id'] for g in result for j in g['judgments']}):
        raise ValueError('scope_gold_inventory_drift')
    return result


def _alignment(case, gold, graph):
    """Strict unique typed/source-turn correspondence, never best-fit mapping."""
    result = {}
    for edge in graph.get('source_assertions', []) if graph.get('state') == 'completed' else []:
        try:
            panel._validated_compiled_evidence(edge, case)
            turns = {e['turn_id'] for e in edge['evidence']}
            candidates = [g for g in gold['source_assertions'] if panel._edge_key(edge) == panel._edge_key(g)
                and turns and turns <= {e['turn_id'] for e in g['evidence']}]
            if len(candidates) == 1:
                result[edge['id']] = candidates[0]['id']
        except (KeyError, TypeError, ValueError):
            continue
    return result


def path_metrics(cases, golds, graphs, predictions):
    """Alignment-dependent path/unique-premise PR with all planned denominators."""
    graph_by = {g['case_id']: g for g in graphs}; gold_by = {g['id']: g for g in golds}
    case_by, _ = _inventory(cases); totals = Counter(); premise_totals = Counter(); details = []
    for row in predictions:
        case, gold = case_by[row['case_id']], gold_by[row['case_id']]
        annotated = [p for p in gold.get('formal_paths', []) if p['query_id'] == row['query_id']]
        if len(annotated) > 1:
            raise ValueError('duplicate_formal_path_annotation')
        gold_paths = [tuple(p) for p in annotated[0]['minimal_support_paths']] if annotated else []
        if len(set(gold_paths)) != len(gold_paths):
            raise ValueError('duplicate_gold_support_path')
        mapping = _alignment(case, gold, graph_by[row['case_id']])
        paths = row.get('paths', []) if row.get('state') == 'completed' and row.get('complete') is True else []
        actual = [tuple(mapping.get(i, 'UNMAPPED_MODEL_ID:' + i) for i in p['premise_assertion_ids']) for p in paths]
        expected = Counter(gold_paths); predicted = Counter(actual)
        tp = sum((expected & predicted).values()); fp = sum((predicted - expected).values()); fn = sum((expected - predicted).values())
        expected_premises = {i for path in gold_paths for i in path}
        predicted_premises = {i for path in actual for i in path}
        ptp = len(expected_premises & predicted_premises); pfp = len(predicted_premises - expected_premises); pfn = len(expected_premises - predicted_premises)
        totals.update(tp=tp, fp=fp, fn=fn); premise_totals.update(tp=ptp, fp=pfp, fn=pfn)
        details.append({'query_id': row['query_id'], 'case_id': row['case_id'], 'state': row['state'],
            'gold_paths': gold_paths, 'predicted_aligned_paths': actual,
            'all_minimal_alternatives_preserved': fp == 0 and fn == 0 and row.get('state') == 'completed',
            'paths': panel._metric(tp, fp, fn), 'unique_premises_per_query': panel._metric(ptp, pfp, pfn)})
    return {'query_count': len(predictions), 'paths': panel._metric(totals['tp'], totals['fp'], totals['fn']),
        'unique_premises_per_query': panel._metric(premise_totals['tp'], premise_totals['fp'], premise_totals['fn']),
        'all_minimal_alternatives_preserved_queries': sum(d['all_minimal_alternatives_preserved'] for d in details),
        'details': details, 'semantic_alignment': 'strict_unique_typed_source_turn_correspondence',
        'world_truth_accuracy': None}


def evaluate_cases(cases, golds, folder, *, expected_freeze_sha256):
    """Caller supplies gold only after exclusive first prediction files exist."""
    verify_freeze(expected_freeze_sha256)
    folder = Path(folder); before = read(folder / 'freeze_before_gold.json')
    if before['freeze_sha256'] != expected_freeze_sha256 or before['gold_read_by_wrapper'] is not False:
        raise ValueError('before_gold_freeze_drift')
    for name, digest in before['files_sha256'].items():
        if Path(name).name != name or panel.digest_file(folder / name) != digest:
            raise ValueError('first_prediction_artifact_drift')
    if read(folder / 'supplied_cases_first.json') != cases:
        raise ValueError('prediction_input_case_drift')
    if len({g['id'] for g in golds}) != len(golds) or {g['id'] for g in golds} != {c['id'] for c in cases}:
        raise ValueError('case_gold_inventory_drift')
    predicted = read(folder / 'first_predictions.json'); graphs = read(folder / 'compiled_variants_first.json')
    recomputed = compute_cases(cases, read(folder / 'raw_model_contents_first.json'),
        split=predicted['split'], expected_freeze_sha256=expected_freeze_sha256)
    if set(before['files_sha256']) != set(recomputed):
        raise ValueError('first_artifact_inventory_drift')
    for name, value in recomputed.items():
        if read(folder / name) != value:
            raise ValueError('first_computation_replay_drift:' + name)
    result = {'schema': 'loom.graph_composed_first_results/1', 'split': predicted['split'],
        'freeze_sha256': expected_freeze_sha256, 'first_predictions_sha256': panel.digest_file(folder / 'first_predictions.json'),
        'gold_supplied_after_prediction_freeze': True, 'arms': {},
        'pipeline_mode': direct.PIPELINE_MODE, 'causal_prefix_extraction_verified': False,
        'content_truth_accuracy': None, 'no_graph_promotion': True}
    for arm in ARMS:
        explicit = panel.score_judgments(_scope_gold(cases, golds, 'explicit_source'), predicted['arms'][arm]['explicit_source'])
        inferred = panel.score_judgments(_scope_gold(cases, golds, 'formal_implication'), predicted['arms'][arm]['formal_implication'])
        # Only original and citation-bound variants measure extraction. The
        # individual-source view is a projection, not a repaired extractor.
        extraction = None if arm == 'individual_source_view' else panel.score_extraction(cases, golds, graphs[arm])
        for score in (explicit, inferred, extraction):
            if score is not None: score['split'] = predicted['split']
        result['arms'][arm] = {'explicit_source': explicit, 'formal_implication_labels': inferred,
            'formal_paths': path_metrics(cases, golds, graphs[arm], predicted['arms'][arm]['formal_implication']),
            'extraction': extraction}
    panel.write_new(folder / 'first_results.json', result)
    return result
