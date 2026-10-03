"""Generic frozen graph-local wrapper: caller supplies released cases/gold."""
from __future__ import annotations
from collections import Counter
from copy import deepcopy
import json
from pathlib import Path
import sys
import time

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
DEV = TOOLS / 'graph_local_baselines_v1'
CACHE = TOOLS / 'local_embedding_panel_v1'
sys.path.insert(0, str(DEV))
import local_baselines as local
import evaluate_dev as evaluation

RELEASE = HERE / 'release.json'
METHODS = evaluation.METHODS


def release_dependencies():
    paths = [HERE / name for name in ('graph_local_validation.py', 'PROTOCOL.md', 'test_wrapper.py')]
    paths += [DEV / name for name in ('local_baselines.py', 'evaluate_dev.py', 'embed_queries.py',
                                     'policy.json', 'PROTOCOL.md', 'release.json')]
    paths += [TOOLS / name for name in ('graph_panel_live.py', 'openrouter_runner.py', 'local_embedding_panel.py')]
    paths += [CACHE / name for name in ('model_manifest.json', 'protocol.json', 'instrument_environment.json')]
    return paths


def freeze_release():
    value = {'schema': 'loom.graph_local_generic_release/1',
             'validation_accessed': False, 'model_or_method_changes': False,
             'files': {str(p.relative_to(TOOLS)): local.file_sha(p) for p in release_dependencies()}}
    local.write_new(RELEASE, value)
    return local.file_sha(RELEASE)


def verify_release(expected_release_sha256):
    if local.file_sha(RELEASE) != expected_release_sha256:
        raise ValueError('generic_release_hash_drift')
    value = json.loads(RELEASE.read_text())
    if set(value['files']) != {str(p.relative_to(TOOLS)) for p in release_dependencies()}:
        raise ValueError('generic_release_inventory_drift')
    for name, expected in value['files'].items():
        if local.file_sha(TOOLS / name) != expected:
            raise ValueError('generic_dependency_drift:' + name)
    return value


def prepare_cases(cases, *, split, expected_release_sha256):
    verify_release(expected_release_sha256)
    if split not in ('dev', 'validation'):
        raise ValueError('unrecognized_split')
    if len({c['id'] for c in cases}) != len(cases):
        raise ValueError('duplicate_case_id')
    policy = json.loads((DEV / 'policy.json').read_text())
    rows = []; excluded = []
    for case in cases:
        for query in case['judgment_queries']:
            if query['scope'] != 'explicit_source':
                excluded.append({'case_id': case['id'], 'query_id': query['id'], 'scope': query['scope']})
                continue
            rows.append(local.prepare_query(case, query, policy))
    if len({r['query_id'] for r in rows}) != len(rows) or not rows:
        raise ValueError('empty_or_duplicate_query_inventory')
    return {'schema': 'loom.research.graph_local_prepared/1', 'split': split,
            'input_cases_canonical_sha256': local.sha_bytes(local.canonical(cases).encode()),
            'policy_sha256': local.file_sha(DEV / 'policy.json'), 'release_sha256': expected_release_sha256,
            'cases': len(cases), 'query_count': len(rows), 'queries': deepcopy(rows),
            'excluded_non_explicit_source_queries': excluded}


def score_prepared(prepared, embedding):
    """Transport generalization only; unchanged token/char/learned/max formula."""
    vector_rows = {r['query_id']: r for r in embedding['queries']}
    if len(vector_rows) != len(embedding['queries']) or set(vector_rows) != {q['query_id'] for q in prepared['queries']}:
        raise ValueError('embedding_query_inventory_drift')
    rows = []
    for query in prepared['queries']:
        embedded = vector_rows[query['query_id']]
        if embedded['prefix_sha256'] != query['prefix_sha256']:
            raise ValueError('embedding_prefix_drift')
        cosines = {c['turn_id']: c['score'] for c in embedded['candidates']}
        if len(cosines) != len(embedded['candidates']) or set(cosines) != {c['turn_id'] for c in query['candidates']}:
            raise ValueError('embedding_candidate_inventory_drift')
        qt, qc = local.tokens(query['query_text']), local.chars(query['query_text']); candidates = []
        for c in query['candidates']:
            values = {'lexical_token_cosine': local.cosine(qt, local.tokens(c['representation_text'])),
                      'character_3_5_cosine': local.cosine(qc, local.chars(c['representation_text'])),
                      'learned_minilm_cosine': cosines[c['turn_id']]}
            values['nongating_union'] = local.union(values.values())
            candidates.append({k: c[k] for k in ('source_id', 'turn_id', 'known_at', 'text_sha256',
                                                'representation_sha256', 'eligible_order', 'span')} | {'scores': values})
        rankings = {m: [c['turn_id'] for c in local.rank_candidates(candidates, m) if c['scores'][m] is not None] for m in METHODS}
        rows.append({'case_id': query['case_id'], 'query_id': query['query_id'], 'language': query['language'],
                     'prefix_sha256': query['prefix_sha256'], 'as_of': query['query']['as_of'],
                     'query_text_sha256': query['query_text_sha256'],
                     'future_excluded_turn_count': query['future_excluded_turn_count'], 'candidates': candidates,
                     'rankings': rankings, 'maximum_scores': {m: local.union(c['scores'][m] for c in candidates) for m in METHODS},
                     'prediction': None, 'source_judgment_available': False, 'retrieval_available': bool(candidates)})
    return {'schema': 'loom.research.graph_local_scores/1', 'split': prepared['split'],
            'release_sha256': prepared['release_sha256'], 'rows': rows, 'query_count': len(rows),
            'source_judgment_available': False, 'no_graph_promotion': True}


def embed_prepared(prepared, output, *, expected_release_sha256):
    verify_release(expected_release_sha256)
    if prepared['release_sha256'] != expected_release_sha256:
        raise ValueError('prepared_release_binding_drift')
    output = Path(output); output.mkdir(exist_ok=False)
    local.write_new(output / 'prepared_inputs.json', prepared)
    sys.path.insert(0, str(CACHE / 'runtime_packages')); sys.path.insert(0, str(TOOLS))
    import local_embedding_panel as instrument
    environment = json.loads((CACHE / 'instrument_environment.json').read_text())
    protocol = {'schema': 'loom.graph_local_generic_embedding/1', 'split': prepared['split'],
                'prepared_sha256': local.file_sha(output / 'prepared_inputs.json'),
                'release_sha256': expected_release_sha256, 'download': False, 'paid_calls': 0,
                'model_id': instrument.MODEL_ID, 'revision': instrument.REVISION,
                'max_tokens': instrument.MAX_TOKENS, 'threads': environment['threads'],
                'score': 'float64dotofL2normalizedfloat32vectors', 'prediction': None}
    local.write_new(output / 'embedding_protocol.json', protocol)
    texts = sorted({q['query_text'] for q in prepared['queries']} |
                   {c['representation_text'] for q in prepared['queries'] for c in q['candidates']})
    np, embed, runtime = instrument.load_instrument(CACHE, environment['threads'])
    started = time.monotonic(); vectors = []; index = []
    for text in texts:
        vector, meta = embed(text); vectors.append(vector)
        index.append({'text': text, 'text_sha256': local.sha_bytes(text.encode()), 'tokenization': meta})
    matrix = np.stack(vectors).astype(np.float32)
    with (output / 'vectors.npy').open('xb') as stream:
        np.save(stream, matrix, allow_pickle=False)
    elapsed = time.monotonic() - started; lookup = {text: i for i, text in enumerate(texts)}; embedding_rows = []
    for q in prepared['queries']:
        a = matrix[lookup[q['query_text']]].astype(np.float64)
        embedding_rows.append({'query_id': q['query_id'], 'case_id': q['case_id'], 'prefix_sha256': q['prefix_sha256'],
            'candidates': [{'turn_id': c['turn_id'], 'score': float(np.dot(a, matrix[lookup[c['representation_text']]].astype(np.float64)))} for c in q['candidates']]})
    embedding = {'queries': embedding_rows, 'runtime': runtime, 'dimensions': 384, 'unique_texts': len(texts),
                 'truncated_unique_texts': sum(r['tokenization']['truncated'] for r in index),
                 'elapsed_inference_seconds': elapsed, 'model_initialization_excluded': True, 'paid_calls': 0}
    local.write_new(output / 'text_vector_index.json', {'rows': index})
    local.write_new(output / 'embedding_scores.json', embedding)
    scores = score_prepared(prepared, embedding)
    local.write_new(output / 'first_scores.json', scores)
    verify_release(expected_release_sha256)
    local.write_new(output / 'freeze_before_gold.json', {'schema': 'loom.graph_local_generic_before_gold/1',
        'split': prepared['split'], 'release_sha256': expected_release_sha256, 'gold_accessed_by_wrapper': False,
        'files': {p.name: local.file_sha(p) for p in sorted(output.iterdir()) if p.is_file()}})
    return scores


def evaluate_cases(cases, golds, firstscores, *, expected_release_sha256):
    """Gold argument must be supplied after persisted first-score/vector freeze."""
    verify_release(expected_release_sha256)
    path = Path(firstscores); folder = path.parent
    if path.name != 'first_scores.json':
        raise ValueError('first_score_artifact_required')
    freeze = json.loads((folder / 'freeze_before_gold.json').read_text())
    if freeze['release_sha256'] != expected_release_sha256 or not freeze['gold_accessed_by_wrapper'] is False:
        raise ValueError('before_gold_release_drift')
    for name, expected in freeze['files'].items():
        if Path(name).name != name or local.file_sha(folder / name) != expected:
            raise ValueError('first_artifact_hash_drift')
    scores = json.loads(path.read_text()); prepared = json.loads((folder / 'prepared_inputs.json').read_text())
    expected = prepare_cases(cases, split=prepared['split'], expected_release_sha256=expected_release_sha256)
    if expected != prepared or scores['release_sha256'] != expected_release_sha256:
        raise ValueError('case_or_score_binding_drift')
    # Reproduce lexical/fusion computations from the immutable learned scores.
    embedding = json.loads((folder / 'embedding_scores.json').read_text())
    if score_prepared(prepared, embedding) != scores:
        raise ValueError('first_score_computation_drift')
    ids = {q['query_id'] for q in prepared['queries']}; relevant_gold = []
    if len({g['id'] for g in golds}) != len(golds) or {g['id'] for g in golds} != {c['id'] for c in cases}:
        raise ValueError('case_gold_inventory_drift')
    for g in golds:
        filtered = deepcopy(g); filtered['judgments'] = [j for j in g['judgments'] if j['query_id'] in ids]
        if filtered['judgments']: relevant_gold.append(filtered)
    judgments = [j for g in relevant_gold for j in g['judgments']]
    if len(judgments) != len(ids) or {j['query_id'] for j in judgments} != ids:
        raise ValueError('gold_query_inventory_drift')
    case_map = {c['id']: c for c in cases}; queries = {q['query_id']: q for q in prepared['queries']}
    score_map = {s['query_id']: s for s in scores['rows']}; rows = []; outside = events = 0
    for g in relevant_gold:
        for judgment in g['judgments']:
            ident = judgment['query_id']; q = queries[ident]
            relevant, counts = evaluation.relevant_evidence(case_map[g['id']], q['query'], g, judgment)
            outside += counts['out_of_prefix_annotations']; events += counts['status_events_used']
            rows.append({'query_id': ident, 'case_id': g['id'], 'family': g['family'], 'language': g['language'],
                         'label': judgment['label'], 'relevant_evidence': relevant,
                         'relevant_turn_ids': sorted({e['turn_id'] for e in relevant}), 'score': score_map[ident]})
    policy = json.loads((DEV / 'policy.json').read_text()); retrieval = {}; naive = {}; predictions = {}
    always = [{'query_id': r['query_id'], 'state': 'completed', 'label': 'unknown'} for r in rows]
    naive['always_unknown'] = local.adapter.score_judgments(relevant_gold, always)
    predictions['always_unknown'] = always
    for method in METHODS:
        retrieval[method] = {'overall': evaluation.ranking_summary(rows, method, policy['ranking_k'], policy['thresholds']), 'groups': {}}
        for field in ('family', 'language', 'label'):
            groups = {}
            for r in rows: groups.setdefault(r[field], []).append(r)
            retrieval[method]['groups'][field] = {k: evaluation.ranking_summary(v, method, policy['ranking_k'], policy['thresholds']) for k, v in sorted(groups.items())}
        naive[method] = {}
        for threshold in policy['thresholds']:
            pred = [{'query_id': r['query_id'], 'state': 'completed' if r['score']['maximum_scores'][method] is not None else 'unavailable',
                     'label': local.naive_label(r['score']['maximum_scores'][method], threshold)} for r in rows]
            predictions[method + '/' + str(threshold)] = pred
            naive[method][str(threshold)] = local.adapter.score_judgments(relevant_gold, pred)
    result = {'schema': 'loom.research.graph_local_generic_results/1', 'split': prepared['split'],
              'queries': len(rows), 'cases': len(cases), 'label_counts': dict(Counter(r['label'] for r in rows)),
              'release_sha256': expected_release_sha256, 'source_judgment_available': False,
              'future_candidate_leaks': 0, 'out_of_prefix_gold_annotations': outside,
              'query_status_event_references_used': events, 'retrieval': retrieval, 'naive_judgments': naive,
              'query_evidence_targets': [dict(r, score=None) for r in rows], 'no_graph_promotion': True,
              'content_truth_accuracy': None, 'gold_evaluation_after_first_score_freeze': True}
    local.write_new(folder / 'first_results.json', result)
    local.write_new(folder / 'naive_predictions.json', {'predictions': predictions})
    return result


if __name__ == '__main__':
    print(freeze_release())
